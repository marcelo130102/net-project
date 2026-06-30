#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'
source .venv/bin/activate

echo "[SLAVE 0] Ejecutando slave 0..."
echo "[SLAVE 0] Archivo: diabetes_slave_1.csv"
echo ""

python -u basicClasificacion_slave.py 0 diabetes_slave_1.csv

echo ""
echo "[SLAVE 0 TERMINADO O DETENIDO]"
exec bash