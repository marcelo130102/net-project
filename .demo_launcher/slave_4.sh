#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'
source .venv/bin/activate

echo "[SLAVE 4] Ejecutando slave 4..."
echo "[SLAVE 4] Archivo: diabetes_slave_5.csv"
echo ""

python -u basicClasificacion_slave.py 4 diabetes_slave_5.csv

echo ""
echo "[SLAVE 4 TERMINADO O DETENIDO]"
exec bash