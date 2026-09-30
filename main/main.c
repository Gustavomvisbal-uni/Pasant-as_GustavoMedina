#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"  

#include "wifi_manager.h"     
#include "mqtt_manager.h"     
#include "led_indicator.h"
#include "lcd_manager.h"
#include "ds18b20_manager.h"  
#include "littlefs_manager.h" 
#include "estado_sistema.h"
#include "dht_manager.h"
#include "hall_rpm_manager.h"
#include "i2c_manager.h"
#include "mcp4725.h"
#include "ads1115.h"
#include "analog_manager.h"
#include "control_manager.h"
#include "config_manager.h"

#define RELE_GPIO_PIN 7

static const char *TAG = "APP_MAIN";

static mcp4725_t s_dac = {0};
static ads1115_t s_adc = {0};

static void leer_sensores(void)
{
    dht_manager_leer();
	hall_rpm_leer();
}

static void analog_task(void *pvParameters)
{
    while (1) {
        analog_manager_leer_todos();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void ds18b20_task(void *pvParameters)
{
    while (1) {
        ds18b20_manager_leer_todos();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void init_analogico(void)
{
    i2c_master_dev_handle_t dac_handle = NULL;
    i2c_master_dev_handle_t adc_handle = NULL;

    // Agregar MCP4725 al bus con su velocidad
    esp_err_t err = i2c_manager_add_device(MCP4725_I2C_ADDR, 400000, &dac_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCP4725 no encontrado: %s", esp_err_to_name(err));
        return;
    }
    mcp4725_init(dac_handle, DAC_VREF_VOLTS, &s_dac);

    // Agregar ADS1115 al bus con su velocidad
    err = i2c_manager_add_device(ADS1115_I2C_ADDR, 400000, &adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1115 no encontrado: %s", esp_err_to_name(err));
        return;
    }
    ads1115_init(adc_handle, ADC_GAIN, &s_adc);
    analog_manager_init(&s_adc);
    control_manager_init(&s_dac);

    ESP_LOGI(TAG, "DAC y ADC inicializados correctamente");
}

static void manejar_comando(mqtt_comando_t cmd)
{
    switch (cmd) {
        case CMD_ENCENDER:
            ESP_LOGI(TAG, "Encendiendo dispositivo...");
            lcd_show_comando("encender");
            gpio_set_level(RELE_GPIO_PIN, 1);
            break;
        case CMD_APAGAR:
            ESP_LOGI(TAG, "Apagando dispositivo...");
            lcd_show_comando("apagar");
            gpio_set_level(RELE_GPIO_PIN, 0);
            led_indicator_turn_off();
            break;
        case CMD_REINICIAR:
            ESP_LOGI(TAG, "Reiniciando dispositivo...");
            lcd_show_comando("reiniciar");
            break;
        case CMD_DESCONOCIDO:
        default:
            ESP_LOGW(TAG, "Comando no reconocido.");
            lcd_show_comando("desconocido");
            break;
    }
}

static void iniciar_nvs_flash(void)
{
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}


static void iniciar_sistema(void)
{
    
    estado_sistema_init();   // PRIMERO: mutex antes que cualquier otro módulo
    i2c_manager_init();
    init_analogico();
    littlefs_manager_init(); 
    
    
    led_indicator_init();
    lcd_init();
    lcd_show_titulo("ESP32-S3 UMA");
    lcd_show_estado("Iniciando...");

    // Sensores físicos: independientes de la red
    ds18b20_manager_init();    
    hall_rpm_manager_init();   

    // Red: Dispara todo lo asíncrono (SNTP → VPN → MQTT)
    mqtt_app_registrar_callback(manejar_comando);
    config_manager_iniciar_vigilancia_boton();
    wifi_init_sta();
}



void app_main(void)
{	
	iniciar_nvs_flash();
	
	verificar_y_arrancar_modo_config();
	
	// Inicia todos los sistemas en el orden debido
    iniciar_sistema();

    ESP_LOGI(TAG, "Inicializando periféricos ...");
    
    /****************************************************************
    *	Inicio de todas la tareas con su respectiva prioridad		*
    *****************************************************************/
    
    xTaskCreatePinnedToCore(lcd_bucle, "lcd_bucle", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(mqtt_publish_task, "mqtt_publish", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(ds18b20_task, "ds18b20_task", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(analog_task, "analog_task", 4096, NULL, 4, NULL, 1);
    
    int cuenta = 0; // Prueba
    
    while (1) {
		// Lee los sensores cada 5 Seg
		leer_sensores();
		ESP_LOGI(TAG, "Contador: %d", cuenta);// Variable de Prueba	
        mqtt_publish_data(cuenta++); // Por prueba también
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
}