#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

/**
 * @file lcd_manager.h
 * @brief Driver para LCD 2004A (20×4) con módulo I2C PCF8574.

 * Tres niveles de API:
 *
 *  1. PRIMITIVA — posición exacta con printf:
 *       lcd_print_at(fila, col, fmt, ...)
 *
 *  2. FILA COMPLETA — rellena/recorta a 20 chars automáticamente:
 *       lcd_print_row(fila, fmt, ...)
 *
 *  3. DEDICADA — wrappers semánticos para sensores y estado:
 *       lcd_show_dht(temp, hum)
 *       lcd_show_estado(texto)
 *       lcd_show_mqtt(conectado)
 *       lcd_show_titulo(texto)
 */

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Configuración de hardware
 * ========================================================================== */

#define LCD_I2C_ADDR        0x27
#define LCD_COLS            20
#define LCD_ROWS            4

/* ============================================================================
 * Layout de filas (modificar aquí para reorganizar la pantalla)
 *
 *   Fila 0: Título / nombre del dispositivo  →  "  ESP32-S3 UMA  "
 *   Fila 1: Estado WiFi / MQTT               →  "MQTT: conectado "
 *   Fila 2: Datos DHT11                      →  "T:23.5C  H:65.0%"
 *   Fila 3: Último comando recibido          →  "CMD: valvula_on "
 * ========================================================================== */

#define LCD_ROW_TITULO      0
#define LCD_ROW_ESTADO      1
#define LCD_ROW_SENSOR_DHT  2
#define LCD_ROW_COMANDO     3

/* ============================================================================
 * Inicialización
 * ========================================================================== */

/**
 * @brief Inicializa el bus I2C y la LCD 2004A.
 *
 * Llama una sola vez desde app_main() antes de cualquier otra función LCD.
 * Reproduce exactamente la secuencia de init del código de prueba funcional.
 */
void lcd_init(void);

/**
 * @brief Borra toda la pantalla y posiciona el cursor en (0,0).
 * Operación lenta (~5 ms) — no usar en bucles rápidos.
 */
void lcd_clear(void);

/* ============================================================================
 * API Nivel 1 — Primitiva: posición exacta
 * ========================================================================== */

/**
 * @brief Escribe texto formateado en una posición exacta (fila, columna).
 *
 * NO rellena ni recorta — escribe exactamente lo que genera el formato.
 * Si el texto supera el ancho disponible desde `col`, el cursor sale de
 * pantalla (el HD44780 no trunca, simplemente escribe en DDRAM fuera del
 * área visible).
 *
 * Ideal para actualizar un valor dentro de una fila sin tocar el resto:
 *   lcd_print_at(2, 2, "%.1f", temp);   // solo actualiza el número
 *
 * @param row  Fila destino (0–3).
 * @param col  Columna inicio (0–19).
 * @param fmt  Formato printf.
 * @param ...  Argumentos del formato.
 */
void lcd_print_at(uint8_t row, uint8_t col, const char *fmt, ...);

/* ============================================================================
 * API Nivel 2 — Fila completa con relleno/recorte automático
 * ========================================================================== */

/**
 * @brief Escribe una fila completa (20 chars), rellenando o recortando.
 *
 * - Si el texto formateado tiene menos de 20 chars → rellena con espacios
 *   (borra el contenido anterior sin parpadeo, sin lcd_clear).
 * - Si tiene más de 20 chars → recorta en el char 20 exacto.
 * - Siempre escribe exactamente LCD_COLS caracteres en pantalla.
 *
 * Uso típico en bucle principal:
 *   lcd_print_row(1, "MQTT: %s", conectado ? "OK" : "espera...");
 *
 * @param row  Fila destino (0–3).
 * @param fmt  Formato printf.
 * @param ...  Argumentos del formato.
 */
void lcd_print_row(uint8_t row, const char *fmt, ...);

/**
 * @brief Escribe texto desde una columna, recortando si excede el ancho restante.
 *
 * Similar a lcd_print_at() pero garantiza que no se escribirá fuera de los
 * límites de la fila: el texto se trunca a (LCD_COLS - col) caracteres.
 * NO rellena con espacios — solo escribe los caracteres que caben.
 *
 * Útil para actualizar un campo en una posición fija sin riesgo de overflow:
 *   lcd_print_clipped(3, 5, payload_largo);  // máximo 15 chars desde col 5
 *
 * @param row  Fila destino (0–3).
 * @param col  Columna inicio (0–19).
 * @param fmt  Formato printf.
 * @param ...  Argumentos del formato.
 */
void lcd_print_clipped(uint8_t row, uint8_t col, const char *fmt, ...);

/* ============================================================================
 * API Nivel 3 — Funciones dedicadas por sensor / estado
 * ========================================================================== */

/**
 * @brief Muestra temperatura y humedad del DHT11 en LCD_ROW_SENSOR_DHT.
 *
 * Formato fijo de 20 chars:  "T:XX.XC  H:XX.X%    "
 * Si algún valor no está disponible, usar lcd_show_sensor_error().
 *
 * @param temperatura  °C leídos del DHT11.
 * @param humedad      % HR leídos del DHT11.
 */
void lcd_show_dht(float temperatura, float humedad);

/**
 * @brief Muestra un mensaje de error de sensor en LCD_ROW_SENSOR_DHT.
 *
 * Formato: "Sensor: err (CRC)"  o el texto que se pase.
 * Rellena la fila completa para borrar datos anteriores.
 *
 * @param motivo  Texto corto del error (se recorta a 12 chars).
 */
void lcd_show_sensor_error(const char *motivo);

/**
 * @brief Muestra el estado de conexión MQTT en LCD_ROW_ESTADO.
 *
 * @param conectado  true → "MQTT: conectado "
 *                   false → "MQTT: espera... "
 */
void lcd_show_mqtt(bool conectado);

/**
 * @brief Muestra un texto de estado libre en LCD_ROW_ESTADO.
 *
 * Recorta/rellena a 20 chars automáticamente.
 * Útil para estados intermedios: "WiFi: conectando...", "VPN: handshake", etc.
 *
 * @param texto  Mensaje de estado (null-terminated).
 */
void lcd_show_estado(const char *texto);

/**
 * @brief Muestra el título del dispositivo en LCD_ROW_TITULO.
 *
 * Centra el texto en 20 chars automáticamente.
 * Llamar una sola vez en app_main() tras lcd_init().
 *
 * @param texto  Título (máximo 20 chars, se recorta si es más largo).
 */
void lcd_show_titulo(const char *texto);

/**
 * @brief Muestra el último comando MQTT recibido en LCD_ROW_COMANDO.
 *
 * Formato: "CMD: <payload>"  recortado a 20 chars totales.
 *
 * @param payload  Contenido del mensaje recibido.
 */
void lcd_show_comando(const char *payload);

/* ============================================================================
 * Control de backlight
 * ========================================================================== */

/**
 * @brief Enciende o apaga el backlight de la pantalla.
 * @param on  true = encendido, false = apagado.
 */
void lcd_set_backlight(bool on);

/* ============================================================================
 * Pantallas rotativas
 * ========================================================================== */

/**
 * @brief Va rotando entre las pantallas que se le envíe en 
 * 
 */
void lcd_bucle(void *pvParameters);
#endif /* LCD_MANAGER_H */