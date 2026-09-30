#include "hall_rpm_manager.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "estado_sistema.h"

static const char *TAG = "HALL_RPM_MGR";

#define HALL_GPIO_PIN       1   // Pin dedicado para el sensor NJK-5002C 
#define HALL_PULSES_PER_REV 1    // 1 iman montado en el eje del motor
#define HALL_COUNT_HIGH_LIMIT 10000
#define HALL_COUNT_LOW_LIMIT  -1  // No se usa (solo contamos hacia arriba), debe ser < 0

static pcnt_unit_handle_t s_pcnt_unit = NULL;
static int64_t            s_last_read_time_us = 0;

esp_err_t hall_rpm_manager_init(void)
{
    pcnt_unit_config_t unit_config = {
        .high_limit = HALL_COUNT_HIGH_LIMIT,
        .low_limit = HALL_COUNT_LOW_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&unit_config, &s_pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error creando unidad PCNT: %s", esp_err_to_name(err));
        return err;
    }

    // Filtro de glitch: ignora pulsos mas cortos de 1000ns (ruido electrico)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_config);

    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = HALL_GPIO_PIN,
        .level_gpio_num = -1, // no usamos señal de control, solo conteo de pulsos
    };
    pcnt_channel_handle_t chan = NULL; // solo se usa aqui dentro, no hace falta guardarla
    err = pcnt_new_channel(s_pcnt_unit, &chan_config, &chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error creando canal PCNT en GPIO%d: %s", HALL_GPIO_PIN, esp_err_to_name(err));
        return err;
    }
    
    gpio_pullup_en(HALL_GPIO_PIN);

    // Cuenta en el flanco de bajada (cuando el iman activa el sensor: HIGH->LOW)
    // Mantiene el valor en el flanco de subida (evita doble conteo)
    pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE);

    pcnt_unit_enable(s_pcnt_unit);
    pcnt_unit_clear_count(s_pcnt_unit);
    pcnt_unit_start(s_pcnt_unit);

    s_last_read_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Sensor Hall inicializado correctamente en GPIO%d", HALL_GPIO_PIN);
    return ESP_OK;
}

esp_err_t hall_rpm_manager_read_rpm(float *out_rpm)
{
    if (out_rpm == NULL || s_pcnt_unit == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_us = esp_timer_get_time();
    double elapsed_sec = (now_us - s_last_read_time_us) / 1000000.0;

    if (elapsed_sec <= 0.001) {
        ESP_LOGW(TAG, "Tiempo transcurrido insuficiente para calcular RPM");
        return ESP_ERR_INVALID_STATE;
    }

    int pulse_count = 0;
    pcnt_unit_get_count(s_pcnt_unit, &pulse_count);
    pcnt_unit_clear_count(s_pcnt_unit);
    s_last_read_time_us = now_us;

    double revoluciones = (double)pulse_count / HALL_PULSES_PER_REV;
    *out_rpm = (float)((revoluciones / elapsed_sec) * 60.0);
    estado_set_rpm(*out_rpm);

    return ESP_OK;
}

void hall_rpm_leer(){
	
	float rpm = 0.0f;
    esp_err_t err = hall_rpm_manager_read_rpm(&rpm);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RPM: %.1f", rpm);
        // estado_set_rpm ya se llama dentro de hall_rpm_manager_read_rpm
    } else if (err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE = se llamó demasiado rápido, no es error real
        ESP_LOGW(TAG, "RPM: lectura no válida (%s)", esp_err_to_name(err));
    }
	
}