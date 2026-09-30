#include "esp_err.h"
#include "esp_system.h"
#include "vpn_manager.h"
#include "esp_log.h"
#include "microlink.h"
#include "mqtt_manager.h"
#include "led_indicator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_manager.h"
#include "estado_sistema.h"
#include "esp_wifi.h" // Agregado para poder reiniciar el WiFi

static const char *TAG_VPN = "VPN_APP";

static microlink_t *ml_handle = NULL;
static bool mqtt_ya_iniciado  = false;
static TaskHandle_t broker_probe_task_handle = NULL;
static int s_vpn_restarts = 0; 

#define BROKER_PROBE_MAX_RETRIES  10 

/* Marcador pasado como parametro de la tarea para distinguir el
 * arranque normal (sondear el broker) del arranque tras un error ya
 * confirmado del tunel (saltar directo a la recuperacion, sin gastar
 * hasta 5 minutos en un sondeo que ya sabemos que va a fallar). */
static const int SALTAR_SONDEO_FLAG = 1;

// Declaración anticipada para poder llamarla desde la tarea de limpieza
void vpn_app_start(void);

/*
-  * Usa microlink_tcp_connect — esto fuerza el handshake WG con el peer
-  * si no hay sesión activa, y espera hasta 30s a que complete.
-  * Es la forma correcta de despertar el túnel con el broker.
-  * En caso de no conectarse reinicia el VPN y si esto no lo acomoda 
-  * Reinicia WIFI
-  */
static void broker_probe_task(void *pvParameters)
{
    int intentos = 0;
    bool alcanzado = false;
    bool saltar_sondeo = (pvParameters != NULL);

    if (saltar_sondeo) {
        ESP_LOGW(TAG_VPN, "Tunel ya reportado en ERROR: saltando el sondeo, recuperacion inmediata.");
    } else {
        ESP_LOGI(TAG_VPN, "Iniciando sondeo TCP al broker via tunel WG (%s:1883)...", BROKER_VPN_IP);

        while (intentos < BROKER_PROBE_MAX_RETRIES) {
            intentos++;
            ESP_LOGI(TAG_VPN, "Intento %d/%d — conectando via microlink_tcp_connect...",
                     intentos, BROKER_PROBE_MAX_RETRIES);

            // Validamos que ml_handle no sea nulo por seguridad
            if (ml_handle == NULL) break; 

            uint32_t broker_ip = microlink_parse_ip(BROKER_VPN_IP);
            microlink_tcp_socket_t *sock = microlink_tcp_connect(ml_handle, broker_ip, 1883, 30000);

            if (sock != NULL) {
                ESP_LOGI(TAG_VPN, "Broker alcanzable. Sesion WG activa.");
                microlink_tcp_close(sock);
                
                s_vpn_restarts = 0; // Reinicia el contador global de fallos
                alcanzado = true;
                
                // Solo iniciamos MQTT si es la primera vez
                if (!mqtt_ya_iniciado) {
                    mqtt_app_start();
                    mqtt_ya_iniciado = true;
                }
                break;
            }
            ESP_LOGW(TAG_VPN, "Broker no responde (%d/%d). Reintentando...", intentos, BROKER_PROBE_MAX_RETRIES);
        }
    }

    // --- LÓGICA DE RECUPERACIÓN ESCALONADA (Y SEGURA) ---
    if (!alcanzado) {
        s_vpn_restarts++;
        
        if (s_vpn_restarts >= 3) {
            ESP_LOGE(TAG_VPN, "Falla critica en la red. Reiniciando ciclo WiFi...");
            led_indicator_set_color(255, 165, 0); 
            s_vpn_restarts = 0; 
            
            // El MQTT quedaria atado a una VPN que esta por desaparecer
            // por completo — se detiene aca, ANTES de tumbar el WiFi,
            // para no dejarlo huerfano.
            if (mqtt_ya_iniciado) {
                mqtt_app_stop();
                mqtt_ya_iniciado = false;
            }
            
            // Limpieza segura antes de tumbar la red
            if (ml_handle != NULL) {
                microlink_stop(ml_handle);
                microlink_destroy(ml_handle);
                ml_handle = NULL;
            }
            
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_wifi_disconnect(); 
            
        } else {
            ESP_LOGW(TAG_VPN, "Reiniciando cliente VPN desde cero (Intento %d/3)...", s_vpn_restarts);
            led_indicator_set_color(128, 0, 128); 
            estado_set_vpn(false);
            
            // Mismo motivo que en el escalon critico: el cliente MQTT
            // quedaria atado a un ml_handle que esta por destruirse.
            if (mqtt_ya_iniciado) {
                mqtt_app_stop();
                mqtt_ya_iniciado = false;
            }
            
            // Limpieza de memoria crítica y segura
            if (ml_handle != NULL) {
                microlink_stop(ml_handle);
                microlink_destroy(ml_handle);
                ml_handle = NULL; 
            }
            
            vTaskDelay(pdMS_TO_TICKS(5000)); // Enfriamiento

            // Se libera el handle de esta tarea ANTES de arrancar la
            // VPN nueva: si el callback de conexion llegara a disparar
            // casi de inmediato, encuentra el camino libre para lanzar
            // su propio broker_probe_task sin bloquearse por esta
            // instancia, que ya terminó su trabajo.
            broker_probe_task_handle = NULL;
            vpn_app_start(); // Arranca una instancia fresca
            vTaskDelete(NULL);
        }
    }

    broker_probe_task_handle = NULL;
    vTaskDelete(NULL); // Destrucción segura de la tarea
}

static void microlink_state_callback(microlink_t *ml, microlink_state_t state, void *user_data)
{
    switch (state) {
        case ML_STATE_CONNECTED: {
            char ip_buf[20];
            uint32_t vpn_ip = microlink_get_vpn_ip(ml);
            microlink_ip_to_str(vpn_ip, ip_buf);
            ESP_LOGI(TAG_VPN, "VPN IP asignada: %s", ip_buf);
            estado_set_vpn(true);   // Para tener la variable que está activado   
            estado_set_vpn_ip(ip_buf);   // Para tener la IP como dato global
            ESP_LOGI(TAG_VPN, "Tailscale conectado. Verificando ruta al broker via WG...");

            // Lanza la tarea de prueba tanto en el primer arranque como en reconexiones profundas
            if (broker_probe_task_handle == NULL) {
                xTaskCreatePinnedToCore(broker_probe_task, "broker_probe", 8192, NULL, 5, &broker_probe_task_handle, 0);
            }
            break;
        }

        case ML_STATE_RECONNECTING:
            ESP_LOGW(TAG_VPN, "VPN inestable. Reconectando...");
            led_indicator_set_color(255, 165, 0);
            estado_set_vpn(false); // Para variable de VPN
            break;

        case ML_STATE_ERROR:
            ESP_LOGE(TAG_VPN, "Error critico en el tunel VPN.");
            led_indicator_set_color(255, 0, 0);
            estado_set_vpn(false); 
            
            // El tunel ya se reporto roto — no tiene sentido gastar
            // hasta 5 minutos sondeando el broker sobre un ml_handle
            // que ya sabemos que fallo. Se pasa el marcador para que
            // la tarea salte directo a la recuperacion.
            if (broker_probe_task_handle == NULL) {
                xTaskCreatePinnedToCore(broker_probe_task, "broker_probe", 8192,
                            (void *)&SALTAR_SONDEO_FLAG, 5, &broker_probe_task_handle, 0);
            }
            break;

        default:
            break;
    }
}

void vpn_app_start(void)
{
    ESP_LOGI(TAG_VPN, "Iniciando MicroLink (Tailscale)...");

    microlink_config_t vpn_cfg = {
        .auth_key         = TAILSCALE_AUTH_KEY,
        .priority_peer_ip = microlink_parse_ip(BROKER_VPN_IP), // Garantiza slot WG para el broker
    };

    ml_handle = microlink_init(&vpn_cfg);

    if (ml_handle != NULL) {
        microlink_set_state_callback(ml_handle, microlink_state_callback, NULL);
        ESP_LOGI(TAG_VPN, "MicroLink inicializado. Negociando con servidor...");
        microlink_start(ml_handle);
    } else {
        ESP_LOGE(TAG_VPN, "Fallo critico al inicializar MicroLink. Posible falta de RAM.");
    }
}