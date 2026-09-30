#include "i2c_manager.h"

#include "esp_log.h"

static const char *TAG = "i2c_manager";
static i2c_master_bus_handle_t s_bus_handle = NULL;

esp_err_t i2c_manager_init(void)
{
    if (s_bus_handle != NULL) {
        ESP_LOGW(TAG, "Bus I2C ya estaba inicializado");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* Pull-ups internas de respaldo.
                                                  La mayoría de breakouts ya traen
                                                  sus propias pull-ups (4.7k-10k). */
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al crear el bus I2C: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bus I2C inicializado (SDA=%d, SCL=%d, %d Hz)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_manager_get_bus(void)
{
    return s_bus_handle;
}

esp_err_t i2c_manager_add_device(uint16_t address,
                                  uint32_t scl_speed_hz,
                                  i2c_master_dev_handle_t *out_handle)
{
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "El bus I2C no está inicializado (llama primero a i2c_manager_init)");
        return ESP_ERR_INVALID_STATE;
    }
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, out_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al añadir dispositivo 0x%02X: %s", address, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Dispositivo I2C 0x%02X añadido al bus", address);
    return ESP_OK;
}

static i2c_master_bus_handle_t s_lcd_bus_handle = NULL;

esp_err_t i2c_manager_init_lcd_bus(void)
{
    if (s_lcd_bus_handle != NULL) {
        ESP_LOGW(TAG, "Bus I2C de LCD ya estaba inicializado");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_LCD_PORT,
        .sda_io_num = I2C_LCD_SDA_IO,
        .scl_io_num = I2C_LCD_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_lcd_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al crear bus I2C de LCD: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bus I2C de LCD inicializado (SDA=%d, SCL=%d, %d Hz)",
             I2C_LCD_SDA_IO, I2C_LCD_SCL_IO, I2C_LCD_FREQ_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_manager_get_lcd_bus(void)
{
    return s_lcd_bus_handle;
}

esp_err_t i2c_manager_add_device_lcd(uint16_t address,
                                      uint32_t scl_speed_hz,
                                      i2c_master_dev_handle_t *out_handle)
{
    if (s_lcd_bus_handle == NULL) {
        ESP_LOGE(TAG, "Bus I2C de LCD no inicializado");
        return ESP_ERR_INVALID_STATE;
    }
    if (out_handle == NULL) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(s_lcd_bus_handle, &dev_cfg, out_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al añadir LCD 0x%02X: %s", address, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LCD 0x%02X añadida al bus I2C dedicado", address);
    return ESP_OK;
}
