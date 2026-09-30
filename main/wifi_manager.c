#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "vpn_manager.h"
#include "esp_sntp.h"
#include <time.h>

#include "nvs.h"
#include "nvs_flash.h"

#include "led_indicator.h"
#include "estado_sistema.h"

static const char *TAG_WIFI = "WIFI_APP";
static int s_retry_num = 0;


#define NVS_WIFI_NAMESPACE  "wifi_cfg"

static void wifi_cargar_credenciales(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = ssid_len;
        if (nvs_get_str(h, "ssid", ssid_out, &len) != ESP_OK) {
            snprintf(ssid_out, ssid_len, "%s", WIFI_SSID);
        }
        len = pass_len;
        if (nvs_get_str(h, "pass", pass_out, &len) != ESP_OK) {
            snprintf(pass_out, pass_len, "%s", WIFI_PASS);
        }
        nvs_close(h);
    } else {
        snprintf(ssid_out, ssid_len, "%s", WIFI_SSID);
        snprintf(pass_out, pass_len, "%s", WIFI_PASS);
    }
}

void wifi_manager_guardar_credenciales(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG_WIFI, "Credenciales WiFi guardadas en NVS (aplican tras reiniciar)");
    } else {
        ESP_LOGE(TAG_WIFI, "No se pudo abrir NVS para guardar credenciales");
    }
}

static void sntp_and_vpn_task(void *pvParameters) {
    // 1. Inicializar SNTP solo si no está corriendo
    if (!esp_sntp_enabled()) {
        ESP_LOGI(TAG_WIFI, "Inicializando SNTP...");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "time.cloudflare.com");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_init();
    } else {
        ESP_LOGI(TAG_WIFI, "El servicio SNTP ya está activo.");
    }

    // 2. Siempre esperar a que el tiempo esté sincronizado
    int retry = 0;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry++ < 50) {
        ESP_LOGI(TAG_WIFI, "Esperando sincronización SNTP... (%d/50)", retry);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 3. Evaluar el resultado y arrancar la VPN
    if (retry < 50) {
        time_t now;
        time(&now);
        ESP_LOGI(TAG_WIFI, "Tiempo sincronizado: %lld", (long long)now);
        
        estado_set_hora_arranque(now); 
    } else {
        ESP_LOGW(TAG_WIFI, "SNTP timeout. El handshake de WireGuard puede fallar por falta de timestamp válido.");
    }

    
    vpn_app_start();
    
    // 4. Limpiar la tarea
    vTaskDelete(NULL);
}


static TimerHandle_t wifi_retry_timer;

static void wifi_retry_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG_WIFI, "Período de espera finalizado. Reiniciando intentos de conexión...");
    s_retry_num = 0;
    esp_wifi_connect();
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    led_indicator_set_color(200, 100, 0);
    esp_wifi_connect();
	} 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Desconectado/Error: LED en Rojo
        led_indicator_set_color(255, 0, 0);
         estado_set_wifi(false);   // Para avisar que está apagado o desconectado
        
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG_WIFI, "Reintentando conectar al WiFi (%d/%d)...", s_retry_num, MAXIMUM_RETRY);
        } else {
            ESP_LOGE(TAG_WIFI, "Fallo al conectar. Iniciando temporizador de enfriamiento...");
            xTimerStart(wifi_retry_timer, 0);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        led_indicator_set_color(0, 255, 0);
        estado_set_wifi(true); // Para avisar que está desactivado
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "IP local asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_retry_num = 0; 
        xTimerStop(wifi_retry_timer, 0);
		
        xTaskCreatePinnedToCore(sntp_and_vpn_task, "sntp_vpn", 4096, NULL, 5, NULL, 0);
    }
}


void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_retry_timer = xTimerCreate("wifi_recon_tmr", 
                                    pdMS_TO_TICKS(COOLDOWN_TIME_MS), 
                                    pdFALSE, 
                                    (void*)0, 
                                    wifi_retry_timer_callback);
    
    if (wifi_retry_timer == NULL) {
        ESP_LOGE(TAG_WIFI, "Error crítico: No se pudo crear el temporizador de reconexión.");
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    // Reemplazar el wifi_config_t actual (el que usa WIFI_SSID/WIFI_PASS directo):
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    wifi_cargar_credenciales((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid),
                              (char*)wifi_config.sta.password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Inicialización del módulo WiFi completa.");
}

