#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdint.h>

// Configuración del Broker (Apunta a la IP de tu nodo en la malla VPN o red local)
#define BROKER_URI  "xxxx" 
#define BROKER_USER "xxxx"
#define BROKER_PASS "xxxx"

// --- DEFINICIÓN CENTRALIZADA DE TOPICS ---
// Jerarquía recomendada: ubicacion/equipo/tipo/funcion
//#define TOPIC_COMANDOS      "Comandos"
#define TOPIC_SENSOR_DHT    "Sensor_DHT22"
#define TOPIC_PRUEBA "Numero"


// Identificador de la unidad — cambiar por unidad
#define UMA_ID  "3"

// Prefijo base
#define TOPIC_BASE  "UMA/" UMA_ID "/"

// Agua
#define TOPIC_AGUA_TEMP_SUM   TOPIC_BASE "Agua/temp_suministro"
#define TOPIC_AGUA_TEMP_RET   TOPIC_BASE "Agua/temp_retorno"
#define TOPIC_AGUA_PRES_SUM   TOPIC_BASE "Agua/presion_suministro"
#define TOPIC_AGUA_PRES_RET   TOPIC_BASE "Agua/presion_retorno"

// Aire
#define TOPIC_AIRE_TEMP_SUM   TOPIC_BASE "Aire/temp_suministro"
#define TOPIC_AIRE_TEMP_RET   TOPIC_BASE "Aire/temp_retorno"
#define TOPIC_AIRE_HUMEDAD    TOPIC_BASE "Aire/humedad"

// Motor
#define TOPIC_MOTOR_RPM       TOPIC_BASE "Motor/rpm"
#define TOPIC_MOTOR_CORRIENTE TOPIC_BASE "Motor/corriente"

// Conexión
#define TOPIC_CON_WIFI        TOPIC_BASE "Conexion/wifi"
#define TOPIC_CON_VPN         TOPIC_BASE "Conexion/vpn"
#define TOPIC_CON_MQTT        TOPIC_BASE "Conexion/mqtt"


// Comandos para dar valores de control al ESP
#define TOPIC_CTRL_SET_SETPOINT       TOPIC_BASE "Control/set/setpoint"       		// Set point de temperatura para modo Automático
#define TOPIC_CTRL_SET_SALIDA         TOPIC_BASE "Control/set/salida_pct"      		// Orden matemática de apertura hacia el DAC ( 0.00 % - 100.00 % )
#define TOPIC_CTRL_SET_MANUAL_PCT     TOPIC_BASE "Control/set/manual_pct"      		// Valor de apertura manual de Válvula solo si es Manual ( 0.00 % - 100.00 % )
#define TOPIC_CTRL_SET_MODO           TOPIC_BASE "Control/set/modo"            		// Activa o desactiva el control de la válvula  ( "ON" | "OFF" )
#define TOPIC_CTRL_SET_MODO_VALVULA   TOPIC_BASE "Control/set/modo_valvula"    		// Solo si modo esta en ON ( "MANUAL" | "AUTOMATICO" )
#define TOPIC_CTRL_SET_KP             TOPIC_BASE "Control/set/kp"             		// Valor proporcional del control automático
#define TOPIC_CTRL_SET_KI             TOPIC_BASE "Control/set/ki"              		// Valor integral del control automático
#define TOPIC_CTRL_SET_KD             TOPIC_BASE "Control/set/kd"              		// Valor derivativo del control automático (Por defecto 0 por control PI)
#define TOPIC_CTRL_SET_ACTUALIZAR     TOPIC_BASE "Control/set/Actualizar"      		// Comando para enviar actualización
#define TOPIC_CTRL_SET_WILDCARD       TOPIC_BASE "Control/set/#"

// Control - Status (ESP32 → Dashboard, retain)
#define TOPIC_CTRL_STATUS_SETPOINT     TOPIC_BASE "Control/status/setpoint"    		// Set point de temperatura para modo Automático
#define TOPIC_CTRL_STATUS_SALIDA       TOPIC_BASE "Control/status/salida_pct"  		// Orden matemática de apertura hacia el DAC ( 0.00 % - 100.00 % )
#define TOPIC_CTRL_STATUS_APERTURA     TOPIC_BASE "Control/status/apertura_pct"		// Feedback real de la válvula ( 0.00 % - 100.00 % )
#define TOPIC_CTRL_STATUS_MODO         TOPIC_BASE "Control/status/modo"        		// Activa o desactiva el control de la válvula  ( "ON" | "OFF" )
#define TOPIC_CTRL_STATUS_MODO_VALVULA TOPIC_BASE "Control/status/modo_valvula"		// Solo si modo esta en ON ( "MANUAL" | "AUTOMATICO" )
#define TOPIC_CTRL_STATUS_MANUAL_PCT   TOPIC_BASE "Control/status/manual_pct"  		// Valor de apertura manual de Válvula solo si es Manual ( 0.00 % - 100.00 % )
#define TOPIC_CTRL_STATUS_KP           TOPIC_BASE "Control/status/kp"          		// Valor proporcional del control automático
#define TOPIC_CTRL_STATUS_KI           TOPIC_BASE "Control/status/ki"          		// Valor integral del control automático
#define TOPIC_CTRL_STATUS_KD           TOPIC_BASE "Control/status/kd"          		// Valor derivativo del control automático (Por defecto 0 por control PI)
#define TOPIC_CTRL_STATUS_FALLA        TOPIC_BASE "Control/status/valvula_falla"	// Estado de alarma del actuador ("OK" | "FALLA")


// Alarmas
#define TOPIC_ALARMAS  TOPIC_BASE "Alarmas"

// Entrada de comandos y status
#define TOPIC_COMANDOS        TOPIC_BASE "Comandos"
#define TOPIC_STATUS          TOPIC_BASE "Status"

/* Límites operativos seguros para UMA */
#define LIMITE_SP_MIN     12.0f
#define LIMITE_SP_MAX     30.0f
#define LIMITE_KP_MIN      0.0f
#define LIMITE_KP_MAX     20.0f
#define LIMITE_KI_MIN      0.0f
#define LIMITE_KI_MAX     10.0f
#define LIMITE_KD_MIN      0.0f
#define LIMITE_KD_MAX     10.0f
#define LIMITE_MANUAL_MIN  0.0f
#define LIMITE_MANUAL_MAX 100.0f

// Intervalos
#define MQTT_PUBLISH_INTERVAL_MS   10000   // 10s con conexión
#define OFFLINE_SAVE_INTERVAL_MS  300000   // 5min sin conexión Se modifica a menos para las pruebas


typedef enum {
    CMD_DESCONOCIDO = 0,
    CMD_ENCENDER,
    CMD_APAGAR,
    CMD_REINICIAR,
    
} mqtt_comando_t;

// Callback que se llamará cuando llegue un comando válido
typedef void (*mqtt_comando_callback_t)(mqtt_comando_t comando);

void mqtt_app_start(void);
void mqtt_app_stop(void);
void mqtt_publish(const char *topic, const char *payload);
void mqtt_app_registrar_callback(mqtt_comando_callback_t cb);  
void mqtt_publish_dht_data(float temperature, float humidity);
void mqtt_publish_data(int cuenta);
void mqtt_publish_task(void *pvParameters);
void mqtt_publish_alarma(const char *topic, const char *payload);

#endif // MQTT_MANAGER_H