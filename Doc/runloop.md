# Measurement Run Loop — Protocol Analysis & Implementation Plan

## 1. Overview

This document analyzes the serial communication protocol for running measurements on the
Visualeyez VZ10K tracker, based on:

- **IRP Capture**: `Data/Measure_1H_10sec.txt` — a complete 1Hz measurement session (10 seconds,
  16 channels) captured with HHD Device Monitoring Studio
- **PTI Low Level Control Kit Manual**: `Doc/PTI LOW LEVEL CONTROL KIT FEB 2022 - CONFIDENTIAL.PDF`
  — the definitive command reference (Section 4, pages 14–54)
- **Existing codebase**: `Detect_HHD.cpp` — the detection module whose patterns we follow

The goal is to implement three routines: **StartMeasurement**, **FetchMeasurements** (run-loop),
and **StopMeasurement**.

---

## 2. IRP Capture Analysis

### 2.1 Capture Statistics

| Metric | Value |
|--------|-------|
| Total text lines | 139,532 |
| Write (DOWN) IRPs | 33 |
| Read (UP) IRPs | 67 |
| COMMSTATUS poll IRPs | ~23,198 |
| Capture duration | ~25 seconds (20:55:04 – 20:55:29) |
| Measurement window | ~11 seconds (20:55:14.660 – 20:55:25.671) |

### 2.2 Command Sequence (All 33 Writes)

Every command follows the PTI format (Section 4.2, page 14):

| Byte | Field | Value |
|:---|:---|:---|
| 1 | Command header | `&` (0x26), always fixed |
| 2 | Command Code | ASCII character (e.g. `L`, `3`, `p`) |
| 3 | Command Index | `0` if unused, or TCMID `1`–`8` |
| 4 | Bytes/Param | Bytes per parameter (`0`–`9`) |
| 5 | Num Params | Number of parameters (`0`–`9`) |
| 6 | Terminator | CR (0x0D) |
| 7+ | Param data | Binary, MSB first (optional) |

#### Configuration Phase (20:55:10.803 – 20:55:14.577)

| # | Offset | Command | Description |
|:---|:---|:---|:---|
| 1 | +0.000 | `` &` 000 `` | Status/version query (undocumented) |
| 2 | +1.703 | `&v 042` | **Set Timing** — Period = 115 μs, Intermission = 998,045 μs |
| 3 | +1.859 | `&L 011` | **Signal Quality (SQR)** = 2 |
| 4 | +1.923 | `&O 021` | **Min Signal (MSR)** = 0x0002 |
| 5 | +1.986 | `&Y A11` | **Exposure Gain** = 8 |
| 6 | +2.111 | `&U 011` | **SOT Limit** = 3 |
| 7 | +2.176 | `&^ 011` | **Tether Mode** = 0x0D |
| 8 | +2.239 | `&Q A00` | **Single Sampling** mode |
| 9 | +2.270 | `&p 000` | **Clear TFS** |
| 10–25 | +2.332 | `&p 112` ×16 | **Append TFS** — 16 LEDs on TCM 1, 1 flash each |
| 26 | +3.347 | `&o 000` | **Sync EOF** mode |
| 27 | +3.409 | `&X 018` | **Multi-Rate Sampling** — all zeros (SM0) |
| 28 | +3.520 | `&r 000` | **Upload TFS** to TCMs |
| 29 | +3.710 | `&: 000` | **Refraction OFF** |
| 30 | +3.774 | `&S 000` | **Internal Trigger** |

#### Start Measurement

| # | Offset | Command | Description |
|:---|:---|:---|:---|
| 31 | +3.857 | `&3 000` | **START Periodic Sampling** — no ACK generated |

#### Stop Measurement

| # | Offset | Command | Description |
|:---|:---|:---|:---|
| 32 | +15.240 | `&5 000` | **STOP Sampling** — 1st attempt |
| 33 | +16.756 | `&5 000` | **STOP Sampling** — 2nd attempt (ensures stop) |

### 2.3 Command/Response Pattern

Between each configuration command, the capture shows:

| Step | Win32 Call | Purpose |
|:---|:---|:---|
| 1 | `PurgeComm(PURGE_RXCLEAR)` | Flush stale RX data |
| 2 | `WriteFile` | Send command bytes |
| 3 | `IOCTL_SERIAL_GET_COMMSTATUS` poll (~1 ms) | Wait for data in RX queue |
| 4 | `ReadFile` (19 bytes) | Read ACK response |

Each ACK response (19 bytes) follows the Message Set format (PTI Section 4.4, page 19):

| Bytes | Field | Example |
|:---|:---|:---|
| 1 | Command Code echo | `0x76` for `&v` |
| 2 | Command Index echo | `0x30` for `0` |
| 3–13 | Reserved | zeros |
| 14 | Message Parameter | `0x06` |
| 15 | Message ID | — |
| 16–19 | Check Bits | `e0 e0 80 e0` |

### 2.4 Timing Calculation

The `&v` command parameters decode as:

| Parameter | Hex | Decoded | Unit |
|:---|:---|:---|:---|
| Sampling Period | `00 00 00 73` | 115 | μs per marker |
| Sequence Intermission | `00 0f 3a 9d` | 998,045 | μs |

Total frame period = (16 channels × 115 μs) + 998,045 μs = 1,840 + 998,045 = **999,885 μs ≈ 1 Hz**

General formula for a desired frequency:

| Variable | Formula |
|:---|:---|
| `total_flashes` | Sum of `flashCount` across all markers in the TFS |
| `frame_period_us` | `1,000,000 / frequency_Hz` |
| `intermission_us` | `frame_period_us - (total_flashes × sampling_period_us)` |

---

## 3. Data Set Format (PTI Manual Section 4.3, page 17)

Every measurement record is exactly **19 bytes**:

| Bytes | Field | Encoding | Range |
|:---|:---|:---|:---|
| 1–4 | Timestamp | Unsigned 32-bit, big-endian | μs since tracker boot |
| 5–7 | X coordinate | Signed 24-bit, big-endian | ÷100 → mm |
| 8–10 | Y coordinate | Signed 24-bit, big-endian | ÷100 → mm |
| 11–13 | Z coordinate | Signed 24-bit, big-endian | ÷100 → mm |
| 14–17 | Status Word | Unsigned 32-bit, big-endian | Signal quality flags |
| 18 | LEDID | Bit 7 = 1, bits 6–0 = ID | 1–64 |
| 19 | TCMID | Bits 7–4 = 0xE, bits 3–0 = ID | 1–8 |

### 3.1 Coordinate Parsing

Signed 24-bit extraction (MSB first):
```cpp
int32_t coord = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
if (coord & 0x800000) coord |= 0xFF000000; // sign-extend
double mm = coord / 100.0;
```

### 3.2 LEDID / TCMID Extraction

| Field | Expression | Mask | Result |
|:---|:---|:---|:---|
| LED ID | `buffer[17] & 0x7F` | Mask off bit 7 | 1–64 |
| TCM ID | `buffer[18] & 0x0F` | Mask off upper nibble | 1–8 |

### 3.3 Data Arrival Pattern (from IRP capture)

At 1 Hz with 16 channels:

| Metric | Value |
|:---|:---|
| Per burst | 16 records × 19 bytes = 304 bytes |
| Burst interval | ~1000 ms |
| Read pattern | 2–3 ReadFile calls per burst (OS buffers vary) |
| First burst latency | < 1 ms after `&3` START command |

---

## 4. Implementation Plan

### 4.1 File Structure

```
Measure_HHD.h    — public API (structs + 3 function declarations)
Measure_HHD.cpp  — implementation
```

### 4.2 Public API

```cpp
// A single marker entry for the Target Flashing Sequence (TFS).
// Maps directly to a &p append command (PTI manual Section 4.7.8).
struct HHD_MarkerEntry
{
    uint8_t tcmId;      // TCM module ID (1-8)
    uint8_t ledId;      // LED marker ID (1-64)
    uint8_t flashCount; // flashes per cycle (1-255, typically 1)
};

// A single parsed measurement sample
struct HHD_MeasurementSample
{
    uint32_t timestamp_us;  // microseconds since tracker boot
    double   x_mm;          // X in millimeters
    double   y_mm;          // Y in millimeters
    double   z_mm;          // Z in millimeters
    uint32_t status;        // status word (signal quality flags)
    uint8_t  ledId;         // LED marker ID (1-64)
    uint8_t  tcmId;         // TCM module ID (1-8)
};

// Opaque handle for an active measurement session
struct HHD_MeasurementSession;

// Start a measurement session on an already-detected port.
// Configures the tracker and begins periodic sampling at the given frequency.
//   hPort       — open COM port handle (from detection phase)
//   frequencyHz — desired measurement rate (1-4600 Hz, clamped)
//   markers     — TFS entries: which markers on which TCMs to sample.
//                 Each entry maps to one &p append command.
// Returns a session handle, or nullptr on failure.
HHD_MeasurementSession* StartMeasurement(HANDLE hPort, int frequencyHz,
                                          const std::vector<HHD_MarkerEntry>& markers);

// Fetch available measurement samples from the serial buffer.
// Call this in a run loop (non-blocking if no data available).
//   session — active measurement session
//   samples — output vector, appended with any new samples
// Returns the number of new samples appended (0 if none available).
int FetchMeasurements(HHD_MeasurementSession* session, std::vector<HHD_MeasurementSample>& samples);

// Stop the measurement session and clean up.
//   session — active measurement session (freed on return)
// Returns true if stop command was acknowledged.
bool StopMeasurement(HHD_MeasurementSession* session);
```

**Example: 16 LEDs on TCM 1 (equivalent to IRP capture)**
```cpp
std::vector<HHD_MarkerEntry> markers;
for (uint8_t i = 1; i <= 16; i++)
    markers.push_back({1, i, 1}); // TCM 1, LED i, 1 flash
auto* session = StartMeasurement(hPort, 1, markers);
```

**Example: markers across multiple TCMs**
```cpp
std::vector<HHD_MarkerEntry> markers = {
    {1, 1, 1}, {1, 2, 1}, {1, 3, 1}, {1, 4, 1},  // TCM 1, LEDs 1-4
    {5, 8, 3}, {5, 10, 1}, {5, 6, 2},              // TCM 5, LEDs 8/10/6
};
auto* session = StartMeasurement(hPort, 10, markers); // 10 Hz
```

### 4.3 Internal Design

#### Session State

The `HHD_MeasurementSession` struct holds:

| Field | Type | Description |
|:---|:---|:---|
| `hPort` | `HANDLE` | COM port handle (not owned, caller manages lifetime) |
| `frequencyHz` | `int` | Active measurement frequency |
| `markers` | `vector<HHD_MarkerEntry>` | The TFS configuration |
| `residual` | `vector<uint8_t>` | Leftover bytes from partial reads (< 19 bytes) |

#### StartMeasurement Flow

Replicates the exact IRP capture sequence:

1. **Purge** RX/TX buffers
2. **Set timeouts** — short read timeout for responsive polling
3. **Send configuration commands** (with ACK wait between each):
   - `&`` 000` — status query
   - `&v 042` + timing params — set sampling period + intermission for desired frequency
   - `&L 011` + `02` — signal quality
   - `&O 021` + `00 02` — min signal
   - `&Y A11` + `08` — exposure gain
   - `&U 011` + `03` — SOT limit
   - `&^ 011` + `0d` — tether mode
   - `&Q A00` — single sampling
   - `&p 000` — clear TFS
   - `&p {tcmId}12` + `{ledId} {flashCount}` × each marker entry — program TFS
   - `&o 000` — sync EOF
   - `&X 018` + 8 zeros — multi-rate SM0
   - `&r 000` — upload TFS
   - `&: 000` — refraction off
   - `&S 000` — internal trigger
4. **Send start**: `&3 000`
5. **Return** session handle

#### Timing Calculation

```cpp
const uint32_t SAMPLING_PERIOD_US = 115; // per-marker, from IRP capture
uint32_t totalFlashes    = sum of flashCount across all markers;
uint32_t framePeriod_us  = 1000000 / frequencyHz;
uint32_t activeTime_us   = totalFlashes * SAMPLING_PERIOD_US;
uint32_t intermission_us = (framePeriod_us > activeTime_us) ? (framePeriod_us - activeTime_us) : 0;
```

Encode into `&v` command:
```
&v042<CR> [sampling_period as 4 bytes MSB] [intermission as 4 bytes MSB]
```

#### SendCommand Helper

Each configuration command follows the same pattern observed in the IRP capture:

| Step | Call | Purpose |
|:---|:---|:---|
| 1 | `PurgeComm(PURGE_RXCLEAR)` | Flush stale data |
| 2 | `WriteFile(cmdBytes, cmdLen)` | Send command |
| 3 | `ClearCommError` poll (≥ 19 bytes, max 500 ms) | Wait for ACK in RX queue |
| 4 | `ReadFile(ackBuf, 19)` | Read 19-byte ACK |
| 5 | Validate `ackBuf[0]` == command code | Confirm correct echo |

#### FetchMeasurements Flow

Called in a run loop, designed for non-blocking operation:

| Step | Action | Details |
|:---|:---|:---|
| 1 | Check available data | `ClearCommError` → `comstat.cbInQue` |
| 2 | Read bytes | `ReadFile` all available bytes into working buffer |
| 3 | Prepend residual | Merge leftover bytes from previous call |
| 4 | Parse 19-byte records | Timestamp, X/Y/Z (÷100 → mm), status, LED ID, TCM ID |
| 5 | Save residual | Store any trailing bytes < 19 for next call |
| 6 | Return | Count of new samples appended |

#### StopMeasurement Flow

| Step | Action | Details |
|:---|:---|:---|
| 1 | Send `&5 000` | STOP — 1st attempt |
| 2 | Sleep 1.5 s | Gap between attempts (matches IRP capture) |
| 3 | Send `&5 000` | STOP — 2nd attempt (ensures stop) |
| 4 | Drain RX buffer | `PurgeComm(PURGE_RXCLEAR)` |
| 5 | Free session | Delete `HHD_MeasurementSession` struct |

### 4.4 Error Handling

| Function | Failure return | Condition |
|:---|:---|:---|
| `SendCommand` | `false` | WriteFile fails or ACK not received within timeout |
| `StartMeasurement` | `nullptr` | Any configuration command fails |
| `FetchMeasurements` | `0` | Read error (caller can check and stop) |
| `StopMeasurement` | `false` | Both stop attempts fail to get acknowledgment |

### 4.5 Thread Safety

The implementation is **not** thread-safe. The caller is responsible for ensuring
`FetchMeasurements` is not called concurrently with `StopMeasurement`. This matches
the single-threaded run-loop pattern requested.

---

## 5. IRP Capture Command-to-PTI Manual Cross-Reference

| Command | Args | Section | Page | Function |
|:---|:---|:---|:---|:---|
| `` &` `` | `000` | — | — | Undocumented firmware query |
| `&v` | `042` | 4.7.14 | 38 | Set Timing |
| `&L` | `011` | 4.7.4 | 28 | Signal Quality (SQR) |
| `&O` | `021` | 4.7.5 | 29 | Min Signal (MSR) |
| `&Y` | `A11` | 4.7.3 | 27 | Auto-Exposure Gain |
| `&U` | `011` | 4.7.6 | 30 | SOT Limit |
| `&^` | `011` | 4.7.17 | 41 | Tether Mode |
| `&Q` | `A00` | 4.7.1 | 25 | Single Sampling |
| `&p` | `000` / `112` | 4.7.8 | 32 | Program TFS (Clear / Append) |
| `&o` | `000` | 4.7.10 | 34 | Sync EOF |
| `&X` | `018` | 4.7.7 | 31 | Multi-Rate Sampling |
| `&r` | `000` | 4.7.9 | 33 | Upload TFS |
| `&:` | `000` | 4.7.12 | 36 | Refraction OFF |
| `&S` | `000` | 4.8.2 | 44 | Internal Trigger |
| `&3` | `000` | 4.8.1 | 43 | Start Periodic Sampling |
| `&5` | `000` | 4.8.3 | 45 | Stop Sampling |
