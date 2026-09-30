## Descripción del Proyecto

El desarrollo, la ingeniería y los fundamentos de este sistema se encuentran documentados a detalle en el archivo "Proyecto escrito de pasantías", elaborado por Gustavo Adolfo Medina Visbal para la empresa Serclisa Aire Acondicionado, C.A., y evaluado por la Universidad Simón Bolívar. El proyecto se encuentra instalado de forma operativa en la sala de máquinas del Centro Comercial Ciudad Tamanaco (CCCT).

## ¿Qué hace?

Consiste en una arquitectura distribuida de Internet de las Cosas (IoT) diseñada para la automatización, supervisión y control remoto de una Unidad Manejadora de Aire (UMA) de agua helada.

* **Control automático:** Ejecuta un lazo de control térmico Proporcional-Integral (PI) de forma concurrente a pie de máquina para regular con precisión la temperatura del aire de inyección.
* **Tolerancia a fallos:** Incorpora un mecanismo de almacenamiento local persistente (Store-and-Forward) en memoria Flash para salvaguardar la telemetría en caso de pérdidas de conexión a la red.
* **Gestión de emergencias:** Procesa una matriz de alarmas críticas con validación cruzada para detectar fallas mecánicas e instrumentales en tiempo real.
* **Supervisión analítica:** Permite la operación a distancia, la recepción de notificaciones y el análisis del comportamiento histórico del equipo mediante tableros interactivos.

## Programas y Complementos (Stack)

* **Hardware principal:** Emplea un microcontrolador ESP32-S3 como nodo de borde y una computadora Raspberry Pi 5 con almacenamiento en unidad de estado sólido (SSD) que funciona como servidor local autoalojado.
* **Firmware:** Está desarrollado en lenguaje C (bajo el entorno ESP-IDF), utilizando el sistema operativo en tiempo real FreeRTOS y el sistema de archivos LittleFS.
* **Red y Comunicaciones:** La telemetría se transmite mediante el protocolo de mensajería MQTT a través de un túnel cifrado de red privada virtual (VPN) configurado con WireGuard y Tailscale, lo que evita la exposición de puertos públicos.
* **Software de Servidor:** Utiliza Node-RED para la interfaz web interactiva y la lógica de enrutamiento, InfluxDB para el registro de la base de datos de series temporales, y Grafana para la visualización analítica.

## Variables del Sistema

* **Variable que controla:** El sistema manipula automáticamente el porcentaje de apertura de una válvula proporcional de agua helada.
* **Variables que monitorea:**
  * Temperaturas del agua helada y del aire, medidas tanto en las líneas de suministro como en las de retorno.
  * Humedad relativa del aire de retorno.
  * Presión del agua en la tubería.
  * Corriente eléctrica de línea consumida por el motor de la unidad.
  * Velocidad de rotación del ventilador.
