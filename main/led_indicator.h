#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <stdint.h>

// El pin del LED RGB en la mayoría de placas ESP32-S3 es el 48.
#define RGB_LED_GPIO 48 

/**
 * @brief Inicializa el hardware del LED direccionable.
 */
void led_indicator_init(void);

/**
 * @brief Establece el color del LED integrado.
 * @param r Intensidad de Rojo (0-255)
 * @param g Intensidad de Verde (0-255)
 * @param b Intensidad de Azul (0-255)
 */
void led_indicator_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Apaga el LED.
 */
void led_indicator_turn_off(void);

#endif // LED_INDICATOR_H