/* ============================================================
 *  littlefs_manager.c
 *  Implementación sobre "joltwallet/littlefs" (esp_littlefs.h)
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "littlefs_manager.h"
#include "estado_sistema.h"

static const char *TAG = "littlefs_manager";

/* ------------------------------------------------------------
 * Montaje de LittleFS
 * ------------------------------------------------------------ */
 
void littlefs_manager_init(void)
{
    bool ok = littlefs_ops_iniciar();
    estado_set_littlefs(ok);
    if (!ok) {
        ESP_LOGE(TAG, "LittleFS no disponible — datos offline no se guardarán");
    }
}

bool littlefs_ops_iniciar(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = "/data",   /* punto de montaje visible en el VFS */
        .partition_label        = "storage", /* debe coincidir con partitions.csv  */
        .format_if_mount_failed = true,       /* auto-formatea si detecta corrupción */
        .dont_mount             = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);

    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Fallo al montar o formatear el sistema de archivos");
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "No se encontró la partición 'storage'. Revisa partitions.csv");
        } else {
            ESP_LOGE(TAG, "Fallo al inicializar LittleFS (%s)", esp_err_to_name(err));
        }
        return false;
    }

    size_t total = 0, usado = 0;
    esp_littlefs_info(conf.partition_label, &total, &usado);
    ESP_LOGI(TAG, "LittleFS montado en /data  (total=%u  usado=%u bytes)",
              (unsigned)total, (unsigned)usado);

    return true;
}



/* Si 'ruta' supera max_bytes, descarta la mitad mas vieja del
 * contenido (alineado al proximo salto de linea, para no partir un
 * registro por la mitad) y conserva el resto — los datos MAS
 * RECIENTES. Ajusta tambien el cursor guardado en NVS (usado por
 * littlefs_ops_enviar_pendientes) para que siga apuntando al mismo
 * punto logico del archivo despues del recorte.
 *
 * Es la garantia dura de que la particion nunca se llena, sin
 * importar el intervalo de escritura ni bugs futuros que aumenten
 * la frecuencia — es un respaldo, no se espera que se dispare en
 * operacion normal. */
static void littlefs_ops_podar_si_excede(const char *ruta, const char *nvs_key, size_t max_bytes)
{
    struct stat st;
    if (stat(ruta, &st) != 0) {
        return;  // no existe el archivo todavia, nada que podar
    }
    if ((size_t)st.st_size <= max_bytes) {
        return;  // dentro del limite
    }

    size_t objetivo = max_bytes / 2;  // deja margen antes de volver a podar
    long bytes_a_quitar = (long)((size_t)st.st_size - objetivo);

    FILE *f_orig = fopen(ruta, "r");
    if (!f_orig) {
        ESP_LOGE(TAG, "No se pudo abrir %s para podar", ruta);
        return;
    }

    /* Nos movemos al punto de corte y avanzamos hasta el proximo
     * salto de linea, para no partir un registro por la mitad. */
    fseek(f_orig, bytes_a_quitar, SEEK_SET);
    long punto_corte = bytes_a_quitar;
    int c;
    while ((c = fgetc(f_orig)) != EOF && c != '\n') {
        punto_corte++;
    }
    if (c == '\n') {
        punto_corte++;  // dejar el corte justo despues del \n
    }
    fseek(f_orig, punto_corte, SEEK_SET);

    /* Copiamos el resto (los datos mas recientes) a un archivo
     * temporal, en bloques chicos — evita reservar un buffer grande
     * de una sola vez. */
    char ruta_tmp[64];
    snprintf(ruta_tmp, sizeof(ruta_tmp), "%s.tmp", ruta);
    FILE *f_tmp = fopen(ruta_tmp, "w");
    if (!f_tmp) {
        ESP_LOGE(TAG, "No se pudo crear %s para podar", ruta_tmp);
        fclose(f_orig);
        return;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f_orig)) > 0) {
        fwrite(buf, 1, n, f_tmp);
    }
    fclose(f_orig);
    fclose(f_tmp);

    remove(ruta);
    rename(ruta_tmp, ruta);

    /* El cursor en NVS apuntaba a una posicion del archivo VIEJO —
     * hay que restarle exactamente lo que se elimino del principio. */
    nvs_handle_t nvs_h;
    if (nvs_open("almacenaje", NVS_READWRITE, &nvs_h) == ESP_OK) {
        int32_t cursor_pos = 0;
        nvs_get_i32(nvs_h, nvs_key, &cursor_pos);
        int32_t nuevo_cursor = cursor_pos - (int32_t)punto_corte;
        if (nuevo_cursor < 0) nuevo_cursor = 0;
        nvs_set_i32(nvs_h, nvs_key, nuevo_cursor);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
    }

    ESP_LOGW(TAG, "%s podado: se descartaron %ld bytes de datos antiguos "
                  "(limite=%u bytes, tamaño anterior=%ld bytes)",
             ruta, punto_corte, (unsigned)max_bytes, (long)st.st_size);
}

void littlefs_ops_append_linea(const char *ruta, const char *linea)
{
    if (strcmp(ruta, RUTA_DATOS) == 0) {
        littlefs_ops_podar_si_excede(ruta, CURSOR_DATOS_KEY, DATOS_MAX_BYTES);
    } else if (strcmp(ruta, RUTA_ALARMAS) == 0) {
        littlefs_ops_podar_si_excede(ruta, CURSOR_ALARMAS_KEY, ALARMAS_MAX_BYTES);
    }

    FILE *f = fopen(ruta, "a");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo abrir %s para agregar linea", ruta);
        return;
    }
    if (fprintf(f, "%s\n", linea) < 0) {
        ESP_LOGE(TAG, "Fallo al escribir en %s (¿particion sin espacio?)", ruta);
    }
    fclose(f);
}

/* ------------------------------------------------------------
 * Utilidad: existencia de archivo
 * ------------------------------------------------------------ */
bool littlefs_ops_existe(const char *ruta)
{
    struct stat st;
    return stat(ruta, &st) == 0;
}

/* ------------------------------------------------------------
 * Mostrar archivo completo, línea a línea, por el log
 * ------------------------------------------------------------ */
void littlefs_ops_mostrar_archivo(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) {
        ESP_LOGW(TAG, "No se pudo abrir %s (¿existe?)", ruta);
        return;
    }
    struct stat st;
    stat(ruta, &st);

    ESP_LOGI(TAG, "--- %s  (%ld bytes) ---", ruta, (long)st.st_size);

    char linea[256];
    while (fgets(linea, sizeof(linea), f) != NULL) {
        size_t len = strlen(linea);
        if (len > 0 && linea[len - 1] == '\n') linea[len - 1] = '\0';
        ESP_LOGI(TAG, "  %s", linea);
    }

    ESP_LOGI(TAG, "------------------------------");
    fclose(f);
}

/* ------------------------------------------------------------
 * Información de espacio en la partición
 * ------------------------------------------------------------ */
void littlefs_ops_mostrar_espacio(void)
{
    size_t total = 0, usado = 0;
    esp_err_t err = esp_littlefs_info("storage", &total, &usado);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo obtener info de la partición");
        return;
    }

    ESP_LOGI(TAG, "Total : %6u bytes (%.1f KB)", (unsigned)total, total / 1024.0f);
    ESP_LOGI(TAG, "Usado : %6u bytes (%.1f KB)", (unsigned)usado, usado / 1024.0f);
    ESP_LOGI(TAG, "Libre : %6u bytes (%.1f KB)",
             (unsigned)(total - usado), (total - usado) / 1024.0f);
}

/* ------------------------------------------------------------
 * Vaciar el contenido de un archivo
 * ------------------------------------------------------------ */
void littlefs_ops_vaciar_archivo(const char *ruta)
{
    /* Abrir un archivo en modo "w" trunca (vacía) su contenido inmediatamente */
    FILE *f = fopen(ruta, "w");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo vaciar %s", ruta);
        return;
    }
    fclose(f);
    ESP_LOGI(TAG, "Archivo %s vaciado exitosamente (0 bytes).", ruta);
}

/* ------------------------------------------------------------
 * Enviar pendientes con validación de cursor (Store and Forward)
 * ------------------------------------------------------------ */
void littlefs_ops_enviar_pendientes(const char *ruta_archivo, const char *nvs_key, bool wifi_conectado, bool mqtt_conectado, littlefs_envio_callback_t enviar_linea)
{
    if (!wifi_conectado || !mqtt_conectado) {
        ESP_LOGW(TAG, "Sin conexion (WiFi/MQTT). Los datos seguiran guardados de forma segura.");
        return;
    }

    /* 1. Abrir NVS y leer el cursor de posicion */
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("almacenaje", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al abrir NVS");
        return;
    }

    int32_t cursor_pos = 0;
    err = nvs_get_i32(nvs_handle, nvs_key, &cursor_pos);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No se encontro cursor previo, asumiendo inicio (0)");
    }

    /* 2. Abrir el archivo de lecturas */
    FILE *f = fopen(ruta_archivo, "r");
    if (!f) {
        ESP_LOGI(TAG, "No hay archivo %s pendiente por enviar.", ruta_archivo);
        nvs_close(nvs_handle);
        return;
    }

    /* 3. Mover el puntero de lectura a la posicion guardada */
    fseek(f, cursor_pos, SEEK_SET);

    char linea[256];
    int enviadas = 0;

    /* 4. Leer y "enviar" linea por linea */
    while (fgets(linea, sizeof(linea), f) != NULL) {
        
        /* Limpiar el salto de linea al final para imprimir limpio */
        size_t len = strlen(linea);
        if (len > 0 && linea[len - 1] == '\n') linea[len - 1] = '\0';

        ESP_LOGI(TAG, "[MQTT SIMULADO] Enviando -> %s", linea);
        if (enviar_linea != NULL) {
            enviar_linea(linea);
        }
        
        /* Simulamos que el broker MQTT respondio con el PUBACK (confirmacion) */
        bool confirmacion_mqtt = true; 

        if (confirmacion_mqtt) {
            /* Guardamos la nueva posicion del cursor actual (bytes) */
            cursor_pos = ftell(f);
            enviadas++;
        } else {
            ESP_LOGE(TAG, "Fallo el envio MQTT. Deteniendo procesamiento.");
            break; /* Salimos del bucle para no perder la linea */
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Espera 100 milisegundos entre mensajes
    }

    /* 5. Guardar la nueva posicion en NVS por si se corta la luz */
    nvs_set_i32(nvs_handle, nvs_key, cursor_pos);
    nvs_commit(nvs_handle);

    /* 6. Verificar si llegamos al final del documento (EOF) */
    if (feof(f)) {
        ESP_LOGI(TAG, "=== Se confirmaron todas las lecturas (%d). Vaciando archivo ===", enviadas);
        fclose(f);
        
        /* Ejecutamos la funcion que borra el contenido */
        littlefs_ops_vaciar_archivo(ruta_archivo);
        
        /* Reiniciamos el cursor de la NVS a 0 */
        nvs_set_i32(nvs_handle, nvs_key, 0);
        nvs_commit(nvs_handle);
    } else {
        ESP_LOGI(TAG, "Envio pausado temporalmente. Se enviaron %d lineas.", enviadas);
        fclose(f);
    }

    nvs_close(nvs_handle);
}