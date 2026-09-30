#include "mqtt_manager.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include <math.h>

#include "mqtt_client.h"
#include "estado_sistema.h"
#include "littlefs_manager.h"
#include "control_manager.h"
#include "alarmas_manager.h"


static const char *TAG_MQTT = "MQTT_APP";

static esp_mqtt_client_handle_t mqtt_client = NULL;

// Variable global para el callback
static mqtt_comando_callback_t on_comando = NULL;



static void fmt_mqtt(char *out, size_t n, float v)
{
    if (isnan(v)) {
        snprintf(out, n, "--.-");
    } else {
        snprintf(out, n, "%.1f", v);
    }
}

/* Función para parseo seguro. Retorna true si es un número válido */
static bool parsear_float_seguro(const char *payload, float min, float max, float *out_val)
{
    char *endptr;
    float val = strtof(payload, &endptr);
    
    if (endptr == payload) {
        return false; /* No se encontró ningún número en el payload */
    }
    
    if (val < min) val = min;
    if (val > max) val = max;
    
    *out_val = val;
    return true;
}

void mqtt_publish_retained(const char *topic, const char *payload)
{
    if (!estado_get_mqtt() || topic == NULL || payload == NULL) return;
    
    /* El último '1' activa la retención en el broker */
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, 1);
    if (msg_id != -1) {
        estado_marcar_publish_ok();
    }
}

static void mqtt_publicar_configuracion(void)
{
    estado_sistema_t s = estado_get_snapshot();
    char val[16];

    fmt_mqtt(val, sizeof(val), s.set_point);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_SETPOINT, val);

    fmt_mqtt(val, sizeof(val), s.kp);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_KP, val);

    fmt_mqtt(val, sizeof(val), s.ki);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_KI, val);

    fmt_mqtt(val, sizeof(val), s.kd);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_KD, val);

    fmt_mqtt(val, sizeof(val), s.valvula_manual_pct);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_MANUAL_PCT, val);

    mqtt_publish_retained(TOPIC_CTRL_STATUS_MODO, s.control_activo ? "ON" : "OFF");
    mqtt_publish_retained(TOPIC_CTRL_STATUS_MODO_VALVULA, s.modo_valvula == MODO_VALVULA_AUTOMATICO ? "AUTOMATICO" : "MANUAL");

    ESP_LOGI(TAG_MQTT, "Configuracion publicada");
}

static void mqtt_publicar_snapshot(void)
{
    estado_sistema_t s = estado_get_snapshot();
    char val[16];

    // Agua
    fmt_mqtt(val, sizeof(val), s.temp_agua_suministro);
    mqtt_publish(TOPIC_AGUA_TEMP_SUM, val);

    fmt_mqtt(val, sizeof(val), s.temp_agua_retorno);
    mqtt_publish(TOPIC_AGUA_TEMP_RET, val);

    fmt_mqtt(val, sizeof(val), s.presion_suministro);
    mqtt_publish(TOPIC_AGUA_PRES_SUM, val);

    fmt_mqtt(val, sizeof(val), s.presion_retorno);
    mqtt_publish(TOPIC_AGUA_PRES_RET, val);

    // Aire
    fmt_mqtt(val, sizeof(val), s.temp_aire_suministro);
    mqtt_publish(TOPIC_AIRE_TEMP_SUM, val);

    fmt_mqtt(val, sizeof(val), s.temp_aire_retorno);
    mqtt_publish(TOPIC_AIRE_TEMP_RET, val);

    fmt_mqtt(val, sizeof(val), s.humedad_ambiente);
    mqtt_publish(TOPIC_AIRE_HUMEDAD, val);

    // Motor
    fmt_mqtt(val, sizeof(val), s.rpm_actual);
    mqtt_publish(TOPIC_MOTOR_RPM, val);

    fmt_mqtt(val, sizeof(val), s.corriente_motor);
    mqtt_publish(TOPIC_MOTOR_CORRIENTE, val);

    // Conexión
/*    mqtt_publish(TOPIC_CON_WIFI,  s.wifi_conectado  ? "ON" : "OFF");
    mqtt_publish(TOPIC_CON_VPN,   s.vpn_conectado   ? "ON" : "OFF");
    mqtt_publish(TOPIC_CON_MQTT,  s.mqtt_conectado  ? "ON" : "OFF");

    // Control
    fmt_mqtt(val, sizeof(val), s.set_point);
    mqtt_publish(TOPIC_CTRL_SETPOINT, val);

    fmt_mqtt(val, sizeof(val), s.kp);
    mqtt_publish(TOPIC_CTRL_KP, val);

    fmt_mqtt(val, sizeof(val), s.ki);
    mqtt_publish(TOPIC_CTRL_KI, val);

    fmt_mqtt(val, sizeof(val), s.kd);
    mqtt_publish(TOPIC_CTRL_KD, val);

    mqtt_publish(TOPIC_CTRL_MODO, s.control_activo ? "ON" : "OFF");
    mqtt_publish(TOPIC_CTRL_MODO_VALVULA,
                 s.modo_valvula == MODO_VALVULA_AUTOMATICO ? "AUTOMATICO" : "MANUAL");
*/
    /*
    fmt_mqtt(val, sizeof(val), s.valvula_manual_pct);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_MANUAL_PCT, val);
	*/
	
    fmt_mqtt(val, sizeof(val), s.valvula_comando_pct);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_SALIDA, val);

    fmt_mqtt(val, sizeof(val), s.feedback_valvula_pct);
    mqtt_publish_retained(TOPIC_CTRL_STATUS_APERTURA, val);

  //  mqtt_publish(TOPIC_CTRL_FALLA, s.valvula_en_falla ? "FALLA" : "OK");
}

static void mqtt_guardar_snapshot_offline(void)
{
    estado_sistema_t s = estado_get_snapshot();
    char linea[200];
    time_t ahora = time(NULL);

    snprintf(linea, sizeof(linea),
             "{\"ts\":%lld,\"tas\":%.1f,\"tar\":%.1f,\"pas\":%.1f,\"par\":%.1f,"
             "\"tis\":%.1f,\"tir\":%.1f,\"hum\":%.1f,\"rpm\":%.0f,\"amp\":%.1f}",
             (long long)ahora,
             s.temp_agua_suministro, s.temp_agua_retorno,
             s.presion_suministro,   s.presion_retorno,
             s.temp_aire_suministro, s.temp_aire_retorno,
             s.humedad_ambiente, s.rpm_actual, s.corriente_motor);

    littlefs_ops_append_linea(RUTA_DATOS, linea);
    ESP_LOGI(TAG_MQTT, "Sin conexion: snapshot guardado en %s", RUTA_DATOS);
}

static void mqtt_enviar_linea_pendiente(const char *linea)
{
    mqtt_publish(TOPIC_STATUS, linea);
}

void mqtt_publish_task(void *pvParameters)
{
    bool conectado_anterior = false;

    while (1) {
        bool conectado = estado_get_mqtt();
		
        if (conectado) {
            if (!conectado_anterior) {
                ESP_LOGI(TAG_MQTT, "Conexion recuperada. Enviando pendientes...");
                
                littlefs_ops_enviar_pendientes(RUTA_DATOS, CURSOR_DATOS_KEY, estado_get_wifi(), true,
                                               mqtt_enviar_linea_pendiente);
                
                alarmas_manager_enviar_pendientes();
            }
            
            mqtt_publicar_snapshot();
            conectado_anterior = true;
            vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_INTERVAL_MS));
        } else {
            mqtt_guardar_snapshot_offline();
            conectado_anterior = false;
            vTaskDelay(pdMS_TO_TICKS(OFFLINE_SAVE_INTERVAL_MS));
        }
    }
}

void mqtt_publish(const char *topic, const char *payload)
{
    if (!estado_get_mqtt()) {
        ESP_LOGW(TAG_MQTT, "Sin conexión. Paquete descartado. Topic: [%s]", topic);
        return;
    }

    if (topic == NULL || payload == NULL) {
        ESP_LOGE(TAG_MQTT, "Topic o payload nulo, abortando publicación.");
        return;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, 0);

    if (msg_id != -1) {
		estado_marcar_publish_ok();
        ESP_LOGI(TAG_MQTT, 
        "\n\n*************************************************"
        "\n Publicado en [%s] -> %s\n"
        "*************************************************\n\n", topic, payload);
        
    } else {
        ESP_LOGE(TAG_MQTT, "Fallo al encolar mensaje en [%s]", topic);
    }
}


// Función que convierte el string recibido a un enum
static mqtt_comando_t parsear_comando(const char *msg, int len)
{
    if (len == strlen("ENCENDER")  && strncmp(msg, "ENCENDER",  len) == 0) return CMD_ENCENDER;
	if (len == strlen("APAGAR")    && strncmp(msg, "APAGAR",    len) == 0) return CMD_APAGAR;
	if (len == strlen("REINICIAR") && strncmp(msg, "REINICIAR", len) == 0) return CMD_REINICIAR;
    return CMD_DESCONOCIDO;
}

// Registrar el callback desde fuera
void mqtt_app_registrar_callback(mqtt_comando_callback_t cb)
{
    on_comando = cb;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "¡Autenticado y Conectado al Broker MQTT!");
            estado_set_mqtt(true); 
            esp_mqtt_client_subscribe(client, TOPIC_COMANDOS, 0);
            esp_mqtt_client_subscribe(client, TOPIC_CTRL_SET_WILDCARD, 0);
            mqtt_publicar_configuracion(); // Configuración cargada del NVS 
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG_MQTT, "Desconectado del Broker MQTT. Intentando reconectar...");
            estado_set_mqtt(false);
            break;

        case MQTT_EVENT_DATA: {
            /* El payload NO tiene '\0' al final, hay que copiarlo primero.
             * Se hace una sola vez y se reutiliza para cualquier topic. */
            char payload[64] = {0};
            int len = event->data_len < (int)sizeof(payload) - 1
                       ? event->data_len
                       : (int)sizeof(payload) - 1;
            memcpy(payload, event->data, len);

            bool cambio_config = false; /* Bandera para disparar el ACK retenido */

            if (event->topic_len == strlen(TOPIC_COMANDOS) &&
                strncmp(event->topic, TOPIC_COMANDOS, event->topic_len) == 0)
            {
                ESP_LOGI(TAG_MQTT, "Comando recibido: [%s]", payload);
                mqtt_comando_t cmd = parsear_comando(payload, len);
                estado_set_ultimo_comando(cmd);
                if (on_comando != NULL) {
                    on_comando(cmd);
                }
            }
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_SETPOINT) &&
                     strncmp(event->topic, TOPIC_CTRL_SET_SETPOINT, event->topic_len) == 0)
            {
                float nuevo_sp;
                if (parsear_float_seguro(payload, LIMITE_SP_MIN, LIMITE_SP_MAX, &nuevo_sp)) {
                    if (nuevo_sp != estado_get_setpoint()) {
                        control_manager_set_setpoint(nuevo_sp);
                        cambio_config = true;
                        ESP_LOGI(TAG_MQTT, "Setpoint actualizado a: %.1f", nuevo_sp);
                    }
                } else {
                    ESP_LOGW(TAG_MQTT, "Payload Setpoint invalido: %s", payload);
                }
            }
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_KP) &&
                     strncmp(event->topic, TOPIC_CTRL_SET_KP, event->topic_len) == 0)
            {
                float nuevo_kp;
                if (parsear_float_seguro(payload, LIMITE_KP_MIN, LIMITE_KP_MAX, &nuevo_kp)) {
                    if (nuevo_kp != estado_get_kp()) {
                        control_manager_set_kp(nuevo_kp);
                        cambio_config = true;
                        ESP_LOGI(TAG_MQTT, "Kp actualizado a: %.2f", nuevo_kp);
                    }
                } else {
                    ESP_LOGW(TAG_MQTT, "Payload Kp invalido: %s", payload);
                }
            }
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_KI) &&
                     strncmp(event->topic, TOPIC_CTRL_SET_KI, event->topic_len) == 0)
            {
                float nuevo_ki;
                if (parsear_float_seguro(payload, LIMITE_KI_MIN, LIMITE_KI_MAX, &nuevo_ki)) {
                    if (nuevo_ki != estado_get_ki()) {
                        control_manager_set_ki(nuevo_ki);
                        cambio_config = true;
                        ESP_LOGI(TAG_MQTT, "Ki actualizado a: %.2f", nuevo_ki);
                    }
                } else {
                    ESP_LOGW(TAG_MQTT, "Payload Ki invalido: %s", payload);
                }
            }
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_KD) &&
                     strncmp(event->topic, TOPIC_CTRL_SET_KD, event->topic_len) == 0)
            {
                float nuevo_kd;
                if (parsear_float_seguro(payload, LIMITE_KD_MIN, LIMITE_KD_MAX, &nuevo_kd)) {
                    if (nuevo_kd != estado_get_kd()) {
                        control_manager_set_kd(nuevo_kd);
                        cambio_config = true;
                        ESP_LOGI(TAG_MQTT, "Kd actualizado a: %.2f", nuevo_kd);
                    }
                } else {
                    ESP_LOGW(TAG_MQTT, "Payload Kd invalido: %s", payload);
                }
            }
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_MANUAL_PCT) &&
                     strncmp(event->topic, TOPIC_CTRL_SET_MANUAL_PCT, event->topic_len) == 0)
            {
                float nuevo_pct;
                if (parsear_float_seguro(payload, LIMITE_MANUAL_MIN, LIMITE_MANUAL_MAX, &nuevo_pct)) {
                    if (nuevo_pct != estado_get_valvula_manual_pct()) {
                        control_manager_set_manual_pct(nuevo_pct);
                        cambio_config = true;
                        ESP_LOGI(TAG_MQTT, "Apertura manual actualizada a: %.1f%%", nuevo_pct);
                    }
                } else {
                    ESP_LOGW(TAG_MQTT, "Payload Manual Pct invalido: %s", payload);
                }
            }
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_MODO_VALVULA) &&
         		strncmp(event->topic, TOPIC_CTRL_SET_MODO_VALVULA, event->topic_len) == 0)
			{
			    modo_valvula_t nuevo_modo;
			
			    if (strcmp(payload, "AUTOMATICO") == 0) {
			        nuevo_modo = MODO_VALVULA_AUTOMATICO;
			    } else if (strcmp(payload, "MANUAL") == 0) {
			        nuevo_modo = MODO_VALVULA_MANUAL;
			    } else {
			        ESP_LOGW(TAG_MQTT, "Comando invalido en [%s]: '%s'. Use unicamente 'MANUAL' o 'AUTOMATICO'.",
			                 TOPIC_CTRL_SET_MODO_VALVULA, payload);
			        return;
			    }
			
			    if (nuevo_modo != estado_get_modo_valvula()) {
			        control_manager_set_modo(nuevo_modo);
			        cambio_config = true;
			        ESP_LOGI(TAG_MQTT, "Modo de valvula actualizado a: %s", payload);
			    }
			}
            else if (event->topic_len == strlen(TOPIC_CTRL_SET_MODO) &&
        		 strncmp(event->topic, TOPIC_CTRL_SET_MODO, event->topic_len) == 0)
			{
			    bool nuevo_estado;
			
			    if (strcmp(payload, "ON") == 0) {
			        nuevo_estado = true;
			    } else if (strcmp(payload, "OFF") == 0) {
			        nuevo_estado = false;
			    } else {
			        ESP_LOGW(TAG_MQTT, "Comando invalido en [%s]: '%s'. Use unicamente 'ON' u 'OFF'.", 
			                 TOPIC_CTRL_SET_MODO, payload);
			        return; /* O un 'break;' dependiendo del flujo del switch/if */
			    }
			
			    if (nuevo_estado != estado_get_control_activo()) {
			        control_manager_set_control_activo(nuevo_estado);
			        cambio_config = true;
			        ESP_LOGI(TAG_MQTT, "Control %s", nuevo_estado ? "ACTIVADO" : "DESACTIVADO");
			    }
			}else if (event->topic_len == strlen(TOPIC_CTRL_SET_ACTUALIZAR) &&
		         strncmp(event->topic, TOPIC_CTRL_SET_ACTUALIZAR, event->topic_len) == 0)
			{
		    	ESP_LOGI(TAG_MQTT, "Actualizacion solicitada. Publicando configuracion actual...");
		    	mqtt_publicar_configuracion();
			}

            /* Si hubo un cambio real en memoria validado (anti-eco), publica todo 
             * con Retain como confirmación (ACK) hacia la red */
            if (cambio_config) {
                mqtt_publicar_configuracion();
            }
            break;
        	}
        

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG_MQTT, "Ocurrio un error critico en el cliente MQTT");
            break;

        default:
            break;
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                  = BROKER_URI,
        .credentials.username                = BROKER_USER,
        .credentials.authentication.password = BROKER_PASS,
        .network.disable_auto_reconnect      = false,
        .network.reconnect_timeout_ms        = 10000,
        .network.timeout_ms                  = 20000,
        .session.keepalive                   = 60,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);

    esp_mqtt_client_start(mqtt_client);
}

/**
 * Detiene y destruye el cliente MQTT existente. Necesario cuando la VPN
 * se reinicia desde cero (vpn_manager destruye y recrea su ml_handle):
 * el cliente viejo quedaría atado a una interfaz de red que ya no
 * existe, y esp_mqtt_client no puede migrar solo a la interfaz nueva.
 * Sin esto, mqtt_ya_iniciado en true evitaria para siempre que se
 * vuelva a llamar mqtt_app_start() tras una reconexion de VPN.
 */
void mqtt_app_stop(void)
{
    if (mqtt_client != NULL) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        ESP_LOGW(TAG_MQTT, "Cliente MQTT detenido y destruido (la VPN se esta reiniciando)");
    }
    /* No asumir que el evento MQTT_EVENT_DISCONNECTED se disparo al
     * detener el cliente manualmente — se fija explicito para que el
     * resto del sistema (LCD, MQTT publish) vea el estado correcto. */
    estado_set_mqtt(false);
}

void mqtt_publish_alarma(const char *topic, const char *payload)
{
    if (!estado_get_mqtt()) {
        ESP_LOGW(TAG_MQTT, "Sin conexion. Alarma descartada (manejado por LittleFS). Topic: [%s]", topic);
        return;
    }
    if (topic == NULL || payload == NULL) return;

    /* El penúltimo parámetro '1' activa el QoS 1 (entrega garantizada) */
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);

    if (msg_id != -1) {
        estado_marcar_publish_ok();
        ESP_LOGW(TAG_MQTT, "ALARMA ENCOLADA (QoS 1, ID: %d) -> [%s]: %s", msg_id, topic, payload);
    } else {
        ESP_LOGE(TAG_MQTT, "Fallo al encolar alarma QoS 1 en [%s]", topic);
    }
}


void mqtt_publish_dht_data(float temperature, float humidity)
{
    char payload[100];
    snprintf(payload, sizeof(payload),
             "{\"temperatura\": %.1f, \"humedad\": %.1f}",
             temperature, humidity);

    mqtt_publish(TOPIC_SENSOR_DHT, payload);  // reutiliza la función genérica
}

void mqtt_publish_data(int cuenta)
{
    char payload[100];
    snprintf(payload, sizeof(payload), "Numero: %d", cuenta);
    mqtt_publish(TOPIC_PRUEBA, payload);
}