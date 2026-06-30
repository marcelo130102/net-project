#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'
source .venv/bin/activate

echo "[SLAVE 3] Ejecutando slave 3..."
echo "[SLAVE 3] Archivo: diabetes_slave_4.csv"
echo ""

python -u basicClasificacion_slave.py 3 diabetes_slave_4.csv

echo ""
echo "[SLAVE 3 TERMINADO O DETENIDO]"
exec bash