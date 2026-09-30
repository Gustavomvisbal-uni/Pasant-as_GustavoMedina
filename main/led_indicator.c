
#include "led_indicator.h"
#include "led_strip.h"
#include "driver/spi_master.h"
#include "esp_log.h"
static const char *TAG = "LED_IND";

// Variable global para manejar el hardware del LED
static led_strip_handle_t led_strip;

void led_indicator_init(void)
{
    ESP_LOGI(TAG, "Inicializando LED RGB en GPIO %d", RGB_LED_GPIO);

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
        .flags.invert_out = false, // Removidos los campos inexistentes
    };

    // DESPUÉS:
    /* Backend SPI en vez de RMT: libera los 4 canales RMT del chip
     * exclusivamente para los DS18B20 (1-Wire). El protocolo hacia el
     * LED es el mismo, solo cambia el periferico de hardware que
     * genera los pulsos.
     *
     * IMPORTANTE: el driver toma el bus SPI2_HOST COMPLETO para si
     * mismo (no tiene concepto de "chip select" para compartirlo).
     * No se puede conectar ningun otro dispositivo a SPI2_HOST
     * mientras el LED lo use. En este proyecto no hay ningun otro
     * dispositivo SPI, asi que no hay conflicto. */
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags.with_dma = false,
    };

    // Inicializar y guardar el "handle" del dispositivo
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
    
    // Apagar por defecto al iniciar
    led_indicator_turn_off();
}

void led_indicator_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_strip) {
        // Establecer el color del LED 0 
        led_strip_set_pixel(led_strip, 0, r, g, b);
        // Enviar la orden de actualización al hardware
        led_strip_refresh(led_strip);
    }
}

void led_indicator_turn_off(void)
{
    if (led_strip) {
        led_strip_clear(led_strip);
    }
}