#!/bin/bash
echo "=== INICIANDO SIMULACIÓN DE APRENDIZAJE FEDERADO ==="

# 1. Levantar el servidor en segundo plano
./server.exe &
SERVER_PID=$!

echo "[BASH] Servidor levantado (PID: $SERVER_PID). Esperando Grace Period..."
sleep 2

# 2. Levantar 5 clientes simultáneos en segundo plano
echo "[BASH] Lanzando 5 clientes simultáneos..."
for i in {1..5}; do
    ./client.exe &
done

# Esperar a que los clientes terminen
wait

echo "=== SIMULACIÓN TERMINADA ==="
# Matar al servidor al terminar
kill $SERVER_PID