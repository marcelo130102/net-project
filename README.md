# Network and Communications Final Project
---
The present project aims to use a neural network to process data and send this data by implementing a reliable data transfer (RDT) over UDP using different types of protocols that will be implemented.

## Members
- Calle, Maria
- Fuentes, Rodrigo
- Surco, Marcelo
- Valenzuela, Luigi

## Protocols
For now, the following protocol structures are proposed

### Normal Data
|1 B|4 B|Variable|4 B|Variable|4 B|
|---|---|---|---|---|---|
|D|Size of data|Data|Size of sequence number|Sequence number|Hash|

### ACK
|1 B|4 B|Variable|4 B|
|---|---|---|---|
|A|Size of sequence number|Sequence number|Hash|

### NACK
|1 B|4 B|Variable|4 B|
|---|---|---|---|
|N|Size of sequence number|Sequence number|Hash|

Note: In all cases, when using fixed-size datagrams of 500 bytes, the missing byte field will have to be filled with padding.
