#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif



/* ---- Bus I2C ------------------------------------------------------------
 * El ESP32-S3 no tiene pines I2C fijos: se pueden usar casi cualquier GPIO.
 * Evita pines de strapping (0, 3, 45, 46) y los del USB nativo si los usas
 * (19, 20 en algunos módulos). Los valores por defecto aquí son GPIO8/GPIO9,
 * ampliamente usados en placas devkit ESP32-S3 como I2C por defecto.
 */
#define I2C_MASTER_PORT         I2C_NUM_0
#define I2C_MASTER_SDA_IO       8
#define I2C_MASTER_SCL_IO       9
#define I2C_MASTER_FREQ_HZ      400000      // 400 kHz (Fast Mode). Baja a 100000 si el cableado es largo o ruidoso.
#define I2C_MASTER_TIMEOUT_MS   1000


/* ---- Bus I2C secundario, dedicado exclusivamente a la LCD ----
 * Evita que el bucle RMS del ADS1115
 * GPIO 15/16 libres, no son de strapping. */
 
#define I2C_LCD_PORT         I2C_NUM_1
#define I2C_LCD_SDA_IO       16
#define I2C_LCD_SCL_IO       15
#define I2C_LCD_FREQ_HZ      50000


/* ---- Direcciones I2C ------------------------------------------------------
 * ADS1115: dirección depende del pin ADDR:
 *   GND -> 0x48 (por defecto en la mayoría de breakouts)
 *   VDD -> 0x49
 *   SDA -> 0x4A
 *   SCL -> 0x4B
 *
 * MCP4725: dirección depende del pin A0 (o está fija según variante):
 *   A0=GND -> 0x60
 *   A0=VCC -> 0x61
 */
#define ADS1115_I2C_ADDR        0x48
#define MCP4725_I2C_ADDR        0x60

/* ---- Parámetros eléctricos ------------------------------------------------
 * DAC_VREF_VOLTS: tensión de referencia/alimentación real del MCP4725.
 * Debe coincidir con la tensión que realmente alimenta al módulo (normalmente
 * la misma que VDD del propio MCP4725: 3.3V si lo alimentas desde el ESP32-S3).
 */
#define DAC_VREF_VOLTS          3.3f

/* Canal del ADS1115 que se va a leer con el multímetro (AIN0..AIN3) */
#define ADC_CHANNEL_TO_READ     ADS1115_CH0

/* Ganancia (PGA) del ADS1115. Con alimentación 3.3V y señales 0-3.3V,
 * ADS1115_GAIN_4096MV (FSR ±4.096V) da buen margen sin recortar la lectura. */
#define ADC_GAIN                ADS1115_GAIN_4096MV

/* Periodo del barrido automático de tensiones en el DAC (prueba "cambia cada 20s") */
#define DAC_SWEEP_PERIOD_MS     20000

/* Periodo de lectura del ADC por consola */
#define ADC_READ_PERIOD_MS      1000


/**
 * @brief Inicializa el bus I2C maestro (una sola vez para todo el proyecto).
 *
 * Usa el nuevo driver i2c_master de IDF >= 5.x. Todos los dispositivos
 * (ADS1115, MCP4725, futuros sensores, etc.) comparten este mismo bus.
 */
esp_err_t i2c_manager_init(void);

/**
 * @brief Devuelve el handle del bus ya inicializado (para añadir dispositivos).
 */
i2c_master_bus_handle_t i2c_manager_get_bus(void);

/**
 * @brief Agrega un dispositivo I2C al bus y devuelve su handle.
 *
 * @param address       Dirección I2C de 7 bits del dispositivo.
 * @param scl_speed_hz  Velocidad específica para ese dispositivo (puede diferir entre
 *                      dispositivos del mismo bus).
 * @param out_handle    Handle resultante, listo para usar en transmit/receive.
 */
esp_err_t i2c_manager_add_device(uint16_t address,
                                  uint32_t scl_speed_hz,
                                  i2c_master_dev_handle_t *out_handle);
                                  

esp_err_t i2c_manager_init_lcd_bus(void);
i2c_master_bus_handle_t i2c_manager_get_lcd_bus(void);
esp_err_t i2c_manager_add_device_lcd(uint16_t address,
                                      uint32_t scl_speed_hz,
                                      i2c_master_dev_handle_t *out_handle);

#ifdef __cplusplus
}
#endif
