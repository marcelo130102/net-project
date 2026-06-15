"""
mock_master.py

Simula al Master enviando un mensaje (un array de pesos en float32)
fragmentado segun el protocolo de protocol.hpp, para poder probar
worker.cpp localmente.

Uso:
    python3 mock_master.py [host] [port]

Por defecto host=127.0.0.1, port=9100 (DEFAULT_WORKER_PORT del worker).
"""

import socket
import struct
import sys
import zlib


DATAGRAM_SIZE = 500
PAYLOAD_OFFSET = 13
PAYLOAD_SIZE = 483
CRC_OFFSET = 496

ACK_SIZE = 12
ACK_TIMEOUT = 2.0
MAX_RETRIES = 5


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_data_packet(sequence: int, fragment: int, total_fragments: int, payload: bytes) -> bytes:
    data_size = len(payload)

    header = struct.pack(
        "<BIHHI",
        ord("D"),       # type
        sequence,       # uint32
        fragment,       # uint16
        total_fragments,  # uint16
        data_size,      # uint32
    )

    crc_input = (
        struct.pack("<B", ord("D"))
        + struct.pack("<I", sequence)
        + struct.pack("<H", fragment)
        + struct.pack("<H", total_fragments)
        + struct.pack("<I", data_size)
        + payload
    )

    crc = crc32(crc_input)

    packet = bytearray(DATAGRAM_SIZE)
    packet[0:len(header)] = header
    packet[PAYLOAD_OFFSET:PAYLOAD_OFFSET + data_size] = payload
    packet[CRC_OFFSET:CRC_OFFSET + 4] = struct.pack("<I", crc)

    return bytes(packet)


def parse_ack(buf: bytes):
    if len(buf) != ACK_SIZE or buf[0:1] != b"A":
        return None

    sequence, fragment, status, crc = struct.unpack("<IHBI", buf[1:])
    return sequence, fragment, status, crc


def send_with_retries(sock, addr, packet: bytes, sequence: int, fragment: int) -> bool:
    for attempt in range(1, MAX_RETRIES + 1):
        sock.sendto(packet, addr)

        try:
            sock.settimeout(ACK_TIMEOUT)
            resp, _ = sock.recvfrom(ACK_SIZE)
        except socket.timeout:
            print(f"  [seq={sequence} frag={fragment}] timeout, intento {attempt}")
            continue

        ack = parse_ack(resp)

        if ack is None:
            print(f"  [seq={sequence} frag={fragment}] respuesta no reconocida")
            continue

        ack_seq, ack_frag, status, _ = ack

        if ack_seq != sequence or ack_frag != fragment:
            continue

        if status == 1:
            print(f"  [seq={sequence} frag={fragment}] ACK")
            return True

        print(f"  [seq={sequence} frag={fragment}] NACK, reintentando")

    return False


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9100

    # Simula un vector de pesos (float32), p.ej. salida de model_to_vector()
    weights = [float(i) * 0.01 for i in range(1000)]
    raw = struct.pack(f"<{len(weights)}f", *weights)

    fragments = [raw[i:i + PAYLOAD_SIZE] for i in range(0, len(raw), PAYLOAD_SIZE)]
    total_fragments = len(fragments)

    sequence = 1

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = (host, port)

    print(f"Enviando {len(weights)} floats en {total_fragments} fragmentos a {addr}")

    for frag_idx, frag_payload in enumerate(fragments):
        packet = build_data_packet(sequence, frag_idx, total_fragments, frag_payload)

        ok = send_with_retries(sock, addr, packet, sequence, frag_idx)

        if not ok:
            print(f"Fallo enviando fragmento {frag_idx}, abortando")
            sock.close()
            return

    print("Mensaje completo enviado y confirmado por el worker.")
    sock.close()


if __name__ == "__main__":
    main()
