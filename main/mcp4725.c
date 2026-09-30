#include "mcp4725.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "mcp4725";

esp_err_t mcp4725_init(i2c_master_dev_handle_t dev_handle, float vref_volts, mcp4725_t *out_dev)
{
    if (dev_handle == NULL || out_dev == NULL || vref_volts <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    out_dev->dev = dev_handle;
    out_dev->vref_volts = vref_volts;
    ESP_LOGI(TAG, "MCP4725 listo (Vref = %.2f V, resolución 12 bits)", vref_volts);
    return ESP_OK;
}

esp_err_t mcp4725_set_raw(mcp4725_t *dev, uint16_t code12bit)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (code12bit > 4095) {
        code12bit = 4095;
    }

    /* Comando "Fast Mode" del MCP4725 (2 bytes, no requiere puntero de
     * registro): C2 C1 = 00, PD1 PD0 = 00 (normal, sin power-down),
     * seguido de los 12 bits del valor D11..D0. */
    uint8_t buf[2] = {
        (uint8_t)((code12bit >> 8) & 0x0F), // bits [7:6]=00 (fast mode), [5:4]=00 (power normal), [3:0]=D11..D8
        (uint8_t)(code12bit & 0xFF),        // D7..D0
    };

    esp_err_t err = i2c_master_transmit(dev->dev, buf, sizeof(buf), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error escribiendo DAC: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t mcp4725_set_voltage(mcp4725_t *dev, float voltage)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (voltage < 0.0f) {
        voltage = 0.0f;
    }
    if (voltage > dev->vref_volts) {
        voltage = dev->vref_volts;
    }

    float code_f = (voltage / dev->vref_volts) * 4095.0f;
    uint16_t code = (uint16_t)roundf(code_f);

    return mcp4725_set_raw(dev, code);
}
