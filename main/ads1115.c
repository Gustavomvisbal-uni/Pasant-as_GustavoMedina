#include "ads1115.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "esp_timer.h"

static const char *TAG = "ads1115";

/* Punteros de registro internos del ADS1115 */
#define ADS1115_REG_CONVERSION   0x00
#define ADS1115_REG_CONFIG       0x01

/* Fondo de escala (FSR) en voltios para cada valor de ganancia, en el mismo
 * orden que ads1115_gain_t */
static const float k_fsr_volts[] = {
    6.144f, // ADS1115_GAIN_6144MV
    4.096f, // ADS1115_GAIN_4096MV
    2.048f, // ADS1115_GAIN_2048MV
    1.024f, // ADS1115_GAIN_1024MV
    0.512f, // ADS1115_GAIN_0512MV
    0.256f, // ADS1115_GAIN_0256MV
};

esp_err_t ads1115_init(i2c_master_dev_handle_t dev_handle,
                        ads1115_gain_t gain,
                        ads1115_t *out_dev)
{
    if (dev_handle == NULL || out_dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out_dev->dev = dev_handle;
    out_dev->gain = gain;
    ESP_LOGI(TAG, "ADS1115 listo (FSR = ±%.3f V)", k_fsr_volts[gain]);
    return ESP_OK;
}

esp_err_t ads1115_read_raw(ads1115_t *dev, ads1115_channel_t channel, int16_t *raw_out)
{
    if (dev == NULL || raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t config = (1u << 15)
                     | ((uint16_t)(4 + channel) << 12)
                     | ((uint16_t)dev->gain << 9)
                     | (1u << 8)
                     | (0x4u << 5)
                     | 0x03u;

    uint8_t write_buf[3] = {
        ADS1115_REG_CONFIG,
        (uint8_t)(config >> 8),
        (uint8_t)(config & 0xFF),
    };

    /* --- Conversión de "purga" ---
     * Al cambiar el MUX a este canal, el capacitor de muestreo interno
     * del ADS1115 puede conservar carga residual del canal leído
     * anteriormente (crosstalk), agravado por la alta impedancia de
     * nuestros divisores resistivos. Se descarta a propósito. */
    esp_err_t err = i2c_master_transmit(dev->dev, write_buf, sizeof(write_buf), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error escribiendo config (purga): %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* --- Conversión real --- */
    err = i2c_master_transmit(dev->dev, write_buf, sizeof(write_buf), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error escribiendo config: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t reg = ADS1115_REG_CONVERSION;
    uint8_t rx[2] = {0};
    err = i2c_master_transmit_receive(dev->dev, &reg, 1, rx, sizeof(rx), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error leyendo conversión: %s", esp_err_to_name(err));
        return err;
    }

    *raw_out = (int16_t)((rx[0] << 8) | rx[1]);
    return ESP_OK;
}

esp_err_t ads1115_read_voltage(ads1115_t *dev, ads1115_channel_t channel, float *voltage_out)
{
    if (voltage_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t raw;
    esp_err_t err = ads1115_read_raw(dev, channel, &raw);
    if (err != ESP_OK) {
        return err;
    }

    float fsr = k_fsr_volts[dev->gain];
    *voltage_out = ((float)raw / 32768.0f) * fsr;
    return ESP_OK;
}


esp_err_t ads1115_read_rms(ads1115_t *dev, ads1115_channel_t channel,
                            float voltaje_bias, float *voltaje_rms_out)
{
    if (dev == NULL || voltaje_rms_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* --- Configurar modo CONTINUO a 860 SPS ---
     * OS[15]      = 0  -> no aplica en modo continuo
     * MUX[14:12]  = 100 + canal
     * PGA[11:9]   = ganancia configurada
     * MODE[8]     = 0  -> modo continuo (a diferencia de single-shot)
     * DR[7:5]     = 111 -> 860 SPS (la más rápida)
     * COMP_QUE[1:0] = 11 -> comparador deshabilitado
     */
    uint16_t config = ((uint16_t)(4 + channel) << 12)
                     | ((uint16_t)dev->gain << 9)
                     | (0u << 8)
                     | (0x7u << 5)
                     | 0x03u;

    uint8_t write_buf[3] = {
        ADS1115_REG_CONFIG,
        (uint8_t)(config >> 8),
        (uint8_t)(config & 0xFF),
    };

    esp_err_t err = i2c_master_transmit(dev->dev, write_buf, sizeof(write_buf), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando modo continuo: %s", esp_err_to_name(err));
        return err;
    }

    /* Descartar la primera conversión (aún puede tener el valor viejo) */
    vTaskDelay(pdMS_TO_TICKS(10));

    float fsr = k_fsr_volts[dev->gain];
    double suma_cuadrados = 0.0;
    uint32_t muestras = 0;

    int64_t inicio_us = esp_timer_get_time();
    int64_t limite_us  = inicio_us + (ADS1115_VENTANA_RMS_MS * 1000);

    uint8_t reg = ADS1115_REG_CONVERSION;
    uint8_t rx[2];

    while (esp_timer_get_time() < limite_us) {
        err = i2c_master_transmit_receive(dev->dev, &reg, 1, rx, sizeof(rx), 1000);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Fallo leyendo muestra RMS: %s", esp_err_to_name(err));
            continue;   // se salta esta muestra, sigue intentando dentro de la ventana
        }

        int16_t raw = (int16_t)((rx[0] << 8) | rx[1]);
        float voltaje = ((float)raw / 32768.0f) * fsr;

        float ac = voltaje - voltaje_bias;   // componente AC alrededor del bias
        suma_cuadrados += (double)ac * (double)ac;
        muestras++;

        /* A 860 SPS, ~1.16ms entre muestras. No forzamos delay exacto;
         * el tiempo de la transacción I2C ya consume parte de ese margen. */
    }

    /* --- Volver a modo single-shot para no interferir con otras lecturas --- */
    uint16_t config_single = (1u << 15)
                            | ((uint16_t)(4 + channel) << 12)
                            | ((uint16_t)dev->gain << 9)
                            | (1u << 8)
                            | (0x4u << 5)
                            | 0x03u;
    uint8_t restore_buf[3] = {
        ADS1115_REG_CONFIG,
        (uint8_t)(config_single >> 8),
        (uint8_t)(config_single & 0xFF),
    };
    i2c_master_transmit(dev->dev, restore_buf, sizeof(restore_buf), 1000);

    if (muestras < 10) {
        ESP_LOGW(TAG, "RMS: muy pocas muestras validas (%lu)", (unsigned long)muestras);
        *voltaje_rms_out = 0.0f;
        return ESP_ERR_TIMEOUT;
    }

    double rms = sqrt(suma_cuadrados / (double)muestras);
    *voltaje_rms_out = (float)rms;

    ESP_LOGD(TAG, "RMS: %lu muestras en %dms, Vrms=%.4f V",
              (unsigned long)muestras, ADS1115_VENTANA_RMS_MS, *voltaje_rms_out);

    return ESP_OK;
}