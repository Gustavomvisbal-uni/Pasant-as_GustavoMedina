#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registros/bits para modo continuo */
#define ADS1115_VENTANA_RMS_MS   300

/* Canal de entrada single-ended (respecto a GND) */
typedef enum {
    ADS1115_CH0 = 0,
    ADS1115_CH1 = 1,
    ADS1115_CH2 = 2,
    ADS1115_CH3 = 3,
} ads1115_channel_t;

/* Ganancia del amplificador programable (PGA) -> define el fondo de escala (FSR) */
typedef enum {
    ADS1115_GAIN_6144MV = 0, // FSR = ±6.144 V
    ADS1115_GAIN_4096MV = 1, // FSR = ±4.096 V
    ADS1115_GAIN_2048MV = 2, // FSR = ±2.048 V (default de fábrica)
    ADS1115_GAIN_1024MV = 3, // FSR = ±1.024 V
    ADS1115_GAIN_0512MV = 4, // FSR = ±0.512 V
    ADS1115_GAIN_0256MV = 5, // FSR = ±0.256 V
} ads1115_gain_t;

typedef struct {
    i2c_master_dev_handle_t dev;
    ads1115_gain_t gain;
} ads1115_t;

/**
 * @brief Inicializa el "objeto" de manejo del ADS1115.
 *
 * No escribe nada en el chip todavía; solo guarda el handle I2C y la ganancia
 * que se usará en cada conversión.
 */
esp_err_t ads1115_init(i2c_master_dev_handle_t dev_handle,
                        ads1115_gain_t gain,
                        ads1115_t *out_dev);

/**
 * @brief Lanza una conversión single-shot en el canal indicado y devuelve el
 * valor crudo de 16 bits con signo.
 */
esp_err_t ads1115_read_raw(ads1115_t *dev, ads1115_channel_t channel, int16_t *raw_out);

/**
 * @brief Igual que ads1115_read_raw pero ya convertido a voltios, según la
 * ganancia configurada.
 */
esp_err_t ads1115_read_voltage(ads1115_t *dev, ads1115_channel_t channel, float *voltage_out);



/**
 * @brief Mide la corriente AC RMS en el canal indicado usando muestreo
 * continuo del ADS1115 durante ADS1115_VENTANA_RMS_MS milisegundos.
 *
 * Asume que la señal está centrada en voltaje_bias (típicamente Vcc/2
 * mediante un divisor resistivo externo) y calcula el RMS de la
 * componente AC (oscilación alrededor de ese punto medio).
 *
 * @param dev              Dispositivo ADS1115 ya inicializado.
 * @param channel          Canal a leer.
 * @param voltaje_bias     Voltaje del punto medio del divisor (ej. 1.65f).
 * @param voltaje_rms_out  Voltaje RMS de la señal AC medida.
 */
esp_err_t ads1115_read_rms(ads1115_t *dev, ads1115_channel_t channel,
                            float voltaje_bias, float *voltaje_rms_out);
                            

#ifdef __cplusplus
}
#endif
