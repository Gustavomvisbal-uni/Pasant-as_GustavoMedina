/**
 * @file lcd_manager.c
 * @brief Driver LCD 2004A con módulo I2C PCF8574 para ESP32-S3.
 *
 * Arquitectura:
 *   - Capa hardware (pcf_write, pulse_en, send_nibble, lcd_cmd, lcd_putc)
 *     solo se ejecuta desde la tarea lcd_task. NUNCA desde app_main.
 *   - Todas las funciones públicas (lcd_show_*, lcd_print_*) simplemente
 *     encolan un mensaje y retornan inmediatamente — no bloquean.
 *   - lcd_task consume la cola y escribe al LCD sin afectar el bucle principal.
 */

#include "lcd_manager.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h"
#include "i2c_manager.h"
#include "esp_timer.h"

#include "estado_sistema.h"

static const char *TAG = "LCD_MGR";

/* ============================================================================
 * Bits del PCF8574
 * ========================================================================== */
#define PCF_RS  0x01
#define PCF_EN  0x04
#define PCF_BL  0x08

static const uint8_t ROW_OFFSET[4] = { 0x00, 0x40, 0x14, 0x54 };

/* ============================================================================
 * Cola interna de mensajes
 * ========================================================================== */
typedef enum {
    LCD_OP_PRINT,       /* escribe texto en (row, col) */
    LCD_OP_CLEAR,       /* borra toda la pantalla       */
    LCD_OP_BACKLIGHT,   /* cambia backlight              */
} lcd_op_t;

typedef struct {
    lcd_op_t op;
    uint8_t  row;
    uint8_t  col;
    bool     bl;
    char     text[LCD_COLS + 1];
} lcd_msg_t;

static QueueHandle_t s_queue     = NULL;
static bool          s_backlight = true;
static bool          s_ready     = false;

/************************ Modificiación para i2c aparte *************************************************/
static i2c_master_dev_handle_t s_lcd_dev = NULL;
static uint8_t  s_error_count    = 0;
static volatile bool s_necesita_reinit = false;

#define LCD_MAX_ERRORES_ANTES_DE_REINIT  5
#define LCD_REINIT_PERIODICO_MS  ( 10 * 60000)  // cada 10 minuto, preventivo
static int64_t s_ultimo_reinit = 0;

/********************************************************************************************************/

/* ============================================================================
 * Capa hardware — solo llamar desde lcd_task
 * ========================================================================== */
static void pcf_write(uint8_t b)
{
    uint8_t data = b | (s_backlight ? PCF_BL : 0);
    esp_err_t r = i2c_master_transmit(s_lcd_dev, &data, 1, 50);

    if (r != ESP_OK) {
        ESP_LOGD(TAG, "I2C: 0x%02X -> %s", data, esp_err_to_name(r));
        s_error_count++;
        if (s_error_count >= LCD_MAX_ERRORES_ANTES_DE_REINIT) {
            s_necesita_reinit = true;
            s_error_count = 0;
        }
    } else {
        s_error_count = 0;
    }
}
static void pulse_en(uint8_t d)
{
    pcf_write(d & ~PCF_EN);
    esp_rom_delay_us(2);
    pcf_write(d | PCF_EN);
    esp_rom_delay_us(2);
    pcf_write(d & ~PCF_EN);
    esp_rom_delay_us(200);
}

static void send_nibble(uint8_t nibble_high, uint8_t rs)
{
    pulse_en((nibble_high & 0xF0) | rs);
}

static void hw_cmd(uint8_t cmd)
{
    send_nibble(cmd,      0);
    send_nibble(cmd << 4, 0);
    esp_rom_delay_us(500);
}

static void hw_putc(uint8_t c)
{
    send_nibble(c,      PCF_RS);
    send_nibble(c << 4, PCF_RS);
    esp_rom_delay_us(500);
}

static void hw_goto(uint8_t row, uint8_t col)
{
    hw_cmd(0x80 | (ROW_OFFSET[row] + col));
}

static void hw_puts_n(const char *s, uint8_t n)
{
    for (uint8_t i = 0; i < n && s[i]; i++)
        hw_putc((uint8_t)s[i]);
}

/* Secuencia de reset/config del HD44780. Se usa tanto en la
 * inicialización normal como para recuperarse de una caída de
 * energía que dejó al controlador en estado de reset. */
static void hw_reset_sequence(void)
{
    send_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(10));
    send_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(5));
    send_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(2));
    send_nibble(0x20, 0); vTaskDelay(pdMS_TO_TICKS(2));

    hw_cmd(0x28);
    hw_cmd(0x08);
    hw_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(5));
    hw_cmd(0x06);
    hw_cmd(0x0C);
}


/* ============================================================================
 * Tarea LCD — consumidora exclusiva del bus I2C del LCD
 * ========================================================================== */
static void lcd_task(void *arg)
{
    lcd_msg_t msg;
    while (1) {
        if (s_necesita_reinit) {
            ESP_LOGW(TAG, "Recuperando LCD tras fallos de comunicación...");
            hw_reset_sequence();
            s_necesita_reinit = false;
        }
	        
        int64_t ahora = esp_timer_get_time() / 1000;
        
		if (ahora - s_ultimo_reinit >= LCD_REINIT_PERIODICO_MS) {
		    hw_reset_sequence();
		    s_ultimo_reinit = ahora;
		}

        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (msg.op) {
                case LCD_OP_PRINT:
                    hw_goto(msg.row, msg.col);
                    hw_puts_n(msg.text, LCD_COLS - msg.col);
                    break;
                case LCD_OP_CLEAR:
                    hw_cmd(0x01);
                    vTaskDelay(pdMS_TO_TICKS(5));
                    break;
                case LCD_OP_BACKLIGHT:
                    s_backlight = msg.bl;
                    pcf_write(0x00);
                    break;
            }
        }
    }
}

/* Encola un mensaje; si la cola está llena el mensaje se descarta */
static void enqueue(lcd_msg_t *msg)
{
    if (!s_ready) return;
    xQueueSend(s_queue, msg, 0);
}



/* ============================================================================
 * Inicialización (se ejecuta en app_main, antes de wifi_init_sta)
 * ========================================================================== */
void lcd_init(void)
{
    ESP_LOGI(TAG, "Inicializando LCD 2004A (0x%02X, via i2c_manager)", LCD_I2C_ADDR);
	
	esp_err_t err = i2c_manager_init_lcd_bus();
    if (err != ESP_OK) return;
	
    err = i2c_manager_add_device_lcd(LCD_I2C_ADDR, I2C_LCD_FREQ_HZ, &s_lcd_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo agregar la LCD: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    hw_reset_sequence();

    /* Cola y tarea dedicada */
    s_queue = xQueueCreate(32, sizeof(lcd_msg_t));
    xTaskCreatePinnedToCore(lcd_task, "lcd_task", 4096, NULL, 2, NULL, 1);
    s_ready = true;

    ESP_LOGI(TAG, "LCD lista");
}

void lcd_clear(void)
{
    lcd_msg_t msg = { .op = LCD_OP_CLEAR };
    enqueue(&msg);
}

void lcd_set_backlight(bool on)
{
    lcd_msg_t msg = { .op = LCD_OP_BACKLIGHT, .bl = on };
    enqueue(&msg);
}

/* ============================================================================
 * Helpers internos de formato → cola
 * ========================================================================== */

/* Encola texto exacto en (row, col) */
static void enqueue_text(uint8_t row, uint8_t col, const char *text)
{
    if (row >= LCD_ROWS || col >= LCD_COLS || !text) return;
    lcd_msg_t msg = { .op = LCD_OP_PRINT, .row = row, .col = col };
    uint8_t avail = LCD_COLS - col;
    snprintf(msg.text, avail + 1, "%s", text);
    enqueue(&msg);
}

/* Encola fila completa de exactamente LCD_COLS chars (rellena con espacios) */
static void enqueue_row(uint8_t row, const char *text)
{
    if (row >= LCD_ROWS || !text) return;
    lcd_msg_t msg = { .op = LCD_OP_PRINT, .row = row, .col = 0 };
    memset(msg.text, ' ', LCD_COLS);
    msg.text[LCD_COLS] = '\0';
    size_t len = strlen(text);
    if (len > LCD_COLS) len = LCD_COLS;
    memcpy(msg.text, text, len);
    enqueue(&msg);
}

/* Centra un texto en la fila indicada, rellenando con espacios a los lados */
static void print_centrado(uint8_t row, const char *texto)
{
    if (row >= LCD_ROWS || !texto) return;
    size_t len = strlen(texto);
    if (len > LCD_COLS) len = LCD_COLS;
    uint8_t pad = (LCD_COLS - (uint8_t)len) / 2;
    char line[LCD_COLS + 1];
    memset(line, ' ', LCD_COLS);
    line[LCD_COLS] = '\0';
    memcpy(line + pad, texto, len);
    enqueue_row(row, line);
}

/* ============================================================================
 * API Nivel 1
 * ========================================================================== */
void lcd_print_at(uint8_t row, uint8_t col, const char *fmt, ...)
{
    if (!fmt || row >= LCD_ROWS || col >= LCD_COLS) return;
    char buf[LCD_COLS + 1];
    va_list a; va_start(a, fmt);
    vsnprintf(buf, LCD_COLS - col + 1, fmt, a);
    va_end(a);
    enqueue_text(row, col, buf);
}

/* ============================================================================
 * API Nivel 2
 * ========================================================================== */
void lcd_print_row(uint8_t row, const char *fmt, ...)
{
    if (!fmt || row >= LCD_ROWS) return;
    char buf[LCD_COLS * 2 + 1];
    va_list a; va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    enqueue_row(row, buf);
}

void lcd_print_clipped(uint8_t row, uint8_t col, const char *fmt, ...)
{
    if (!fmt || row >= LCD_ROWS || col >= LCD_COLS) return;
    char buf[LCD_COLS + 1];
    va_list a; va_start(a, fmt);
    vsnprintf(buf, LCD_COLS - col + 1, fmt, a);
    va_end(a);
    enqueue_text(row, col, buf);
}

/* ============================================================================
 * API Nivel 3
 * ========================================================================== */
void lcd_show_dht(float temperatura, float humedad)
{
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "T:%.1fC  H:%.1f%%", temperatura, humedad);
    enqueue_row(LCD_ROW_SENSOR_DHT, buf);
}

void lcd_show_sensor_error(const char *motivo)
{
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "Sensor: err %.7s", motivo ? motivo : "?");
    enqueue_row(LCD_ROW_SENSOR_DHT, buf);
}

void lcd_show_mqtt(bool conectado)
{
    enqueue_row(LCD_ROW_ESTADO,
                conectado ? "MQTT: conectado" : "MQTT: espera...");
}

void lcd_show_estado(const char *texto)
{
    if (texto) enqueue_row(LCD_ROW_ESTADO, texto);
}

void lcd_show_titulo(const char *texto)
{
    print_centrado(LCD_ROW_TITULO, texto);
}

void lcd_show_comando(const char *payload)
{
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "CMD: %.15s", payload ? payload : "");
    enqueue_row(LCD_ROW_COMANDO, buf);
}

/* ============================================================================
 * Pantallas rotativas — lcd_bucle
 * ========================================================================== */

/*
 * Formatea un valor de sensor con 1 decimal, o "--.-" si todavía no
 * hay dato real (NAN de un DS18B20 desconectado, o 0.0 de un sensor
 * analógico aún no integrado). El sufijo se concatena tal cual
 * (unidad, símbolo de grado, etc.), sin reinterpretarlo como formato.
 */
static void fmt_valor(char *out, size_t n, float v, const char *sufijo)
{
    if (isnan(v) || v == 0.0f) {
        snprintf(out, n, "--.-%s", sufijo);
    } else {
        snprintf(out, n, "%4.1f%s", v, sufijo);
    }
}

void lcd_bucle(void *pvParameters)
{
    char buf_a[12], buf_b[12], buf_c[12];
    char fecha[20];

    while (1) {
        /* ==========================================================
         * PANTALLA 1: AGUA (temperaturas DS18B20 + presión analógica)
         * ========================================================== */
        estado_sistema_t s = estado_get_snapshot();

        fmt_valor(buf_a, sizeof(buf_a), s.temp_agua_suministro, "\xDF" "C");
        fmt_valor(buf_b, sizeof(buf_b), s.presion_suministro,   " PSI");
        print_centrado(0, "AGUA");
        lcd_print_row(1, "SUM: %s %s", buf_a, buf_b);

        fmt_valor(buf_a, sizeof(buf_a), s.temp_agua_retorno, "\xDF" "C");
        fmt_valor(buf_b, sizeof(buf_b), s.presion_retorno,   " PSI");
        lcd_print_row(2, "RET: %s %s", buf_a, buf_b);
        lcd_print_row(3, " ");

        vTaskDelay(pdMS_TO_TICKS(5000));

        /* ==========================================================
         * PANTALLA 2: AIRE (temperaturas DS18B20 + humedad DHT)
         * ========================================================== */
        s = estado_get_snapshot();

        print_centrado(0, "AIRE");
        fmt_valor(buf_a, sizeof(buf_a), s.temp_aire_suministro, "\xDF" "C");
        lcd_print_row(1, "SUM:  %s", buf_a);
        fmt_valor(buf_b, sizeof(buf_b), s.temp_aire_retorno, "\xDF" "C");
        lcd_print_row(2, "RET:  %s", buf_b);
        fmt_valor(buf_c, sizeof(buf_c), s.humedad_ambiente, " %%");
        lcd_print_row(3, "Hum:  %s", buf_c);

        vTaskDelay(pdMS_TO_TICKS(5000));

        /* ==========================================================
         * PANTALLA 3: MOTOR (RPM real + corriente, pendiente integrar)
         * ========================================================== */
        s = estado_get_snapshot();

        print_centrado(0, "MOTOR");
        /* El RPM SI se imprime siempre como número: 0 es un valor
         * válido (motor detenido), a diferencia de los sensores
         * todavía no conectados. */
        lcd_print_row(1, "RPM:        %4.0f", s.rpm_actual);
        fmt_valor(buf_a, sizeof(buf_a), s.corriente_motor, " A");
        lcd_print_row(2, "Corriente:  %s", buf_a);
        lcd_print_row(3, " ");

        vTaskDelay(pdMS_TO_TICKS(5000));

        /* ==========================================================
         * PANTALLA 4: CONEXION (wifi/vpn/mqtt en tiempo real)
         * ========================================================== */
        s = estado_get_snapshot();

        print_centrado(0, "CONEXION");
        lcd_print_row(1, " WIFI:%-3s  VPN:%-3s ",
                      s.wifi_conectado ? "ON" : "--",
                      s.vpn_conectado  ? "ON" : "--");
        lcd_print_row(2, " MQTT:%-3s  UMA:ON  ",
                      s.mqtt_conectado ? "ON" : "--");
        lcd_print_row(3, " ");

        vTaskDelay(pdMS_TO_TICKS(5000));

        /* ==========================================================
         * PANTALLA 5: CREDITOS
         * ========================================================== */
        time_t t = estado_get_hora_arranque();
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        strftime(fecha, sizeof(fecha), "%d/%m %H:%M", &tm_info);

        print_centrado(0, "UMA - N" "\xDF" "3");
        print_centrado(1, "Gustavo Medina");
        print_centrado(2, "SERCLISA");
        lcd_print_row(3, "Inicio: %s", fecha);

        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}