# Visualeyez VZ10K/10K5 Serial Communication Protocol Analysis

**Target Application:** `Multi-Tracker Detection_x64.exe`
[cite_start]**Hardware:** Phoenix Technologies Visualeyez VZ10K/VZ10K5 Tracker [cite: 3]
[cite_start]**Interface:** RS-422 Serial over PCIe (COM9) [cite: 142, 143]

---

## 1. Core Communication Parameters
[cite_start]To successfully communicate with the tracker, the host computer's serial port must use the following baseline parameters[cite: 148, 149]:
* [cite_start]**Data Bits:** 8 [cite: 149]
* **Parity:** None (N)
* [cite_start]**Stop Bits:** 1 [cite: 149]
* **Hardware Flow Control:** DTR/RTS Enabled

[cite_start]**Baud Rate Note:** The baud rate dynamically shifts during the initialization phase, starting at `2,000,000` (2 Mbps) for the hardware reset, before ramping up to the mandatory operational speed of `2,500,000` (2.5 Mbps)[cite: 149].

---

## 2. Hardware Initialization & Handshake Sequence

The application executes a highly specific 4-phase sequence to reset, identify, and prepare the tracker for high-speed 3D motion capture streaming.

### Phase 1: Hardware Reset via DTR
* **Action:** The host opens the port at 2 Mbps and rapidly toggles the DTR (Data Terminal Ready) line.
* [cite_start]**Mechanism:** The tracker's DB9 Data Connector utilizes Pin 2 as the Hardware RESET pin[cite: 146, 147]. [cite_start]By pulling this pin low via the serial control lines, the application forces the core processor to reset the tracker hardware[cite: 147, 413].

### Phase 2: Polling for the "Initial Message Set"
* [cite_start]**Action:** The host waits approximately 1.5 seconds for the hardware to reboot[cite: 416].
* [cite_start]**Mechanism:** Immediately after a power-up or hardware reset, the tracker automatically outputs a 19-byte "Initial Message Set" to the host[cite: 375].
* [cite_start]**Payload:** This 19-byte packet contains an initialization confirmation code (`01h` on Byte 15) and embeds the Tracker's Serial Number across Bytes 5 through 12[cite: 377, 378]. [cite_start]Every data and message output from the tracker is strictly 19 bytes long[cite: 175, 176].

### Phase 3: Host Acknowledgment Command
* **Action:** Instantly after receiving the 19-byte boot message, the host application transmits a 6-byte command payload: `26 3f 31 30 30 0d` (Hex) / `&?100\r` (ASCII).
* [cite_start]**Mechanism:** This perfectly aligns with the proprietary Visualeyez command format[cite: 247, 250, 251]:
    * [cite_start]`&` (0x26): Command Header (Start of command) [cite: 251]
    * `?` (0x3f): Command Code
    * `1` (0x31): Command Index 
    * [cite_start]`0` (0x30): Number of bytes per parameter [cite: 251]
    * [cite_start]`0` (0x30): Number of parameters to follow [cite: 251]
    * [cite_start]`<CR>` (0x0d): Carriage Return (End of command) [cite: 251]

### Phase 4: Switching to Production Speed
* **Action:** Following the command acknowledgment, the host reconfigures the COM port to `2,500,000` baud.
* [cite_start]**Mechanism:** This speed shift is required by the manufacturer's operational specifications to handle the massive data throughput needed for 10,000 Hz target sampling[cite: 33, 149].
* **Result:** The system issues a `PURGE` command to clear the RX/TX buffers and begins streaming 19-byte target coordinate sets.

---

## 3. Data Set Structure (Operational Mode)
[cite_start]Once operational, every 19-byte data packet sent from the tracker to the host contains[cite: 290, 291]:
* [cite_start]**Bytes 1-4:** Timestamp (in microseconds) [cite: 293]
* [cite_start]**Bytes 5-7:** X Coordinate (in 10µm units) [cite: 297, 298]
* [cite_start]**Bytes 8-10:** Y Coordinate (in 10µm units) [cite: 297, 298]
* [cite_start]**Bytes 11-13:** Z Coordinate (in 10µm units) [cite: 297, 298]
* [cite_start]**Bytes 14-17:** Status Word (Signal quality and computation flags) [cite: 302]
* [cite_start]**Byte 18:** LED Identifier (LEDID) [cite: 331]
* [cite_start]**Byte 19:** Target Control Module Identifier (TCMID) [cite: 342]