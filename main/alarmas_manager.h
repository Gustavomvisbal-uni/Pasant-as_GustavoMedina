#ifndef ALARMAS_MANAGER_H
#define ALARMAS_MANAGER_H

#include "esp_err.h"
#include "esp_log.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALERTA_NIVEL_WARN = 0,
    ALERTA_NIVEL_ERROR,
    ALERTA_NIVEL_RESOLVED
} alerta_nivel_t;

/**
 * @brief Inicializa el módulo de alarmas y limpia el buffer de repetición.
 */
void alarmas_manager_init(void);

/**
 * @brief Registra, filtra y enruta una alarma hacia MQTT o LittleFS.
 */
void alertas_registrar(alerta_nivel_t nivel, const char *tag, const char *fmt, ...);

/**
 * @brief Lee las alarmas acumuladas en LittleFS y las publica en MQTT al restablecer conexión.
 */
void alarmas_manager_enviar_pendientes(void);

#define EVALUAR_FALLA(condicion_error, bandera, tag, msg_err, msg_ok) \
    do { \
        if (condicion_error) { \
            if (!(bandera)) { \
                ESP_LOGE(tag, "%s", msg_err); \
                alertas_registrar(ALERTA_NIVEL_ERROR, tag, "%s", msg_err); \
                (bandera) = true; \
            } \
        } else { \
            if (bandera) { \
                ESP_LOGI(tag, "%s", msg_ok); \
                alertas_registrar(ALERTA_NIVEL_RESOLVED, tag, "%s", msg_ok); \
                (bandera) = false; \
            } \
        } \
    } while(0)
#ifdef __cplusplus
}
#endif

#endif // ALARMAS_MANAGER_H