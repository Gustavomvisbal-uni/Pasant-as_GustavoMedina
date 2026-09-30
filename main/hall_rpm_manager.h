#ifndef HALL_RPM_MANAGER_H
#define HALL_RPM_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa el contador de pulsos por hardware (PCNT) para el sensor
 * de efecto Hall NJK-5002C, usado para medir RPM del motor.
 *
 * @return ESP_OK si se configuro correctamente
 */
esp_err_t hall_rpm_manager_init(void);

/**
 * Calcula las RPM actuales del motor, basandose en los pulsos contados
 * desde la ultima llamada a esta funcion (o desde el init, la primera vez).
 * Al llamarla, reinicia el contador para la siguiente ventana de medicion.
 *
 * @param out_rpm Puntero donde se escribe el valor de RPM calculado
 * @return ESP_OK si el calculo fue valido
 *         ESP_ERR_INVALID_STATE si se llamo demasiado rapido despues
 *         de la llamada anterior (tiempo transcurrido insuficiente)
 */
esp_err_t hall_rpm_manager_read_rpm(float *out_rpm);

void hall_rpm_leer();

#ifdef __cplusplus
}
#endif

#endif // HALL_RPM_MANAGER_H