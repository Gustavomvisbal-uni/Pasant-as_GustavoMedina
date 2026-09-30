#include "alarmas_manager.h"
#include "mqtt_manager.h"
#include "estado_sistema.h"
#include "littlefs_manager.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ALARMAS_MGR";

/* Variables de estado local para el filtro anti-spam */
static char           s_ultimo_mensaje[128] = {0};
static alerta_nivel_t s_ultimo_nivel        = (alerta_nivel_t)-1;

/* Mutex que protege s_ultimo_mensaje / s_ultimo_nivel contra acceso
 * concurrente desde distintas tareas (analog_task, ds18b20_task,
 * loop principal, etc.) */
static SemaphoreHandle_t s_alarmas_mutex = NULL;

void alarmas_manager_init(void)
{
    memset(s_ultimo_mensaje, 0, sizeof(s_ultimo_mensaje));
    s_ultimo_nivel = (alerta_nivel_t)-1;

    s_alarmas_mutex = xSemaphoreCreateMutex();
    if (s_alarmas_mutex == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el mutex de alarmas");
    }

    ESP_LOGI(TAG, "Modulo de alarmas inicializado");
}

static const char* nivel_a_str(alerta_nivel_t nivel)
{
    switch (nivel) {
        case ALERTA_NIVEL_WARN:     return "WARN";
        case ALERTA_NIVEL_ERROR:    return "ERROR";
        case ALERTA_NIVEL_RESOLVED: return "RESOLVED";
        default:                    return "INFO";
    }
}

void alertas_registrar(alerta_nivel_t nivel, const char *tag, const char *fmt, ...)
{
    char base_msg[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(base_msg, sizeof(base_msg), fmt, args);
    va_end(args);

    bool duplicado = false;

    if (s_alarmas_mutex != NULL &&
        xSemaphoreTake(s_alarmas_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

        if (nivel == s_ultimo_nivel &&
            strncmp(base_msg, s_ultimo_mensaje, sizeof(s_ultimo_mensaje)) == 0) {
            duplicado = true;
        } else {
            s_ultimo_nivel = nivel;
            strncpy(s_ultimo_mensaje, base_msg, sizeof(s_ultimo_mensaje) - 1);
            s_ultimo_mensaje[sizeof(s_ultimo_mensaje) - 1] = '\0';
        }

        xSemaphoreGive(s_alarmas_mutex);
    } else {
        ESP_LOGW(TAG, "No se pudo tomar el mutex de alarmas (timeout o no inicializado); "
                      "se continua sin filtrar duplicado");
    }

    if (duplicado) {
        return;
    }

    time_t ahora = time(NULL);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"ts\":%lld,\"lvl\":\"%s\",\"tag\":\"%s\",\"msg\":\"%s\"}",
             (long long)ahora, nivel_a_str(nivel), tag != NULL ? tag : "UNKNOWN", base_msg);

    if (estado_get_mqtt()) {
        mqtt_publish_alarma(TOPIC_ALARMAS, payload);
    } else {
        littlefs_ops_append_linea(RUTA_ALARMAS, payload);
        ESP_LOGI(TAG, "Sin MQTT. Alarma almacenada en %s", RUTA_ALARMAS);
    }
}

static void enviar_alarma_callback(const char *linea)
{
    mqtt_publish_alarma(TOPIC_ALARMAS, linea);
}

void alarmas_manager_enviar_pendientes(void)
{
    if (!estado_get_mqtt()) return;
    ESP_LOGI(TAG, "Procesando alarmas pendientes guardadas en LittleFS...");
    littlefs_ops_enviar_pendientes(RUTA_ALARMAS, CURSOR_ALARMAS_KEY, estado_get_wifi(), true, enviar_alarma_callback);
}