#include "ds18b20_manager.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "alarmas_manager.h"
#include "esp_log.h"
#include <math.h>
#include <stdbool.h>

#include "estado_sistema.h"

static const char *TAG = "DS18B20_MGR";

// Un pin dedicado por sensor. Para reemplazar un sensor en el futuro
// no se toca este codigo: se conecta el nuevo sensor al mismo pin
// y se vuelve a llamar ds18b20_manager_init().
// GPIO2 y GPIO7 se evitan por estar ocupados (DHT11 y rele de prueba).

static const int DS18B20_GPIO_PINS[DS18B20_SENSOR_COUNT] = {
    [DS18B20_TEMP_AGUA_ENTRADA] = 42,
    [DS18B20_TEMP_AGUA_RETORNO] = 41,
    [DS18B20_TEMP_AIRE_ENTRADA] = 40,
    [DS18B20_TEMP_AIRE_RETORNO] = 39,
    
};

static onewire_bus_handle_t    s_bus[DS18B20_SENSOR_COUNT]    = {NULL};
static ds18b20_device_handle_t s_device[DS18B20_SENSOR_COUNT] = {NULL};
static bool                    s_present[DS18B20_SENSOR_COUNT] = {false};
static bool 					s_falla_sensor[DS18B20_SENSOR_COUNT] = {false};

void ds18b20_manager_leer_todos(void)
{
    static const struct {
        ds18b20_sensor_id_t id;
        const char *nombre;
    } sensores[] = {
        { DS18B20_TEMP_AGUA_ENTRADA, "Agua suministro" },
        { DS18B20_TEMP_AGUA_RETORNO, "Agua retorno"    },
        { DS18B20_TEMP_AIRE_ENTRADA, "Aire suministro" },
        { DS18B20_TEMP_AIRE_RETORNO, "Aire retorno"    },
    };
    for (int i = 0; i < 4; i++) {
        float temp = 0.0f;
        esp_err_t err = ds18b20_manager_read(sensores[i].id, &temp);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%s: %.2f C", sensores[i].nombre, temp);
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "%s: sensor no detectado", sensores[i].nombre);
        } else {
            ESP_LOGE(TAG, "%s: error de lectura (%s)", sensores[i].nombre, esp_err_to_name(err));
        }
    }
}

static void actualizar_estado(ds18b20_sensor_id_t sensor_id, float temp)
{
    switch (sensor_id) {
        case DS18B20_TEMP_AGUA_ENTRADA:
            estado_set_temp_agua_suministro(temp);
            break;
        case DS18B20_TEMP_AGUA_RETORNO:
            estado_set_temp_agua_retorno(temp);
            break;
        case DS18B20_TEMP_AIRE_ENTRADA:
            estado_set_temp_aire_suministro(temp);
            break;
        case DS18B20_TEMP_AIRE_RETORNO:
            estado_set_temp_aire_retorno(temp);
            break;
        default:
            break;
    }
}


esp_err_t ds18b20_manager_init(void)
{
    int detected_count = 0;

    for (int i = 0; i < DS18B20_SENSOR_COUNT; i++) {
        s_present[i] = false;

        onewire_bus_config_t bus_config = {
            .bus_gpio_num = DS18B20_GPIO_PINS[i],
        };
        onewire_bus_rmt_config_t rmt_config = {
            .max_rx_bytes = 10,
        };

        if (onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus[i]) != ESP_OK) {
            ESP_LOGW(TAG, "Sensor %d (GPIO%d): error creando bus", i, DS18B20_GPIO_PINS[i]);
            continue;
        }

        ds18b20_config_t ds_cfg = {};
        esp_err_t err = ds18b20_new_device_from_bus(s_bus[i], &ds_cfg, &s_device[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Sensor %d (GPIO%d): NO DETECTADO (%s)",
                      i, DS18B20_GPIO_PINS[i], esp_err_to_name(err));
            continue;
        }

        s_present[i] = true;
        detected_count++;
        ESP_LOGI(TAG, "Sensor %d (GPIO%d) detectado correctamente", i, DS18B20_GPIO_PINS[i]);
    }

    ESP_LOGI(TAG, "Inicializacion completa: %d/%d sensores detectados", detected_count, DS18B20_SENSOR_COUNT);
    return ESP_OK;
}

esp_err_t ds18b20_manager_read(ds18b20_sensor_id_t sensor_id, float *out_temp_c)
{
    if (sensor_id >= DS18B20_SENSOR_COUNT || out_temp_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_present[sensor_id]) {
        *out_temp_c = NAN;
        actualizar_estado(sensor_id, NAN);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ds18b20_trigger_temperature_conversion(s_device[sensor_id]);
    if (err == ESP_OK) {
        err = ds18b20_get_temperature(s_device[sensor_id], out_temp_c);
    }

    char msg_err[64];
    char msg_ok[64];
    snprintf(msg_err, sizeof(msg_err), "Falla critica en lectura de DS18B20 (ID: %d)", sensor_id);
    snprintf(msg_ok, sizeof(msg_ok), "Sensor DS18B20 restablecido (ID: %d)", sensor_id);

    EVALUAR_FALLA(err != ESP_OK, s_falla_sensor[sensor_id], TAG, msg_err, msg_ok);

    if (err != ESP_OK) {
        *out_temp_c = NAN;
        actualizar_estado(sensor_id, NAN);
        return err;
    }

    actualizar_estado(sensor_id, *out_temp_c);   
    return ESP_OK;
}