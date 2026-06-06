# Network and Communications Final Project
---
The present project aims to use a neural network to process data and send this data by implementing a reliable data transfer (RDT) over UDP using different types of protocols that will be implemented.

## Members
- Calle, Maria
- Fuentes, Rodrigo
- Surco, Marcelo
- Valenzuela, Luigi

## Protocols
For now, the following optimized protocol structures are proposed for sending all the corresponding data. Fields have been rearranged to establish a fixed-size header at the beginning of each frame, avoiding dynamic offset calculations in C++ and maximizing transmission efficiency:

### Normal Data
| 1 B | 4 B | 2 B | 2 B | 4 B | Variable | 4 B |
|---|---|---|---|---|
| D (Type) | Sequence number | Fragment Number | Total Fragments |  Size of data | Data | Hash(CRC32) |

### ACK
| 1 B | 4 B | 4 B |
|---|---|---|
| A (Type) | Sequence number | Hash(CRC32) |

### NACK
| 1 B | 4 B | 4 B |
|---|---|---|
| N (Type) | Sequence number | Hash(CRC32) |

> Note: In all cases, when using fixed-size datagrams of 500 bytes, the missing byte field will have to be filled with padding.

---

## Technical Specifications and RDT Mechanics

### 1. Integrity and Corruption Control (CRC32 Implementation)

To optimize transmission efficiency and maintain a high goodput within the 500-byte fixed-size datagram restriction, the **4 B Hash** field at the end of each frame (Normal Data, ACK, and NACK) will be implemented using **CRC32 (Cyclic Redundancy Check)**.

* **Size Allocation:** 4 bytes (32 bits), perfectly matching the designated trailing field in the protocol structures.
* **Overhead Impact:** It accounts for only **0.8%** of the total 500-byte datagram capacity.
* **Header Architecture:** By removing the redundant sequence number size field and making the sequence number a fixed 4-byte integer (`uint32_t`), the frame establishes a rigid layout. In the C++ Worker, the first 9 bytes of any incoming packet can be directly mapped to a structure template without complex pointer arithmetic or dynamic memory offsets.
* **Justification:** Heavy cryptographic hashes (such as MD5 or SHA-256) were discarded to prevent unnecessary computational bottlenecks within the neural network's distributed training loop. CRC32 provides robust mathematical error detection against bit alterations caused by UDP network noise while maintaining near-instantaneous serialization/deserialization execution times in both Python (Master) and C++ (Workers).
* **Data Integrity Workflow:** The sender computes the CRC32 checksum over all preceding packet fields. The receiver recalculates the checksum upon packet arrival. If a mismatch occurs, corruption is detected, the payload is safely discarded, and a **NACK** frame is triggered.

### 2. Loss Control: Dynamic Timeout Algorithm

To handle potential packet dropouts without introducing rigid latency bottlenecks, the Master architecture incorporates a hybrid approach combining a TCP-style dynamic timeout mechanism (**Jacobson-Karels Algorithm**) with an **Exponential Backoff** safety net. 

Since the environment operates locally or via a local network (LAN), fixed timeouts (e.g., 1 second) would severely degrade the synchronization performance of neural network weight updates.

#### 2.1. Dynamic Timeout Calculation (TCP-Style)
The Master node continuously tracks the Round Trip Time (RTT) of every non-retransmitted packet to adjust the retransmission timer dynamically using the following calculations:

* **Sample RTT Measurement:**
  `SampleRTT = ACK_Reception_Time - DATA_Transmission_Time`
  *(Note: Retransmitted frames are omitted from measurements to avoid Karn's ambiguity).*

* **Smoothed Estimated RTT:**
  An Exponentially Weighted Moving Average (EWMA) filter is applied with alpha = 0.125 to weight historical network states and smooth out abrupt variance:
  `EstimatedRTT = (1 - 0.125) * EstimatedRTT + 0.125 * SampleRTT`

* **RTT Deviation:**
  Measures current network variance with beta = 0.25:
  `DevRTT = (1 - 0.25) * DevRTT + 0.25 * |SampleRTT - EstimatedRTT|`

* **Final Timeout Interval (RTO):**
  Sets the active timer threshold, factoring in a safety margin proportional to network jitter:
  `Timeout = EstimatedRTT + 4 * DevRTT`

#### 2.2. Backup Mechanism: Exponential Backoff
If a local network bottleneck or thread starvation occurs on a C++ Worker node, the packet timer will expire (`Timeout`) before an ACK arrives.

* The unacknowledged frame is immediately retransmitted.
* To mitigate network stress or worker overload, the active timer value doubles exponentially: `New_Timeout = Current_Timeout * 2`.
* This exponential growth is capped at a strict threshold ceiling of **2000 ms** to prevent the connection state from becoming unresponsive.
* Upon successfully receiving the next valid ACK frame, the timer **instantly resets** to the dynamic baseline calculated by the Jacobson-Karels algorithm.
