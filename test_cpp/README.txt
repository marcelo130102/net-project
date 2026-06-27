# Capa de Transporte Confiable (RDT) sobre UDP
### Optimizada para Aprendizaje Federado Masivo

Este módulo contiene la implementación del protocolo de transferencia confiable de datos (RDT) sobre el protocolo no orientado a conexión UDP (Jurisdicción: Persona A). Está diseñado específicamente para soportar el intercambio masivo, concurrente y ordenado de matrices de pesos neuronales entre múltiples nodos esclavos y un nodo maestro.

---

## Componentes del Entregable Central (`protocol.hpp` y `protocol.cpp`)

Este componente constituye el núcleo del sistema de transporte. Toda la lógica matemática de red, control de flujo, integridad de datos y gestión dinámica de memoria reside aquí.

### Matriz de Optimizaciones e Implementaciones

| Característica / Mecanismo | Descripción Técnica | Impacto en el Sistema |
| :--- | :--- | :--- |
| **Algoritmo Jacobson-Karels** | Cálculo dinámico del RTO (Timeout) basado en `EstimatedRTT` y `DevRTT`. | Evita esperas infinitas o subdesbordamientos. Acota el RTO entre **100 ms** y **2000 ms** ($BACKOFF\_MAX\_MS$). |
| **Recolector de Mensajes Zombi** | Función `cleanupZombieMessages()` que monitorea la marca de tiempo `lastActivity`. | **Gestión de RAM:** Purga automáticamente cualquier flujo incompleto inactivo por más de 15s, previniendo *memory leaks*. |
| **Asignación Previa de Memoria** | Uso de la instrucción `reserve()` antes de la reconstrucción final del mensaje unificado. | **Optimización de CPU:** Asigna el bloque exacto en la RAM de una sola vez, evitando congelamientos del Máster por redimensionamientos continuos. |
| **Control de Integridad y NACKs** | Verificación mediante algoritmo CRC32 sobre datos binarios con soporte para `ACK_ERROR`. | Si un datagrama llega corrupto, el Servidor dispara un NACK. El cliente retransmite al instante sin esperar el timeout físico. |

---

## Entorno de Validación Simulada y Estrés

> [!NOTE]  
> Los siguientes archivos **no forman parte del software de producción final**. Constituyen el banco de pruebas aislado desarrollado para validar la robustez e inmunidad a fallos del protocolo antes de su acoplamiento con Python.

### Archivos del Banco de Pruebas

* **`client.cpp` & `server.cpp` (Simuladores de IA):** Las funciones `main()` actúan como generadores de carga. Recrean con precisión la fragmentación *Stop-and-Wait* y el desempaquetado de memoria cruda de tensores reales (arreglos binarios de tipo `float32` de **~331 KB**) emulando múltiples épocas sucesivas.
* **`test_carga.sh` (Orquestador de Concurrencia):** Script en Bash que automatiza las pruebas lanzando un Servidor y 5 Clientes simultáneos en segundo plano para validar el aislamiento por llaves unificadas (`IP:PUERTO:SEQ`).

### Guía de Ejecución Paso a Paso

```bash
# 1. Compilar el entorno de pruebas
g++ server.cpp protocol.cpp -o server
g++ client.cpp protocol.cpp -o client

# 2. Asignar permisos y lanzar la simulación concurrente
chmod +x test_carga.sh
./test_carga.sh
