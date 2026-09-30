#include "dht_manager.h"
#include "estado_sistema.h"
#include "esp_log.h"

#include "alarmas_manager.h"

static const char *TAG = "DHT_MGR";

static bool s_falla_dht = false;

void dht_manager_leer(void)
{
    float temperatura = 0.0f;
    float humedad     = 0.0f;

    esp_err_t err = dht_read_float_data(DHT_SENSOR_TYPE, DHT_GPIO_PIN,
                                         &humedad, &temperatura);
	EVALUAR_FALLA(err != ESP_OK, s_falla_dht, TAG, "Fallo al leer DHT11. Reintentando en siguiente ciclo...", "Comunicacion con el sensor DHT22 restablecida");
                  
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Humedad: %.1f %%  Temperatura: %.1f C", humedad, temperatura);
        estado_set_humedad_ambiente(humedad);
    } 
}