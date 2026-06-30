#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'
source .venv/bin/activate

echo "[SLAVE 2] Ejecutando slave 2..."
echo "[SLAVE 2] Archivo: diabetes_slave_3.csv"
echo ""

python -u basicClasificacion_slave.py 2 diabetes_slave_3.csv

echo ""
echo "[SLAVE 2 TERMINADO O DETENIDO]"
exec bash