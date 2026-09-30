/**
 * @file control_manager.h
 * @brief Control de la válvula del serpentín: modo manual o automático
 * (PID), como una tarea FreeRTOS independiente del bucle principal.
 *
 * Variable controlada en modo automático: temp_aire_suministro (la
 * práctica convencional en unidades manejadoras de aire — control
 * directo sobre el aire de descarga del serpentín, no sobre el
 * retorno, por la menor cantidad de tiempo muerto entre mover la
 * válvula y ver el efecto).
 *
 * Persistencia: setpoint, Kp, Ki, Kd, modo y % manual se guardan en
 * NVS y sobreviven a reinicios.
 */
#pragma once

#include "esp_err.h"
#include "mcp4725.h"
#include "estado_sistema.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Periodo del ciclo de control — independiente del bucle de sensores */
#define CONTROL_PERIODO_MS         2000

/* Límites de seguridad de la salida: nunca pedir cierre ni apertura
 * total, sin importar si viene del PID o del modo manual */
#define CONTROL_VALVULA_MIN_PCT    3.0f
#define CONTROL_VALVULA_MAX_PCT    95.0f

/* Ganancia del amplificador LM358 (R1=10k, Rf=20k) que lleva los
 * 0-3.3V del DAC a 0-10V hacia el actuador */
#define CONTROL_GANANCIA_LM358     3.0f

/* Tiempo de recorrido completo del ML7420A3055 (datasheet Honeywell) */
#define CONTROL_VALVULA_RECORRIDO_MS    60000
/* Margen extra antes de considerar que ya debería haber llegado */
#define CONTROL_VALVULA_MARGEN_MS       10000
/* Tolerancia entre lo comandado y el feedback real antes de marcar
 * valvula_en_falla (da margen por tolerancia de resistencias y del
 * propio actuador) */
#define CONTROL_VALVULA_TOLERANCIA_PCT  8.0f

/**
 * @brief Carga la configuración persistida (setpoint, Kp, Ki, Kd, modo,
 * % manual) desde NVS hacia estado_sistema, SIN tocar ningún hardware.
 *
 * Se puede llamar de forma independiente del resto del módulo — por
 * ejemplo desde el portal de configuración AP (config_manager), que
 * nunca inicializa el DAC y por lo tanto no puede llamar a
 * control_manager_init() completo.
 */
void control_manager_cargar_configuracion(void);

/**
 * @brief Inicializa el módulo completo: carga la configuración (ver
 * control_manager_cargar_configuracion), guarda el handle del DAC, y
 * lanza la tarea de control en su propio hilo de FreeRTOS.
 *
 * Llamar una sola vez, después de que el DAC ya esté inicializado
 * (ver init_analogico() en main.c).
 */
void control_manager_init(mcp4725_t *dac_handle);

/* --- Setters: actualizan estado_sistema Y persisten en NVS de
 * inmediato. Pensados para llamarse desde mqtt_manager (topics
 * entrantes) o desde config_manager (formulario del portal AP). --- */
void control_manager_set_modo(modo_valvula_t modo);
void control_manager_set_manual_pct(float pct);
void control_manager_set_setpoint(float valor);
void control_manager_set_kp(float valor);
void control_manager_set_ki(float valor);
void control_manager_set_kd(float valor);
void control_manager_set_control_activo(bool activo);

#ifdef __cplusplus
}
#endif
