# Capa de Transporte Confiable (RDT) sobre UDP
### Optimizada para Aprendizaje Federado Masivo

Este módulo contiene la implementación del protocolo de **Reliable Data Transfer (RDT)** sobre el protocolo no orientado a conexión **UDP**. Está diseñado para soportar el intercambio masivo, concurrente y ordenado de matrices de pesos neuronales entre múltiples nodos esclavos y un nodo maestro.

---

## Componentes del Entregable Central (`protocol.hpp` y `protocol.cpp`)

Este componente constituye el núcleo del sistema de transporte. Toda la lógica de comunicación, control de flujo, retransmisión, integridad de datos y gestión dinámica de memoria reside en estos dos archivos.

### Matriz de Optimizaciones e Implementaciones

| Característica / Mecanismo | Descripción Técnica | Impacto en el Sistema |
| :--- | :--- | :--- |
| **Algoritmo Jacobson-Karels** | Cálculo dinámico del RTO (Timeout) basado en `EstimatedRTT` y `DevRTT`. | Evita retransmisiones prematuras y tiempos de espera excesivos. El RTO se mantiene entre **100 ms** y **2000 ms** (`BACKOFF_MAX_MS`). |
| **Recolector de Mensajes Zombi** | Función `cleanupZombieMessages()` que monitorea la marca de tiempo `lastActivity`. | Elimina automáticamente mensajes incompletos inactivos durante más de 15 segundos, evitando fugas de memoria. |
| **Asignación Previa de Memoria** | Uso de `reserve()` antes de reconstruir el mensaje completo. | Reduce la cantidad de realocaciones de memoria durante el reensamblado de mensajes grandes. |
| **Control de Integridad y NACKs** | Verificación mediante CRC32 con soporte para `ACK_ERROR`. | Cuando un fragmento llega corrupto, el receptor solicita su retransmisión inmediata sin esperar el timeout. |

---

## Entorno de Validación

> [!NOTE]
> Los siguientes archivos no forman parte de la implementación principal del protocolo. Se utilizaron para validar su funcionamiento mediante pruebas funcionales y de carga.

### Archivos del Banco de Pruebas

- **`client.cpp` y `server.cpp`**  
  Contienen las funciones `main()` utilizadas para simular el comportamiento del cliente y del servidor. Permiten validar el envío, recepción, fragmentación y reconstrucción de mensajes utilizando arreglos binarios de tipo `float32` de aproximadamente **331 KB**.

- **`test_carga.sh`**  
  Script en Bash que automatiza las pruebas ejecutando un servidor y cinco clientes simultáneamente para verificar el aislamiento de las conexiones mediante la llave `IP:PUERTO:SEQ`.

---

## Compilación

El proyecto incluye un **Makefile**, por lo que basta ejecutar:

```bash
make
```

Si se desea compilar manualmente:

```bash
g++ server.cpp protocol.cpp -o server
g++ client.cpp protocol.cpp -o client
```

---

## Permisos de ejecución

En sistemas Linux, otorgar permisos de ejecución a los scripts y ejecutables:

```bash
chmod +x test_carga.sh server client
```

---

## Simulación de condiciones de red (Opcional)

Para evaluar el comportamiento del protocolo bajo condiciones adversas, puede utilizarse la herramienta **`tc`** junto con **`netem`** para introducir latencia y pérdida de paquetes sobre la interfaz de loopback (`lo`).

Aplicar una configuración de prueba:

```bash
sudo tc qdisc add dev lo root netem delay 50ms 10ms loss 5%
```

Esta configuración simula:

- Latencia promedio de **50 ms**.
- Variación de latencia de **10 ms**.
- Pérdida aleatoria del **5 %** de los paquetes.

Una vez finalizadas las pruebas, eliminar la configuración aplicada:

```bash
sudo tc qdisc del dev lo root
```

> **Nota:** Estos comandos requieren privilegios de administrador (`sudo`) y solo afectan la interfaz de loopback utilizada para las pruebas locales.

---

## Ejecución

Para ejecutar las pruebas concurrentes:

```bash
./test_carga.sh
```

Si se desea ejecutar manualmente:

Servidor:

```bash
./server
```

Cliente:

```bash
./client
```

---

## Notas de implementación

- El protocolo implementa una capa de transporte confiable sobre UDP mediante el mecanismo **Stop-and-Wait**.
- Cada mensaje es fragmentado y reensamblado automáticamente cuando supera el tamaño máximo permitido por un paquete.
- La integridad de cada fragmento se verifica mediante **CRC32**.
- Se utilizan **ACK** y **NACK** para confirmar recepciones correctas o solicitar retransmisiones inmediatas.
- El tiempo de espera (**RTO**) se adapta dinámicamente utilizando el algoritmo de **Jacobson-Karels**.
- Los mensajes incompletos se eliminan automáticamente después de un período de inactividad para evitar consumo innecesario de memoria.
- Cada flujo de comunicación se identifica mediante la llave `IP:PUERTO:SEQ`, permitiendo manejar múltiples clientes concurrentes de forma independiente.
