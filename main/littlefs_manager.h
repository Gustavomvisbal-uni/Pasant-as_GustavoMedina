/* ============================================================
 *  littlefs_manager.h
 *  Operaciones de archivo sobre LittleFS para el ESP32-S3
 *  (versión enfocada solo en archivos, con datos de sensores
 *   inventados — sin WiFi ni MQTT todavía)
 * ============================================================ */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rutas de archivo (relativas al punto de montaje "/data") */
#define RUTA_DATOS      "/data/Datos.txt"
#define RUTA_ALARMAS  "/data/Alarmas.txt"


/* Claves NVS para los cursores de cada archivo */
#define CURSOR_DATOS_KEY     "cursor_dat"
#define CURSOR_ALARMAS_KEY "cursor_alrm"


/* Tamaño maximo que puede alcanzar datos.txt antes de que se empiecen a
 * descartar los registros mas viejos (poda automatica). La particion
 * "storage" completa son 10MB; se deja ~14% de margen (creado.txt,
 * boots.txt, y espacio libre que LittleFS necesita para su propia
 * gestion interna — wear-leveling, bloques de repuesto). Superar el
 * 100% de una particion LittleFS puede volverla inestable, no solo
 * "llena". */
#define DATOS_MAX_BYTES  (9 * 1024 * 1024)
#define ALARMAS_MAX_BYTES  (512 * 1024)

/** Callback que littlefs_ops_enviar_pendientes() llama por cada línea
 *  pendiente. Quien lo implemente decide cómo "enviarla" (MQTT, etc.)
 *  — littlefs_manager no necesita saber nada de MQTT. */
typedef void (*littlefs_envio_callback_t)(const char *linea);


void littlefs_manager_init(void);


/**
 * Monta la partición "storage" (definida en partitions.csv) como
 * LittleFS en el punto de montaje "/data".
 * @return true si el montaje fue exitoso
 */
bool littlefs_ops_iniciar(void);

/** Devuelve true si la ruta indicada existe. */
bool littlefs_ops_existe(const char *ruta);


/** Abre un archivo en modo lectura y lo imprime línea a línea por log. */
void littlefs_ops_mostrar_archivo(const char *ruta);

/** Imprime el espacio total/usado/libre de la partición LittleFS. */
void littlefs_ops_mostrar_espacio(void);

/** 
 * Vacía el contenido de un archivo dejándolo en 0 bytes.
 * Útil para reiniciar los datos acumulados.
 */
void littlefs_ops_vaciar_archivo(const char *ruta);

/** Procesa el archivo Datos.txt, envía por MQTT y maneja la persistencia en NVS */
void littlefs_ops_enviar_pendientes(const char *ruta_archivo, const char *nvs_key, bool wifi_conectado, bool mqtt_conectado, littlefs_envio_callback_t enviar_linea);

/** Añade una línea de texto ya formateada al final de un archivo.
 *  Genérica: no conoce la estructura de los datos, solo los guarda. */
void littlefs_ops_append_linea(const char *ruta, const char *linea);


#ifdef __cplusplus
}
#endif