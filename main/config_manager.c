#include "config_manager.h"
#include "wifi_manager.h"
#include "estado_sistema.h"
#include "control_manager.h"
#include "led_indicator.h"
#include "lcd_manager.h"
#include "i2c_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "CONFIG_MGR";

static httpd_handle_t s_server = NULL;
static esp_timer_handle_t s_timeout_timer = NULL;
static EventGroupHandle_t s_salir_event = NULL;
#define BIT_SALIR (1 << 0)

/* ------------------------------------------------------------------
 * Manejo del flag de arranque en NVS
 * ------------------------------------------------------------------ */
bool config_manager_debe_arrancar_en_modo_config(void)
{
    nvs_handle_t h;
    uint8_t modo = BOOT_MODO_NORMAL;

    if (nvs_open(NVS_BOOT_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_BOOT_KEY, &modo);
        nvs_close(h);
    }
    return modo == BOOT_MODO_CONFIG;
}

static void guardar_flag_arranque(uint8_t modo)
{
    nvs_handle_t h;
    if (nvs_open(NVS_BOOT_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_BOOT_KEY, modo);
        nvs_commit(h);
        nvs_close(h);
    }
}

void verificar_y_arrancar_modo_config(void){
	
	// Bifurcación ANTES de tocar cualquier otro subsistema
    if (config_manager_debe_arrancar_en_modo_config()) {
		i2c_manager_init();
		lcd_init();
		led_indicator_init();
        config_manager_ejecutar_modo_config();
        // Nunca retorna: config_manager_ejecutar_modo_config() termina con esp_restart()
    }
}


static void boton_salir_config_task(void *pvParameters)
{
    int64_t tiempo_inicio = 0;
    bool contando = false;
    bool ya_disparado = false;

    while (1) {
        bool presionado = (gpio_get_level(CONFIG_BOTON_GPIO) == 0);

        if (presionado) {
            if (!contando) {
                contando = true;
                tiempo_inicio = esp_timer_get_time();
            } else if (!ya_disparado) {
                int64_t transcurrido_ms = (esp_timer_get_time() - tiempo_inicio) / 1000;
                if (transcurrido_ms >= CONFIG_BOTON_HOLD_MS) {
                    ya_disparado = true;
                    ESP_LOGW(TAG, "Boton mantenido 3s en modo config: saliendo...");
                    xEventGroupSetBits(s_salir_event, BIT_SALIR);
                }
            }
        } else {
            contando = false;
            ya_disparado = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ------------------------------------------------------------------
 * Decodificación mínima de URL (form-urlencoded)
 * ------------------------------------------------------------------ */
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if (*src == '%' && (a = src[1]) && (b = src[2]) &&
            isxdigit((unsigned char)a) && isxdigit((unsigned char)b)) {
            a = tolower((unsigned char)a); b = tolower((unsigned char)b);
            a = a >= 'a' ? a - 'a' + 10 : a - '0';
            b = b >= 'a' ? b - 'a' + 10 : b - '0';
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ------------------------------------------------------------------
 * Página principal (GET /)
 * ------------------------------------------------------------------ */
static esp_err_t get_root_handler(httpd_req_t *req)
{
    /* Valores actuales (ya cargados desde NVS por
     * control_manager_cargar_configuracion(), llamado antes de este
     * punto) para precargar el formulario de control. */
    float sp  = estado_get_setpoint();
    float kp  = estado_get_kp();
    float ki  = estado_get_ki();
    float kd  = estado_get_kd();
    float man = estado_get_valvula_manual_pct();
    bool  automatico = (estado_get_modo_valvula() == MODO_VALVULA_AUTOMATICO);
    
    // NUEVO: Cargar el estado activo/inactivo actual
    bool activo = estado_get_control_activo();

    char html[3072];
    int n = snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>UMA - Configuracion</title>"
        "<style>"
        "body{font-family:'Segoe UI',sans-serif;background:#1a2634;color:#e8eef3;"
        "display:flex;flex-direction:column;align-items:center;min-height:100vh;margin:0;padding:20px;box-sizing:border-box;gap:20px}"
        ".card{background:#24384a;border-radius:16px;padding:32px 28px;max-width:360px;width:100%%;"
        "box-shadow:0 8px 24px rgba(0,0,0,0.3)}"
        "h1{font-size:1.3em;margin:0 0 4px;color:#5ec8f2}"
        "p.sub{color:#8fa3b3;font-size:0.85em;margin:0 0 24px}"
        "label{display:block;font-size:0.85em;color:#a8bccb;margin:16px 0 6px}"
        "input,select{width:100%%;padding:12px;border-radius:8px;border:1px solid #3a5268;"
        "background:#1a2634;color:#e8eef3;font-size:1em;box-sizing:border-box}"
        "input:focus,select:focus{outline:none;border-color:#5ec8f2}"
        "button{width:100%%;margin-top:24px;padding:14px;border:none;border-radius:8px;"
        "background:#5ec8f2;color:#0b1620;font-size:1em;font-weight:600;cursor:pointer}"
        "button:active{background:#3fa8d4}"
        ".salir button{background:#e08a3f}"
        "</style></head>"
        "<body>"

        "<div class='card'>"
        "<h1>UMA &middot; N&deg;3</h1>"
        "<p class='sub'>Configuracion de red WiFi</p>"
        "<form method='POST' action='/guardar_red'>"
        "<label>Nombre de red (SSID)</label>"
        "<input name='ssid' autocomplete='off' required>"
        "<label>Contrase&ntilde;a</label>"
        "<input name='pass' type='text' autocomplete='off'>"
        "<button type='submit'>Guardar red</button>"
        "</form>"
        "</div>"

        "<div class='card'>"
        "<h1>Control de la valvula</h1>"
        "<p class='sub'>Setpoint y ganancias del PID</p>"
        "<form method='POST' action='/guardar_control'>"
        
        "<label>Estado general</label>"
        "<select name='ctrl_activo'>"
        "<option value='ON'%s>Encendido (ON)</option>"
        "<option value='OFF'%s>Apagado (OFF)</option>"
        "</select>"

        "<label>Setpoint (&deg;C aire suministro)</label>"
        "<input name='setpoint' type='number' step='0.1' value='%.1f'>"
        "<label>Kp</label>"
        "<input name='kp' type='number' step='0.01' value='%.2f'>"
        "<label>Ki</label>"
        "<input name='ki' type='number' step='0.01' value='%.2f'>"
        "<label>Kd</label>"
        "<input name='kd' type='number' step='0.01' value='%.2f'>"
        "<label>Modo de la valvula</label>"
        "<select name='modo'>"
        "<option value='MANUAL'%s>Manual</option>"
        "<option value='AUTOMATICO'%s>Automatico (PID)</option>"
        "</select>"
        "<label>Apertura manual (%%)</label>"
        "<input name='manual_pct' type='number' step='1' min='0' max='100' value='%.0f'>"
        "<button type='submit'>Guardar control</button>"
        "</form>"
        "</div>"

        "<div class='card salir'>"
        "<p class='sub'>Cuando termines de ajustar ambos formularios:</p>"
        "<form method='POST' action='/salir'>"
        "<button type='submit'>Aplicar cambios y reiniciar</button>"
        "</form>"
        "</div>"

        "</body></html>",
        activo ? " selected" : "",
        activo ? "" : " selected",
        sp, kp, ki, kd,
        automatico ? "" : " selected",
        automatico ? " selected" : "",
        man);

    if (n < 0 || n >= (int)sizeof(html)) {
        ESP_LOGE(TAG, "HTML de configuracion truncado (necesita %d bytes)", n);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/* ------------------------------------------------------------------
 * Guardar red
 * ------------------------------------------------------------------ */
static esp_err_t post_red_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    char ssid_raw[64] = {0}, pass_raw[64] = {0};
    char ssid[64] = {0}, pass[64] = {0};

    httpd_query_key_value(buf, "ssid", ssid_raw, sizeof(ssid_raw));
    httpd_query_key_value(buf, "pass", pass_raw, sizeof(pass_raw));
    url_decode(ssid, ssid_raw);
    url_decode(pass, pass_raw);

    if (strlen(ssid) == 0) {
        httpd_resp_sendstr(req, "<html><body>SSID vacio, no se guardo nada. "
                                 "<a href='/'>Volver</a></body></html>");
        return ESP_OK;
    }

    wifi_manager_guardar_credenciales(ssid, pass);

    /* Ya NO dispara la salida del modo config: se guarda y se queda
     * en el portal, para poder ajustar tambien el formulario de
     * control antes de reiniciar una sola vez con el boton de abajo. */
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h3>Credenciales guardadas.</h3>"
        "<a href='/'>Volver</a></body></html>");

    return ESP_OK;
}

/* ------------------------------------------------------------------
 * Guardar configuracion de control (setpoint, Kp, Ki, Kd, modo,
 * porcentaje manual). Igual que el formulario de red: no dispara la
 * salida del modo config, eso queda a cargo del boton explicito
 * "Aplicar cambios y reiniciar".
 * ------------------------------------------------------------------ */
static esp_err_t post_control_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[len] = '\0';

    char campo[32];

    // NUEVO: Procesar el campo ctrl_activo desde el formulario
    if (httpd_query_key_value(buf, "ctrl_activo", campo, sizeof(campo)) == ESP_OK) {
        bool encender = (strcmp(campo, "ON") == 0);
        control_manager_set_control_activo(encender);
    }

    if (httpd_query_key_value(buf, "setpoint", campo, sizeof(campo)) == ESP_OK) {
        control_manager_set_setpoint(strtof(campo, NULL));
    }
    if (httpd_query_key_value(buf, "kp", campo, sizeof(campo)) == ESP_OK) {
        control_manager_set_kp(strtof(campo, NULL));
    }
    if (httpd_query_key_value(buf, "ki", campo, sizeof(campo)) == ESP_OK) {
        control_manager_set_ki(strtof(campo, NULL));
    }
    if (httpd_query_key_value(buf, "kd", campo, sizeof(campo)) == ESP_OK) {
        control_manager_set_kd(strtof(campo, NULL));
    }
    if (httpd_query_key_value(buf, "modo", campo, sizeof(campo)) == ESP_OK) {
        modo_valvula_t modo = (strcmp(campo, "AUTOMATICO") == 0)
                               ? MODO_VALVULA_AUTOMATICO : MODO_VALVULA_MANUAL;
        control_manager_set_modo(modo);
    }
    if (httpd_query_key_value(buf, "manual_pct", campo, sizeof(campo)) == ESP_OK) {
        control_manager_set_manual_pct(strtof(campo, NULL));
    }

    ESP_LOGI(TAG, "Configuracion de control guardada desde el portal AP");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h3>Configuracion de control guardada.</h3>"
        "<a href='/'>Volver</a></body></html>");

    return ESP_OK;
}
/* ------------------------------------------------------------------
 * Salir: borra el flag y libera el bucle bloqueante para reiniciar
 * ------------------------------------------------------------------ */
static esp_err_t post_salir_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h3>Reiniciando en modo normal...</h3></body></html>");

    xEventGroupSetBits(s_salir_event, BIT_SALIR);
    return ESP_OK;
}

static void timeout_callback(void *arg)
{
    ESP_LOGW(TAG, "Timeout de modo configuracion (5 min).");
    xEventGroupSetBits(s_salir_event, BIT_SALIR);
}

/* ------------------------------------------------------------------
 * Ejecuta el modo configuración COMPLETO (bloqueante).
 * Se llama en vez de iniciar_sistema() cuando el flag NVS lo indica.
 * ------------------------------------------------------------------ */
void config_manager_ejecutar_modo_config(void)
{
    ESP_LOGW(TAG, "=== ARRANCANDO EN MODO CONFIGURACION (solo AP+HTTP) ===");

    // Mínimo indispensable: NVS ya se inicializa antes de llamar esto
    estado_sistema_init();
    control_manager_cargar_configuracion();  // precarga el formulario con los valores reales guardados
    
    lcd_show_titulo("MODO AP");
	lcd_show_estado("CONEXION WIFI");
	lcd_print_row(2, "SSID: %s", AP_CONFIG_SSID);
	lcd_print_row(3, "IP: 192.168.4.1");
	led_indicator_set_color(255, 255, 255);

    // WiFi en modo AP PURO (no APSTA, no STA) — nada de VPN, nada de MQTT
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_CONFIG_SSID,
            .ssid_len = strlen(AP_CONFIG_SSID),
            .password = AP_CONFIG_PASS,
            .max_connection = AP_CONFIG_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = 1,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP activo: SSID=%s  IP=192.168.4.1", AP_CONFIG_SSID);
    
    s_salir_event = xEventGroupCreate();

    // Configurar el GPIO del botón (ya que en modo config no pasamos por
    // config_manager_iniciar_vigilancia_boton())
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BOTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    xTaskCreate(boton_salir_config_task, "boton_salir_cfg", 3072, NULL, 5, NULL);

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
	http_cfg.max_open_sockets = 4;
	http_cfg.stack_size = 8192;
	http_cfg.recv_wait_timeout = 10;
	http_cfg.send_wait_timeout = 10;
    ESP_ERROR_CHECK(httpd_start(&s_server, &http_cfg));

    static const httpd_uri_t uri_get_root  = { "/", HTTP_GET,  get_root_handler, NULL };
    static const httpd_uri_t uri_post_red  = { "/guardar_red", HTTP_POST, post_red_handler, NULL };
    static const httpd_uri_t uri_post_control = { "/guardar_control", HTTP_POST, post_control_handler, NULL };
    static const httpd_uri_t uri_post_salir= { "/salir", HTTP_POST, post_salir_handler, NULL };
    httpd_register_uri_handler(s_server, &uri_get_root);
    httpd_register_uri_handler(s_server, &uri_post_red);
    httpd_register_uri_handler(s_server, &uri_post_control);
    httpd_register_uri_handler(s_server, &uri_post_salir);

    esp_timer_create_args_t timer_args = {
        .callback = &timeout_callback,
        .name = "config_timeout",
    };
    esp_timer_create(&timer_args, &s_timeout_timer);
    esp_timer_start_once(s_timeout_timer, (uint64_t)CONFIG_AP_TIMEOUT_MS * 1000);

    ESP_LOGI(TAG, "Servidor activo. Esperando configuracion o timeout...");

    // Bloquea aquí hasta que el usuario confirme "Listo" o pase el timeout
    xEventGroupWaitBits(s_salir_event, BIT_SALIR, pdTRUE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "Saliendo de modo configuracion. Reiniciando en modo normal...");
    guardar_flag_arranque(BOOT_MODO_NORMAL);

    vTaskDelay(pdMS_TO_TICKS(500));  // da tiempo a que la respuesta HTTP salga
    esp_restart();
}

/* ------------------------------------------------------------------
 * Vigilancia del botón durante operación NORMAL — solo guarda el
 * flag y reinicia; no toca WiFi/VPN/MQTT directamente.
 * ------------------------------------------------------------------ */
static void boton_watch_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BOTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    int64_t tiempo_inicio = 0;
    bool contando = false;
    bool ya_disparado = false;

    while (1) {
        bool presionado = (gpio_get_level(CONFIG_BOTON_GPIO) == 0);

        if (presionado) {
            if (!contando) {
                contando = true;
                tiempo_inicio = esp_timer_get_time();
            } else if (!ya_disparado) {
                int64_t transcurrido_ms = (esp_timer_get_time() - tiempo_inicio) / 1000;
                if (transcurrido_ms >= CONFIG_BOTON_HOLD_MS) {
                    ya_disparado = true;
                    ESP_LOGW(TAG, "Boton mantenido 3s: reiniciando en modo configuracion...");
                    guardar_flag_arranque(BOOT_MODO_CONFIG);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    esp_restart();
                }
            }
        } else {
            contando = false;
            ya_disparado = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void config_manager_iniciar_vigilancia_boton(void)
{
    xTaskCreate(boton_watch_task, "boton_config", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "Vigilancia de boton de configuracion iniciada (GPIO%d)", CONFIG_BOTON_GPIO);
}