#include "analog_manager.h"
#include "estado_sistema.h"
#include "esp_log.h"
#include "alarmas_manager.h"

#define LIMITE_CORRIENTE_MAX_A 16.0f  /* Sobrepaso = motor trancado/forzado */
#define LIMITE_CORRIENTE_MIN_A  2.0f  /* Caída (girando en vacío) = correa rota */
#define LIMITE_MOTOR_APAGADO_A  0.5f  /* Filtro para evitar alarmas si el motor está apagado */

static const char *TAG = "ANALOG_MGR";

static bool s_falla_adc_corriente = false;
static bool s_falla_motor_trancado = false;
static bool s_falla_motor_correa = false;
static bool s_falla_adc_psum = false;
static bool s_falla_adc_pret = false;

/* ---- Presiones: transmisor 0-5V Con divisor de voltaje de 10K 10K ----
 * 0 V  * 0.5 = 0 V  -> 0 PSI
 * 5 V  * 0.5 = 2.5V  -> 174 PSI (fondo de escala del transmisor) */
#define PRESION_V_MIN           0.0f
#define PRESION_V_MAX           2.5f
#define PRESION_PSI_MAX         174.0f

/* ---- Feedback de válvula: rango REAL del actuador ML7420A3055 es 2-10V
 * (no 0-10V), confirmado en datasheet Honeywell. Divisor 220k/100k:
 * relación subida una década respecto al primer diseño para no cargar
 * la salida del actuador (impedancia de salida 1kohm, carga máxima 1mA).
 * 2V (cerrada) -> 0.625V ;  10V (abierta) -> 3.125V, ambos en el ADS1115 */
#define VALVULA_V_MIN            0.625f
#define VALVULA_V_MAX            3.125f

/* ---- SCT-013-030: ya entrega 0-1V RMS de forma directa (versión con
 * resistencia de carga interna). Relación fija del sensor: 30A -> 1V.
 * El bias es el punto medio de un divisor 2x10k a 3.3V, necesario porque
 * la señal es AC y oscila alrededor de ese punto, no de 0V. ---- */
#define SCT_BIAS_VOLTS          1.65f
#define SCT_AMPERIOS_POR_VOLT   30.0f

static ads1115_t *s_adc = NULL;

esp_err_t analog_manager_init(ads1115_t *adc_dev)
{
    if (adc_dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_adc = adc_dev;
    ESP_LOGI(TAG, "Listo (AIN0=Psum, AIN1=Pret, AIN2=Valvula, AIN3=Corriente)");
    return ESP_OK;
}

/* Lleva un voltaje al rango [0,1] dentro de [v_min, v_max] y lo escala a
 * [0, salida_max]. Recorta (clamp) fuera de rango: un transmisor 4-20mA
 * fuera de calibración o un sensor desconectado no debe generar valores
 * negativos ni disparados en el LCD/MQTT. */
static float escalar_clamp(float v, float v_min, float v_max, float salida_max)
{
    if (v <= v_min) return 0.0f;
    if (v >= v_max) return salida_max;
    return (v - v_min) / (v_max - v_min) * salida_max;
}

static float leer_presion_psi(ads1115_channel_t canal, const char *nombre, bool *bandera_falla)
{
    if (s_adc == NULL) return 0.0f;

    float v = 0.0f;
    esp_err_t err = ads1115_read_voltage(s_adc, canal, &v);
    
    // Preparamos los mensajes dinámicos para que MQTT indique qué sensor falló
    char msg_err[64], msg_ok[64];
    snprintf(msg_err, sizeof(msg_err), "%s: Error de lectura I2C", nombre);
    snprintf(msg_ok, sizeof(msg_ok), "%s: Lectura I2C restablecida", nombre);

    // Evaluamos y enviamos la alarma si hay error (err != ESP_OK)
    EVALUAR_FALLA(err != ESP_OK, *bandera_falla, TAG, msg_err, msg_ok);

    if (err != ESP_OK) {
        return 0.0f;
    }

    float psi = escalar_clamp(v, PRESION_V_MIN, PRESION_V_MAX, PRESION_PSI_MAX);
    ESP_LOGI(TAG, "%s: %.3fV -> %.1f PSI", nombre, v, psi);
    return psi;
}
void analog_manager_leer_todos(void)
{
    if (s_adc == NULL) {
        ESP_LOGE(TAG, "analog_manager usado antes de analog_manager_init()");
        return;
    }

    /* --- Presiones: solo se muestran en LCD/MQTT, no entran al PID.
     * Si falla la lectura se reporta 0 PSI, igual que hace ds18b20_manager
     * con NAN para sensores desconectados. --- */
     
    float psi_sum = leer_presion_psi(ADS1115_CH0, "Presion suministro", &s_falla_adc_psum);
    
    float psi_ret = leer_presion_psi(ADS1115_CH1, "Presion retorno", &s_falla_adc_pret);
   	
   	
   	if(psi_sum != 0){estado_set_presion_suministro(psi_sum);}
   	
    if(psi_ret != 0){estado_set_presion_retorno(psi_ret);}
    
    /* --- Feedback de la válvula: SÍ entra al PID. Ante un error de
     * lectura preferimos no pisar el último valor válido con un 0, que
     * el lazo de control interpretaría como "válvula cerrada". --- */
    float v_valvula = 0.0f;
    float pct_valvula = estado_get_feedback_valvula(); /* si falla, se conserva el ultimo valor para el log */
    esp_err_t err = ads1115_read_voltage(s_adc, ADS1115_CH2, &v_valvula);
    if (err == ESP_OK) {
	    pct_valvula = 100.0f - escalar_clamp(v_valvula, VALVULA_V_MIN, VALVULA_V_MAX, 100.0f);
	    estado_set_feedback_valvula(pct_valvula);
	}else {
        ESP_LOGW(TAG, "Valvula: error de lectura (%s). Se mantiene el ultimo valor.",
                 esp_err_to_name(err));
    }

    /* --- Corriente del motor: RMS sobre la ventana de muestreo continuo
     * definida en ADS1115_VENTANA_RMS_MS (ads1115.h). Mismo criterio que
     * la válvula: un error no debe resetear la lectura a 0A. --- */
    float vrms = 0.0f;
    float amperios = estado_get_corriente_motor();
    err = ads1115_read_rms(s_adc, ADS1115_CH3, SCT_BIAS_VOLTS, &vrms);
    
    /* 1. Monitoreo de hardware electrónico */
    EVALUAR_FALLA(err != ESP_OK, s_falla_adc_corriente, TAG,
                  "Corriente: error de lectura RMS en ADC",
                  "Corriente: lectura RMS del ADC restablecida");

    if (err == ESP_OK) {
        amperios = vrms * SCT_AMPERIOS_POR_VOLT;
        estado_set_corriente_motor(amperios);

        /* 2. Diagnóstico mecánico deducido */
        bool motor_trancado = (amperios >= LIMITE_CORRIENTE_MAX_A);
        
        
        /* Se considera correa rota si el motor está recibiendo energía (amperios > apagado) 
         * pero el eje del ventilador no registra movimiento (RPM < 50). */
        float rpm_actuales = estado_get_rpm(); 
        bool correa_rota = (amperios > LIMITE_MOTOR_APAGADO_A) && (rpm_actuales < 50.0f);
        

        EVALUAR_FALLA(motor_trancado, s_falla_motor_trancado, TAG,
                      "CRITICO: Motor atascado o sobreesforzado (Corriente alta)",
                      "Consumo del motor normalizado (esfuerzo regular)");

        EVALUAR_FALLA(correa_rota, s_falla_motor_correa, TAG,
                      "CRITICO: Correa rota (Motor girando en vacio/Corriente baja)",
                      "Transmision de correa restablecida (consumo normal)");
    }
}

float analog_get_presion_suministro_psi(void)
{
    return leer_presion_psi(ADS1115_CH0, "Presion suministro", &s_falla_adc_psum);
}

float analog_get_presion_retorno_psi(void)
{
    return leer_presion_psi(ADS1115_CH1, "Presion retorno", &s_falla_adc_pret);
}

float analog_get_feedback_valvula_pct(void)
{
    if (s_adc == NULL) return 0.0f;

    float v = 0.0f;
    esp_err_t err = ads1115_read_voltage(s_adc, ADS1115_CH2, &v);
    if (err != ESP_OK) return 0.0f;

    return 100.0f - escalar_clamp(v, VALVULA_V_MIN, VALVULA_V_MAX, 100.0f);
}

float analog_get_corriente_motor_arms(void)
{
    if (s_adc == NULL) return 0.0f;

    float vrms = 0.0f;
    esp_err_t err = ads1115_read_rms(s_adc, ADS1115_CH3, SCT_BIAS_VOLTS, &vrms);
    if (err != ESP_OK) return 0.0f;

    return vrms * SCT_AMPERIOS_POR_VOLT;
}