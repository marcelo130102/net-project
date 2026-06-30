import os
import sys
import time
import threading
import numpy as np

# Path configuration
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CPP_DIR = os.path.join(BASE_DIR, "cpp_python_example")
sys.path.insert(0, CPP_DIR)

import modulo

def run_slave(barrier, results):
    try:
        print("[SLAVE] Starting and waiting for master registration...")
        # Wait a brief moment to ensure master bind is complete
        time.sleep(0.5)
        slave = modulo.NetSlave("127.0.0.1", 45000)
        print("[SLAVE] Registered successfully. Waiting for master matrix...")

        # 1. Receive matrix from master
        received_mat = slave.receive_matrix()
        print(f"[SLAVE] Matrix received from master: shape={received_mat.shape}")
        results['received_by_slave'] = received_mat

        # 2. Modify matrix and send it back to master
        sent_mat = received_mat * 2.0
        print(f"[SLAVE] Sending modified matrix back to master...")
        slave.send_matrix(sent_mat)
        results['sent_by_slave'] = sent_mat
        print("[SLAVE] Completed.")
    except Exception as e:
        print(f"[SLAVE ERROR] {e}")
        results['error_slave'] = e
    finally:
        barrier.wait()

def run_master(barrier, results):
    try:
        print("[MASTER] Starting on port 45000, expecting 1 slave...")
        master = modulo.NetMaster(45000, 1)
        print("[MASTER] Slave registered.")

        # 1. Send test matrix to slave 0
        original_mat = np.array([[1.5, 2.5, 3.5], [4.5, 5.5, 6.5]], dtype=np.float32)
        print(f"[MASTER] Sending original matrix:\n{original_mat}")
        master.send_matrix(0, original_mat)
        results['sent_by_master'] = original_mat

        # 2. Receive modified matrix back
        print("[MASTER] Waiting for modified matrix from slave 0...")
        received_mat = master.receive_matrix(0)
        print(f"[MASTER] Modified matrix received from slave 0:\n{received_mat}")
        results['received_by_master'] = received_mat
        print("[MASTER] Completed.")
    except Exception as e:
        print(f"[MASTER ERROR] {e}")
        results['error_master'] = e
    finally:
        barrier.wait()

def main():
    print("=== STARTING UDP MODULE RESILIENCY AND PROTOCOL VALIDATION ===")
    barrier = threading.Barrier(3)
    results = {}

    t_master = threading.Thread(target=run_master, args=(barrier, results))
    t_slave = threading.Thread(target=run_slave, args=(barrier, results))

    t_master.start()
    t_slave.start()

    # Wait for completion or timeout
    barrier.wait()

    t_master.join()
    t_slave.join()

    # Validation
    if 'error_master' in results:
        print(f"Master failed with error: {results['error_master']}")
        sys.exit(1)
    if 'error_slave' in results:
        print(f"Slave failed with error: {results['error_slave']}")
        sys.exit(1)

    sent_by_master = results.get('sent_by_master')
    received_by_slave = results.get('received_by_slave')
    sent_by_slave = results.get('sent_by_slave')
    received_by_master = results.get('received_by_master')

    assert np.allclose(sent_by_master, received_by_slave), "Matrix corrupted from Master to Slave!"
    assert np.allclose(sent_by_slave, received_by_master), "Matrix corrupted from Slave to Master!"

    print("\n[SUCCESS] Matrix transmission validation completed perfectly!")
    print(f"Sent by Master matches Received by Slave:\n{received_by_slave}")
    print(f"Sent by Slave matches Received by Master:\n{received_by_master}")
    print("=== ALL VALIDATIONS PASSED ===")

if __name__ == "__main__":
    main()
