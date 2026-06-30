#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'
source .venv/bin/activate

echo "[MASTER] Ejecutando master con 5 slaves..."
echo "[MASTER] Archivo: diabetes_master.csv"
echo ""

python -u basicClasificacion_master.py 5 diabetes_master.csv

echo ""
echo "[MASTER TERMINADO O DETENIDO]"
exec bash