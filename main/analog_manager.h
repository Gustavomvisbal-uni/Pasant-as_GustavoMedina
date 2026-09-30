/**
 * @file analog_manager.h
 * @brief Lecturas analógicas de proceso: presiones (4-20mA), feedback de
 * válvula (0-10V) y corriente del motor (SCT-013-030), todas vía ADS1115.
 * Asignación de canales del ADS1115 (todos single-ended, ganancia
 * ADS1115_GAIN_4096MV, definida en ADC_GAIN dentro de i2c_manager.h):
 *
 *   AIN0 -> Presión de suministro   (transmisor 0-5V, R1=10kohm  R2=10kohm -> 0-2.5V)
 *   AIN1 -> Presión de retorno      (transmisor 0-5V, R1=10kohm  R2=10kohm -> 0-2.5V)
 *   AIN2 -> Feedback de la válvula  (0-10V escalado con divisor a 0-3.3V)
 *   AIN3 -> Corriente del motor     (SCT-013-030, salida directa 0-1V RMS, con bias de 1.65V para la lectura AC)
 */
#pragma once

#include "esp_err.h"
#include "ads1115.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Guarda el handle del ADS1115 ya inicializado (por main.c) para
 * usarlo en las lecturas posteriores.
 *
 * No toca el hardware: ads1115_init() ya debe haberse llamado antes,
 * igual que se hace hoy en init_analogico() dentro de main.c.
 */
esp_err_t analog_manager_init(ads1115_t *adc_dev);

/**
 * @brief Lee los 4 canales en orden y actualiza estado_sistema.
 *
 * Tarda aproximadamente 330ms en total: ~10ms por cada lectura DC
 * (presiones y válvula) más ~300ms de la ventana RMS del SCT-013-030
 * (ADS1115_VENTANA_RMS_MS). Pensada para llamarse desde el bucle
 * principal, no desde una tarea de alta frecuencia.
 */
void analog_manager_leer_todos(void);

/* --- Lecturas individuales, por si algún módulo necesita solo una --- */
float analog_get_presion_suministro_psi(void);
float analog_get_presion_retorno_psi(void);
float analog_get_feedback_valvula_pct(void);
float analog_get_corriente_motor_arms(void);

#ifdef __cplusplus
}
#endif