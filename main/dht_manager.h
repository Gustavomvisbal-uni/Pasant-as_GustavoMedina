#ifndef DHT_MANAGER_H
#define DHT_MANAGER_H

#include "dht.h"

#define DHT_SENSOR_TYPE  DHT_TYPE_AM2301
#define DHT_GPIO_PIN     2

/**
 * Lee temperatura y humedad del DHT11 y actualiza estado_sistema.
 * Solo la humedad se guarda en estado (la temperatura se descarta
 * por ser menos precisa que los DS18B20).
 */
void dht_manager_leer(void);

#endif // DHT_MANAGER_H