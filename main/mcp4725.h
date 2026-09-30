#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t dev;
    float vref_volts; // Tensión de referencia/alimentación real del DAC
} mcp4725_t;

/**
 * @brief Inicializa el "objeto" de manejo del MCP4725.
 *
 * @param vref_volts Tensión real que alimenta al MCP4725 (normalmente 3.3V si
 *                    se alimenta desde el ESP32-S3). Es la tensión máxima que
 *                    el DAC puede generar en su salida.
 */
esp_err_t mcp4725_init(i2c_master_dev_handle_t dev_handle, float vref_volts, mcp4725_t *out_dev);

/**
 * @brief Fija la salida analógica en el voltaje indicado (0 .. vref_volts).
 * Valores fuera de rango se recortan (clamp) al límite más cercano.
 */
esp_err_t mcp4725_set_voltage(mcp4725_t *dev, float voltage);

/**
 * @brief Fija directamente el código de 12 bits (0..4095) sin pasar por voltios.
 */
esp_err_t mcp4725_set_raw(mcp4725_t *dev, uint16_t code12bit);

#ifdef __cplusplus
}
#endif
