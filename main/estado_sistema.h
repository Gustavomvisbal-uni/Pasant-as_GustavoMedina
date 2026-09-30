#ifndef ESTADO_SISTEMA_H
#define ESTADO_SISTEMA_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "mqtt_manager.h"   /* para el enum mqtt_comando_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Modo de operación de la válvula: manual (el operador fija el %
 * directamente) o automático (el PID calcula el % según el setpoint).
 */
typedef enum {
    MODO_VALVULA_MANUAL = 0,
    MODO_VALVULA_AUTOMATICO = 1,
} modo_valvula_t;

/**
 * Snapshot de todo el estado del sistema en un instante dado.
 * Se obtiene con estado_get_snapshot() cuando necesitas varios
 * campos a la vez de forma consistente (ej. refrescar el LCD).
 */
typedef struct {
    /* --- Conectividad --- */
    bool           wifi_conectado;
    bool           vpn_conectado;
    bool           mqtt_conectado;
    bool           littlefs_montado;
    char           vpn_ip[20];
    mqtt_comando_t ultimo_comando;

    /* --- Circuito de agua ---
     * Temperaturas: DS18B20. Presiones: sensores analógicos,
     * pendientes de integrar (por ahora quedan en 0.0). */
    float temp_agua_suministro;
    float temp_agua_retorno;
    float presion_suministro;   /* PSI */
    float presion_retorno;      /* PSI */

    /* --- Circuito de aire ---
     * Temperaturas: DS18B20. Humedad: DHT11/DHT22 (ambiente). */
    float temp_aire_suministro;
    float temp_aire_retorno;
    float humedad_ambiente;

    /* --- Motor --- */
    float rpm_actual;
    float corriente_motor;      /* Amperios, medido con SCT-013-030 (RMS) */

    /* --- Válvula --- */
    float feedback_valvula_pct; /* % de apertura real, medido (0-10V -> 0-100%).
                                    Usado por el PID para verificar que la
                                    válvula sigue la consigna dada. */

    /* --- Diagnóstico --- */
    int64_t ultimo_publish_ok_us;
    time_t  hora_arranque;      /* fijar tras sincronizar SNTP; 0 = sin sincronizar */
    
    /* --- Control (PID) --- */
    float set_point;
    float kp;
    float ki;
    float kd;
    bool  control_activo;

    /* --- Control de la válvula (modo y estado de actuación) --- */
    modo_valvula_t modo_valvula;
    float valvula_manual_pct;   /* % pedido cuando el modo es MANUAL */
    float valvula_comando_pct;  /* % que control_manager esta pidiendo AHORA,
                                    ya con limites aplicados (informativo) */
    bool  valvula_en_falla;     /* true si el feedback no coincide con lo
                                    comandado despues del tiempo de recorrido */
    
} estado_sistema_t;

 
    
/**
 * Crea el mutex interno y deja el estado en sus valores por defecto.
 * IMPORTANTE: llamar UNA sola vez, al principio de app_main(),
 * antes de wifi_init_sta(), mqtt_app_registrar_callback(), etc.
 */
void estado_sistema_init(void);

/* --- WiFi --- */
void estado_set_wifi(bool conectado);
bool estado_get_wifi(void);

/* --- VPN --- */
void estado_set_vpn(bool conectado);
bool estado_get_vpn(void);
void estado_set_vpn_ip(const char *ip);
void estado_get_vpn_ip(char *out, size_t out_len);

/* --- MQTT --- */
void estado_set_mqtt(bool conectado);
bool estado_get_mqtt(void);

/* --- LittleFS --- */
void estado_set_littlefs(bool montado);
bool estado_get_littlefs(void);

/* --- Último comando MQTT recibido --- */
void estado_set_ultimo_comando(mqtt_comando_t cmd);
mqtt_comando_t estado_get_ultimo_comando(void);

/* --- Agua --- */
void estado_set_temp_agua_suministro(float v);
float estado_get_temp_agua_suministro(void);
void estado_set_temp_agua_retorno(float v);
float estado_get_temp_agua_retorno(void);
void estado_set_presion_suministro(float v);
float estado_get_presion_suministro(void);
void estado_set_presion_retorno(float v);
float estado_get_presion_retorno(void);

/* --- Aire --- */
void estado_set_temp_aire_suministro(float v);
float estado_get_temp_aire_suministro(void);
void estado_set_temp_aire_retorno(float v);
float estado_get_temp_aire_retorno(void);
void estado_set_humedad_ambiente(float v);
float estado_get_humedad_ambiente(void);

/* --- Motor --- */
void estado_set_rpm(float rpm);
float estado_get_rpm(void);
void estado_set_corriente_motor(float amperios);
float estado_get_corriente_motor(void);

/* --- Válvula --- */
void estado_set_feedback_valvula(float pct);
float estado_get_feedback_valvula(void);

/* --- Publish / hora de arranque --- */
void estado_marcar_publish_ok(void);
int64_t estado_get_ultimo_publish_ok_us(void);
void estado_set_hora_arranque(time_t t);
time_t estado_get_hora_arranque(void);

/* --- Control --- */
void estado_set_setpoint(float v);
float estado_get_setpoint(void);
void estado_set_kp(float v);
float estado_get_kp(void);
void estado_set_ki(float v);
float estado_get_ki(void);
void estado_set_kd(float v);
float estado_get_kd(void);
void estado_set_control_activo(bool v);
bool estado_get_control_activo(void);

/* --- Control de la válvula --- */
void estado_set_modo_valvula(modo_valvula_t modo);
modo_valvula_t estado_get_modo_valvula(void);
void estado_set_valvula_manual_pct(float pct);
float estado_get_valvula_manual_pct(void);
void estado_set_valvula_comando_pct(float pct);
float estado_get_valvula_comando_pct(void);
void estado_set_valvula_en_falla(bool en_falla);
bool estado_get_valvula_en_falla(void);


estado_sistema_t estado_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif // ESTADO_SISTEMA_H