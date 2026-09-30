#include "control_manager.h"
#include "alarmas_manager.h"
#include "mcp4725.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "CONTROL_MGR";
#define NVS_CONTROL_NAMESPACE  "control_cfg"
static bool s_falla_dac = false;

/* Valores de fábrica, usados solo si NVS está vacío (primer arranque
 * o después de un borrado de flash) */
#define DEFAULT_SETPOINT     18.0f
#define DEFAULT_KP           1.0f
#define DEFAULT_KI           0.1f
#define DEFAULT_KD           0.0f
#define DEFAULT_MODO         MODO_VALVULA_MANUAL
#define DEFAULT_MANUAL_PCT   0.0f

static mcp4725_t *s_dac = NULL;

/* Estado interno del PID: vive acá, NO en estado_sistema, porque es
 * un detalle de implementación del algoritmo, no un dato de proceso
 * que otros módulos necesiten leer. */
static float   s_integral          = 0.0f;
static float   s_error_anterior    = 0.0f;
static int64_t s_ultimo_calculo_us = 0;

/* Verificación de que la válvula obedece al comando */
static float   s_ultimo_pct_comandado = 0.0f;
static int64_t s_ultimo_cambio_us     = 0;
static bool    s_primer_ciclo         = true;
static int64_t s_tiempo_esperado_ms   = 0;  

/* ============================================================
 * Persistencia en NVS — floats via blob (NVS no tiene un tipo
 * float nativo), enteros pequeños via nvs_set_u8/get_u8
 * ============================================================ */
static void guardar_float(const char *key, float valor)
{
    nvs_handle_t h;
    if (nvs_open(NVS_CONTROL_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo abrir NVS para guardar '%s'", key);
        return;
    }
    nvs_set_blob(h, key, &valor, sizeof(valor));
    nvs_commit(h);
    nvs_close(h);
}

static float leer_float(const char *key, float valor_default)
{
    nvs_handle_t h;
    float valor = valor_default;
    if (nvs_open(NVS_CONTROL_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(valor);
        nvs_get_blob(h, key, &valor, &len);  /* si no existe, deja valor_default */
        nvs_close(h);
    }
    return valor;
}

static void guardar_u8(const char *key, uint8_t valor)
{
    nvs_handle_t h;
    if (nvs_open(NVS_CONTROL_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo abrir NVS para guardar '%s'", key);
        return;
    }
    nvs_set_u8(h, key, valor);
    nvs_commit(h);
    nvs_close(h);
}

static uint8_t leer_u8(const char *key, uint8_t valor_default)
{
    nvs_handle_t h;
    uint8_t valor = valor_default;
    if (nvs_open(NVS_CONTROL_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &valor);
        nvs_close(h);
    }
    return valor;
}

void control_manager_cargar_configuracion(void)
{
    estado_set_setpoint(leer_float("setpoint", DEFAULT_SETPOINT));
    estado_set_kp(leer_float("kp", DEFAULT_KP));
    estado_set_ki(leer_float("ki", DEFAULT_KI));
    estado_set_kd(leer_float("kd", DEFAULT_KD));
    estado_set_modo_valvula((modo_valvula_t)leer_u8("modo", (uint8_t)DEFAULT_MODO));
    estado_set_valvula_manual_pct(leer_float("manual_pct", DEFAULT_MANUAL_PCT));
    estado_set_control_activo(leer_u8("ctrl_activo", 0) == 1);

    ESP_LOGI(TAG, "Config cargada: SP=%.1f Kp=%.2f Ki=%.2f Kd=%.2f modo=%s manual=%.1f%%, ON/OFF=%s",
             estado_get_setpoint(), estado_get_kp(), estado_get_ki(), estado_get_kd(),
             estado_get_modo_valvula() == MODO_VALVULA_AUTOMATICO ? "AUTOMATICO" : "MANUAL",
             estado_get_valvula_manual_pct(),
             estado_get_control_activo() ? "ON" : "OFF");
}

/* ============================================================
 * Setters públicos: RAM + NVS en un solo lugar
 * ============================================================ */
void control_manager_set_modo(modo_valvula_t modo)
{
    modo_valvula_t modo_anterior = estado_get_modo_valvula();
    if (modo == modo_anterior) {
        return;  /* sin cambio real: no repetir la escritura en NVS
                     ni, mas importante, la transferencia sin golpe */
    }
    estado_set_modo_valvula(modo);
    guardar_u8("modo", (uint8_t)modo);

    if (modo_anterior == MODO_VALVULA_MANUAL && modo == MODO_VALVULA_AUTOMATICO) {
        /* Transferencia sin golpe: el termino integral se inicializa
         * para que la PRIMERA salida calculada del PID coincida con
         * la posicion real que la valvula ya tiene (su feedback
         * actual), en vez de arrancar desde 0 y dar un salto brusco.
         *
         * salida = Kp*error + integral  =>  integral = feedback - Kp*error */
        float feedback_actual = estado_get_feedback_valvula();
        float kp = estado_get_kp();
        float pv_actual = estado_get_temp_aire_suministro();
        float error_actual;

        if (isnan(pv_actual)) {
            /* Sin dato valido de temperatura justo en el instante del
             * cambio de modo: se asume error=0 para no envenenar el
             * integral con NaN (que se propagaria para siempre). */
            ESP_LOGW(TAG, "Temp. aire suministro invalida (NaN) al pasar a automatico. "
                          "Se asume error=0 para la transferencia sin golpe.");
            error_actual = 0.0f;
        } else {
            error_actual = pv_actual - estado_get_setpoint();
        }

        s_integral = feedback_actual - (kp * error_actual);
        s_error_anterior = error_actual;  /* evita un golpe de derivada tambien */

        ESP_LOGI(TAG, "Transferencia sin golpe: integral inicial=%.2f (feedback=%.1f%%)",
                 s_integral, feedback_actual);
    }
}

void control_manager_set_manual_pct(float pct)
{
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    if (pct == estado_get_valvula_manual_pct()) {
        return;
    }
    estado_set_valvula_manual_pct(pct);
    guardar_float("manual_pct", pct);
}

void control_manager_set_setpoint(float valor)
{
    if (valor == estado_get_setpoint()) {
        return;
    }
    estado_set_setpoint(valor);
    guardar_float("setpoint", valor);
}

void control_manager_set_kp(float valor)
{
    if (valor == estado_get_kp()) {
        return;
    }
    estado_set_kp(valor);
    guardar_float("kp", valor);
}

void control_manager_set_ki(float valor)
{
    if (valor == estado_get_ki()) {
        return;
    }
    estado_set_ki(valor);
    guardar_float("ki", valor);
}

void control_manager_set_kd(float valor)
{
    if (valor == estado_get_kd()) {
        return;
    }
    estado_set_kd(valor);
    guardar_float("kd", valor);
}

/* ============================================================
 * Comando de salida hacia el hardware
 * ============================================================ */
static void comandar_valvula_pct(float pct_deseado)
{
    /* Limites de seguridad: nunca pedir cierre ni apertura total */
    if (pct_deseado < CONTROL_VALVULA_MIN_PCT) pct_deseado = CONTROL_VALVULA_MIN_PCT;
    if (pct_deseado > CONTROL_VALVULA_MAX_PCT) pct_deseado = CONTROL_VALVULA_MAX_PCT;

    /* % -> voltios de la valvula (0-10V) -> voltios que debe generar
     * el DAC, descontando la ganancia x3 del LM358 */
    float voltaje_valvula = (pct_deseado / 100.0f) * 10.0f;
    float voltaje_dac = voltaje_valvula / CONTROL_GANANCIA_LM358;

    esp_err_t err = mcp4725_set_voltage(s_dac, voltaje_dac);
    EVALUAR_FALLA(err != ESP_OK, s_falla_dac, TAG, 
                  "Error critico I2C comandando el DAC de la valvula", 
                  "Comunicacion con el DAC restablecida");

    if (err != ESP_OK) return;

    estado_set_valvula_comando_pct(pct_deseado);

    /* Reiniciar la ventana de verificacion en CUALQUIER cambio (ya no
     * solo >1%). El timeout ahora es proporcional a la magnitud del
     * cambio: un ajuste chico del PID no espera los mismos 70s que un
     * cambio de 0% a 100%, porque tampoco tarda lo mismo en recorrerse. */
    float delta = fabsf(pct_deseado - s_ultimo_pct_comandado);
    if (delta > 0.01f || s_primer_ciclo) {
        s_ultimo_cambio_us = esp_timer_get_time();
        s_tiempo_esperado_ms = CONTROL_VALVULA_MARGEN_MS + (int64_t)(CONTROL_VALVULA_RECORRIDO_MS * (delta / 100.0f));
        s_primer_ciclo = false;
    }
    s_ultimo_pct_comandado = pct_deseado;
}

/* ============================================================
 * Verificacion: el feedback real debe converger al comando dentro
 * del tiempo de recorrido del actuador
 * ============================================================ */
static void verificar_actuacion(void)
{
    int64_t ahora_us = esp_timer_get_time();
    int64_t tiempo_esperando_ms = (ahora_us - s_ultimo_cambio_us) / 1000;

    if (tiempo_esperando_ms < (CONTROL_VALVULA_RECORRIDO_MS + CONTROL_VALVULA_MARGEN_MS)) {
        return; 
    }

    float feedback_actual = estado_get_feedback_valvula();
    float diferencia = fabsf(feedback_actual - s_ultimo_pct_comandado);
    bool condicion_falla = diferencia > CONTROL_VALVULA_TOLERANCIA_PCT;

    /* Cadenas dinámicas de texto para la macro */
    char msg_err[128];
    char msg_ok[128];
    snprintf(msg_err, sizeof(msg_err), "Valvula atascada: comandado=%.1f%% feedback=%.1f%% (diff=%.1f%%)",
             s_ultimo_pct_comandado, feedback_actual, diferencia);
    snprintf(msg_ok, sizeof(msg_ok), "Valvula OK (destrancada): feedback=%.1f%%", feedback_actual);

    /* Bandera local para el control de repetición */
    static bool s_falla_mecanica = false;

    /* Disparo centralizado */
    EVALUAR_FALLA(condicion_falla, s_falla_mecanica, TAG, msg_err, msg_ok);

    /* Sincronizamos el estado global para que el dashboard o el PID lo sepan */
    if (s_falla_mecanica != estado_get_valvula_en_falla()) {
        estado_set_valvula_en_falla(s_falla_mecanica);
    }
}

/* ============================================================
 * PID (modo automatico) — dt medido, no asumido, porque el ciclo
 * del bucle principal (fuente de temp_aire_suministro) no tiene
 * duracion fija.
 *
 * IMPORTANTE: pv debe venir YA validada (isnan() descartado) por
 * quien llama — ver control_task(). Esta funcion no vuelve a leer
 * estado_sistema para evitar un segundo fetch que, en teoria, podria
 * devolver un valor distinto al ya validado por el llamador.
 * ============================================================ */
static float calcular_salida_automatica(float dt_s, float pv)
{
    float setpoint = estado_get_setpoint();
    float kp = estado_get_kp();
    float ki = estado_get_ki();
    float kd = estado_get_kd();

    float error = pv - setpoint;
    float derivada = (dt_s > 0.0f) ? (error - s_error_anterior) / dt_s : 0.0f;
    s_error_anterior = error;

    /* Acumular el integral ANTES de aplicar limites; si la salida se
     * satura, se deshace el incremento de este ciclo (anti-windup) */
    s_integral += error * ki * dt_s;

    float salida = (kp * error) + s_integral + (kd * derivada);

    if (salida > CONTROL_VALVULA_MAX_PCT) {
        salida = CONTROL_VALVULA_MAX_PCT;
        s_integral -= error * ki * dt_s;
    } else if (salida < CONTROL_VALVULA_MIN_PCT) {
        salida = CONTROL_VALVULA_MIN_PCT;
        s_integral -= error * ki * dt_s;
    }

    return salida;
}

/* ============================================================
 * Tarea de control — el "latido" del modulo, aislada del bucle
 * principal para poder ajustar su periodo y monitorearla por
 * separado (nombre propio en la lista de tareas de FreeRTOS)
 * ============================================================ */
static void control_task(void *pvParameters)
{
    s_ultimo_calculo_us = esp_timer_get_time();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIODO_MS));

        if (!estado_get_control_activo()) {
            /* Interruptor maestro apagado: no tocar la valvula. Se
             * queda donde este — el actuador ademas retiene su
             * ultima posicion fisica si pierde la señal de comando,
             * segun su propio datasheet (fail-safe "stays in place"). */
            continue;
        }

        int64_t ahora_us = esp_timer_get_time();
        float dt_s = (ahora_us - s_ultimo_calculo_us) / 1000000.0f;
        s_ultimo_calculo_us = ahora_us;

        float salida_pct;
        if (estado_get_modo_valvula() == MODO_VALVULA_AUTOMATICO) {
            float pv = estado_get_temp_aire_suministro();
            if (isnan(pv)) {
                /* Sin dato valido de temperatura (sensor desconectado
                 * o fallando este ciclo). NO se corre el PID: usar un
                 * NaN en la aritmetica lo dejaria contaminado para
                 * siempre (NaN + cualquier cosa = NaN), y convertir un
                 * NaN a entero para el DAC es comportamiento
                 * indefinido en C. La valvula retiene su ultima
                 * posicion comandada hasta que vuelva un dato valido. */
                ESP_LOGW(TAG, "Temp. aire suministro invalida (NaN) — PID en pausa este ciclo, "
                              "valvula retiene su ultima posicion");
                continue;
            }
            salida_pct = calcular_salida_automatica(dt_s, pv);
        } else {
            salida_pct = estado_get_valvula_manual_pct();
        }

        comandar_valvula_pct(salida_pct);
        verificar_actuacion();
    }
}

void control_manager_set_control_activo(bool activo)
{
    if (activo == estado_get_control_activo()) {
        return; // Sin cambios, no escribimos en memoria
    }
    estado_set_control_activo(activo);
    guardar_u8("ctrl_activo", activo ? 1 : 0);
}

/* ============================================================
 * Inicializacion
 * ============================================================ */
void control_manager_init(mcp4725_t *dac_handle)
{
    s_dac = dac_handle;

    control_manager_cargar_configuracion();

    xTaskCreatePinnedToCore(control_task, "control_task", 4096, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "Tarea de control iniciada (periodo %dms)", CONTROL_PERIODO_MS);
}