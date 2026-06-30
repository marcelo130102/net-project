#!/usr/bin/env bash
set -e

cd '/mnt/c/Users/INTEL/Documents/Universidad/2026-1/Redes/net-project'

echo "[SETUP] Entrando al repo..."
echo "[SETUP] Repo: C:\Users\INTEL\Documents\Universidad\2026-1\Redes\net-project"

if [ ! -d .venv ]; then
    echo "[SETUP] Creando entorno virtual..."
    python3 -m venv .venv
fi

echo "[SETUP] Activando entorno virtual..."
source .venv/bin/activate

echo "[SETUP] Instalando/verificando dependencias..."
python -m pip install -q --upgrade pip setuptools wheel pybind11 numpy pandas matplotlib scikit-learn torch

if ! ls cpp_python_example/modulo*.so >/dev/null 2>&1 || ! ls cpp_python_example/calculator*.so >/dev/null 2>&1; then
    echo "[SETUP] Compilando bindings C++..."
    cd cpp_python_example
    python setup.py build_ext --inplace
    cd ..
else
    echo "[SETUP] Bindings C++ ya compilados."
fi

echo "[SETUP] Dividiendo dataset en 6 partes..."
python dataset_splitter.py 'Dataset of Diabetes.csv' 6

echo "[SETUP] Preparación terminada."