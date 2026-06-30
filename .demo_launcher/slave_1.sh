#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'
source .venv/bin/activate

echo "[SLAVE 1] Ejecutando slave 1..."
echo "[SLAVE 1] Archivo: diabetes_slave_2.csv"
echo ""

python -u basicClasificacion_slave.py 1 diabetes_slave_2.csv

echo ""
echo "[SLAVE 1 TERMINADO O DETENIDO]"
exec bash