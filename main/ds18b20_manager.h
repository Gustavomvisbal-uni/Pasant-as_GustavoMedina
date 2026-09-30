#ifndef DS18B20_MANAGER_H
#define DS18B20_MANAGER_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DS18B20_TEMP_AGUA_ENTRADA = 0,
    DS18B20_TEMP_AGUA_RETORNO,
    DS18B20_TEMP_AIRE_ENTRADA,
    DS18B20_TEMP_AIRE_RETORNO,
    DS18B20_SENSOR_COUNT
} ds18b20_sensor_id_t;

void ds18b20_manager_leer_todos(void);

/**
 * Inicializa los 4 buses 1-Wire (uno por pin) y crea el dispositivo
 * DS18B20 unico esperado en cada uno.
 *
 * @return ESP_OK si el proceso corrio sin errores de bus.
 *         Revisa los logs para saber cuantos sensores fueron detectados.
 */
esp_err_t ds18b20_manager_init(void);

/**
 * Dispara la conversion y lee la temperatura de UN sensor especifico.
 * Esta funcion bloquea internamente ~750ms (tiempo de conversion a 12 bits),
 * ya que la libreria del sensor maneja esa espera automaticamente.
 *
 * @return ESP_OK si la lectura fue exitosa
 *         ESP_ERR_NOT_FOUND si ese sensor no fue detectado (desconectado/dañado)
 */
esp_err_t ds18b20_manager_read(ds18b20_sensor_id_t sensor_id, float *out_temp_c);

#ifdef __cplusplus
}
#endif

#endif // DS18B20_MANAGER_H