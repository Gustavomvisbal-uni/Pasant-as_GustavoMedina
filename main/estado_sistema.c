#include "estado_sistema.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ESTADO_SISTEMA";

static estado_sistema_t s_estado = {0};
static SemaphoreHandle_t s_mutex = NULL;

/* Timeout finito en vez de portMAX_DELAY: si algo se queda pegado
 * con el mutex tomado, preferimos loguear y devolver un valor por
 * defecto en vez de bloquear para siempre a otra tarea (LCD, MQTT). */
#define ESTADO_MUTEX_TIMEOUT_MS 200

static bool lock(void)
{
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "estado_sistema usado antes de estado_sistema_init()");
        return false;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(ESTADO_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout esperando el mutex de estado");
        return false;
    }
    return true;
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

void estado_sistema_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "No se pudo crear el mutex de estado_sistema");
            return;
        }
    }
    memset(&s_estado, 0, sizeof(s_estado));
    s_estado.ultimo_comando = CMD_DESCONOCIDO;
    s_estado.hora_arranque  = 0; /* se fija cuando SNTP sincronice */
}

/* --- WiFi --- */
void estado_set_wifi(bool conectado)
{
    if (!lock()) return;
    s_estado.wifi_conectado = conectado;
    unlock();
}
bool estado_get_wifi(void)
{
    bool v = false;
    if (lock()) { v = s_estado.wifi_conectado; unlock(); }
    return v;
}

/* --- VPN --- */
void estado_set_vpn(bool conectado)
{
    if (!lock()) return;
    s_estado.vpn_conectado = conectado;
    unlock();
}
bool estado_get_vpn(void)
{
    bool v = false;
    if (lock()) { v = s_estado.vpn_conectado; unlock(); }
    return v;
}
void estado_set_vpn_ip(const char *ip)
{
    if (!ip || !lock()) return;
    snprintf(s_estado.vpn_ip, sizeof(s_estado.vpn_ip), "%s", ip);
    unlock();
}
void estado_get_vpn_ip(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (!lock()) { out[0] = '\0'; return; }
    snprintf(out, out_len, "%s", s_estado.vpn_ip);
    unlock();
}

/* --- MQTT --- */
void estado_set_mqtt(bool conectado)
{
    if (!lock()) return;
    s_estado.mqtt_conectado = conectado;
    unlock();
}
bool estado_get_mqtt(void)
{
    bool v = false;
    if (lock()) { v = s_estado.mqtt_conectado; unlock(); }
    return v;
}

/* --- LittleFS --- */
void estado_set_littlefs(bool montado)
{
    if (!lock()) return;
    s_estado.littlefs_montado = montado;
    unlock();
}
bool estado_get_littlefs(void)
{
    bool v = false;
    if (lock()) { v = s_estado.littlefs_montado; unlock(); }
    return v;
}

/* --- Último comando --- */
void estado_set_ultimo_comando(mqtt_comando_t cmd)
{
    if (!lock()) return;
    s_estado.ultimo_comando = cmd;
    unlock();
}
mqtt_comando_t estado_get_ultimo_comando(void)
{
    mqtt_comando_t v = CMD_DESCONOCIDO;
    if (lock()) { v = s_estado.ultimo_comando; unlock(); }
    return v;
}

/* --- Agua --- */
void estado_set_temp_agua_suministro(float v)
{
    if (!lock()) return;
    s_estado.temp_agua_suministro = v;
    unlock();
}
float estado_get_temp_agua_suministro(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.temp_agua_suministro; unlock(); }
    return v;
}
void estado_set_temp_agua_retorno(float v)
{
    if (!lock()) return;
    s_estado.temp_agua_retorno = v;
    unlock();
}
float estado_get_temp_agua_retorno(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.temp_agua_retorno; unlock(); }
    return v;
}
void estado_set_presion_suministro(float v)
{
    if (!lock()) return;
    s_estado.presion_suministro = v;
    unlock();
}
float estado_get_presion_suministro(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.presion_suministro; unlock(); }
    return v;
}
void estado_set_presion_retorno(float v)
{
    if (!lock()) return;
    s_estado.presion_retorno = v;
    unlock();
}
float estado_get_presion_retorno(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.presion_retorno; unlock(); }
    return v;
}

/* --- Aire --- */
void estado_set_temp_aire_suministro(float v)
{
    if (!lock()) return;
    s_estado.temp_aire_suministro = v;
    unlock();
}
float estado_get_temp_aire_suministro(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.temp_aire_suministro; unlock(); }
    return v;
}
void estado_set_temp_aire_retorno(float v)
{
    if (!lock()) return;
    s_estado.temp_aire_retorno = v;
    unlock();
}
float estado_get_temp_aire_retorno(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.temp_aire_retorno; unlock(); }
    return v;
}
void estado_set_humedad_ambiente(float v)
{
    if (!lock()) return;
    s_estado.humedad_ambiente = v;
    unlock();
}
float estado_get_humedad_ambiente(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.humedad_ambiente; unlock(); }
    return v;
}

/* --- Motor --- */
void estado_set_rpm(float rpm)
{
    if (!lock()) return;
    s_estado.rpm_actual = rpm;
    unlock();
}
float estado_get_rpm(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.rpm_actual; unlock(); }
    return v;
}
void estado_set_corriente_motor(float amperios)
{
    if (!lock()) return;
    s_estado.corriente_motor = amperios;
    unlock();
}
float estado_get_corriente_motor(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.corriente_motor; unlock(); }
    return v;
}

/* --- Válvula --- */
void estado_set_feedback_valvula(float pct)
{
    if (!lock()) return;
    s_estado.feedback_valvula_pct = pct;
    unlock();
}
float estado_get_feedback_valvula(void)
{
    float v = 0.0f;
    if (lock()) { v = s_estado.feedback_valvula_pct; unlock(); }
    return v;
}

/* --- Publish timestamp / hora de arranque --- */
void estado_marcar_publish_ok(void)
{
    if (!lock()) return;
    s_estado.ultimo_publish_ok_us = esp_timer_get_time();
    unlock();
}
int64_t estado_get_ultimo_publish_ok_us(void)
{
    int64_t v = 0;
    if (lock()) { v = s_estado.ultimo_publish_ok_us; unlock(); }
    return v;
}
void estado_set_hora_arranque(time_t t)
{
    if (!lock()) return;
    s_estado.hora_arranque = t;
    unlock();
}
time_t estado_get_hora_arranque(void)
{
    time_t v = 0;
    if (lock()) { v = s_estado.hora_arranque; unlock(); }
    return v;
}

estado_sistema_t estado_get_snapshot(void)
{
    estado_sistema_t copia = {0};
    if (lock()) { copia = s_estado; unlock(); }
    return copia;
}

/* --- Control --- */
void estado_set_setpoint(float v) { if (!lock()) return; s_estado.set_point = v; unlock(); }
float estado_get_setpoint(void) { float v = 18.0f; if (lock()) { v = s_estado.set_point; unlock(); } return v; }
void estado_set_kp(float v) { if (!lock()) return; s_estado.kp = v; unlock(); }
float estado_get_kp(void) { float v = 1.0f; if (lock()) { v = s_estado.kp; unlock(); } return v; }
void estado_set_ki(float v) { if (!lock()) return; s_estado.ki = v; unlock(); }
float estado_get_ki(void) { float v = 0.1f; if (lock()) { v = s_estado.ki; unlock(); } return v; }
void estado_set_kd(float v) { if (!lock()) return; s_estado.kd = v; unlock(); }
float estado_get_kd(void) { float v = 0.0f; if (lock()) { v = s_estado.kd; unlock(); } return v; }
void estado_set_control_activo(bool v) { if (!lock()) return; s_estado.control_activo = v; unlock(); }
bool estado_get_control_activo(void) { bool v = false; if (lock()) { v = s_estado.control_activo; unlock(); } return v; }

/* --- Control de la válvula --- */
void estado_set_modo_valvula(modo_valvula_t modo) { if (!lock()) return; s_estado.modo_valvula = modo; unlock(); }
modo_valvula_t estado_get_modo_valvula(void) { modo_valvula_t v = MODO_VALVULA_MANUAL; if (lock()) { v = s_estado.modo_valvula; unlock(); } return v; }
void estado_set_valvula_manual_pct(float pct) { if (!lock()) return; s_estado.valvula_manual_pct = pct; unlock(); }
float estado_get_valvula_manual_pct(void) { float v = 0.0f; if (lock()) { v = s_estado.valvula_manual_pct; unlock(); } return v; }
void estado_set_valvula_comando_pct(float pct) { if (!lock()) return; s_estado.valvula_comando_pct = pct; unlock(); }
float estado_get_valvula_comando_pct(void) { float v = 0.0f; if (lock()) { v = s_estado.valvula_comando_pct; unlock(); } return v; }
void estado_set_valvula_en_falla(bool en_falla) { if (!lock()) return; s_estado.valvula_en_falla = en_falla; unlock(); }
bool estado_get_valvula_en_falla(void) { bool v = false; if (lock()) { v = s_estado.valvula_en_falla; unlock(); } return v; }