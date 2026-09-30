idf_component_register(SRCS "main.c"
							"mqtt_manager.c"
							"led_indicator.c"
							"wifi_manager.c"
							"vpn_manager.c"
							"lcd_manager.c"
							"ds18b20_manager.c"
							"hall_rpm_manager.c"
							"littlefs_manager.c"
							"estado_sistema.c"
							"dht_manager.c"
							"i2c_manager.c"
					        "ads1115.c"
					        "mcp4725.c"
					        "config_manager.c"
					        "analog_manager.c"
					        "control_manager.c"
					        "alarmas_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_timer esp_driver_pcnt esp_event nvs_flash mqtt freertos log led_strip microlink lwip littlefs driver esp_driver_i2c)
                    
# ── Generar la imagen de LittleFS a partir de la carpeta data/ ──
# Empaqueta todo lo que hay en ../data (tu mio.txt) y lo flashea
# directamente en la partición "storage" definida en partitions.csv.
littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)