#ifndef VPN_MANAGER_H
#define VPN_MANAGER_H

#define TAILSCALE_AUTH_KEY "xxxxxxxxx"

// IP Tailscale del broker — usada para el sondeo TCP previo a MQTT
#define BROKER_VPN_IP "xxxxxx"

/**
 * @brief Inicializa MicroLink (Tailscale). Puede llamarse de nuevo
 *        tras un fallo — reinicia el cliente desde cero.
 */
void vpn_app_start(void);

#endif // VPN_MANAGER_H
