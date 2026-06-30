import subprocess
import sys

res = subprocess.run([".venv/bin/python", "-u", "test_udp_module.py"], capture_output=True, text=True)
print("STDOUT:")
print(res.stdout)
print("STDERR:")
print(res.stderr)
print("EXIT CODE:", res.returncode)
