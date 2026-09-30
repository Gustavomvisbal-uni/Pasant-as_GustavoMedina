#pragma once

#include <stdbool.h>
#include "driver/gpio.h"


#define CONFIG_BOTON_GPIO      GPIO_NUM_11
#define CONFIG_BOTON_HOLD_MS   3000
#define CONFIG_AP_TIMEOUT_MS   (5 * 60 * 1000)   // 5 minutos

#define NVS_BOOT_NAMESPACE     "boot_cfg"
#define NVS_BOOT_KEY           "modo"
#define BOOT_MODO_NORMAL       0
#define BOOT_MODO_CONFIG       1

/**
 * 
 * @return true si el sistema debe arrancar en modo configuración
 *         (en cuyo caso, el resto de iniciar_sistema() debe omitirse).
 */
bool config_manager_debe_arrancar_en_modo_config(void);

/**
 * Arranca completamente el modo configuración: WiFi AP puro + servidor
 * HTTP. Bloquea la tarea que lo llama (usa su propio bucle con timeout).
 * Al terminar (timeout o "Listo" desde la web), borra el flag de NVS
 * y reinicia el equipo.
 */
void config_manager_ejecutar_modo_config(void);

/**
 * Arranca la tarea que vigila el botón físico durante operación normal.
 * Si se mantiene presionado 3s, guarda el flag en NVS y reinicia.
 * Llamar desde iniciar_sistema() en el arranque normal.
 */
void config_manager_iniciar_vigilancia_boton(void);

void verificar_y_arrancar_modo_config(void);