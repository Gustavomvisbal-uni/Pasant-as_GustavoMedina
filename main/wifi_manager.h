#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

// --- CONFIGURACIÓN DE RED ---
#define WIFI_SSID      "XXXXXXX" //  
#define WIFI_PASS         "XXXXX"  //   
#define MAXIMUM_RETRY       5
#define COOLDOWN_TIME_MS    60000 // 60 segundos de espera entre ráfagas de reintentos

// Modo AP de configuración
#define AP_CONFIG_SSID       "xxxx"
#define AP_CONFIG_PASS       "xxxx"     // mínimo 8 caracteres para WPA2
#define AP_CONFIG_MAX_CONN   2

/**
 * @brief Inicializa la interfaz de red, el bucle de eventos y arranca
 * la conexión WiFi en modo Estación (STA) de forma asíncrona.
 */
void wifi_init_sta(void);

void wifi_manager_guardar_credenciales(const char *ssid, const char *pass);

#endif // WIFI_MANAGER_H