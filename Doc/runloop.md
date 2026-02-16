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

```
Byte 1: '&' (0x26)     — command header
Byte 2: Command Code    — ASCII character identifying the command
Byte 3: Command Index   — typically '0' or TCMID ('1'-'8')
Byte 4: #Bytes/Param    — bytes per parameter ('0'-'9')
Byte 5: #Params         — number of parameters ('0'-'9')
Byte 6: CR (0x0D)       — end of command word
Byte 7+: Parameter data  — binary, MSB first
```

#### Configuration Phase (20:55:10.803 – 20:55:14.577)

| # | Time Offset | Command | Hex | Params | PTI Reference |
|---|-------------|---------|-----|--------|---------------|
| 1 | +0.000 | `&`` 000 | `26 60 30 30 30 0d` | — | Status/version query (undocumented) |
| 2 | +1.703 | `&v` 042 | `26 76 30 34 32 0d` + 8 bytes | `00 00 00 73 00 0f 3a 9d` | **Set Timing** — Sampling Period = 115 μs, Intermission = 998,045 μs |
| 3 | +1.859 | `&L` 011 | `26 4c 30 31 31 0d` + 1 byte | `02` | **Signal Quality (SQR)** = 2 |
| 4 | +1.923 | `&O` 021 | `26 4f 30 32 31 0d` + 2 bytes | `00 02` | **Min Signal (MSR)** = 0x0002 |
| 5 | +1.986 | `&Y` A11 | `26 59 41 31 31 0d` + 1 byte | `08` | **Exposure Gain** = 8 |
| 6 | +2.111 | `&U` 011 | `26 55 30 31 31 0d` + 1 byte | `03` | **SOT Limit** = 3 |
| 7 | +2.176 | `&^` 011 | `26 5e 30 31 31 0d` + 1 byte | `0d` | **Tether Mode** = 0x0D |
| 8 | +2.239 | `&Q` A00 | `26 51 41 30 30 0d` | — | **Single Sampling** mode |
| 9 | +2.270 | `&p` 000 | `26 70 30 30 30 0d` | — | **Clear TFS** |
| 10–25 | +2.332 – +3.282 | `&p` 112 | `26 70 31 31 32 0d` + 2 bytes | `{01..10} 01` | **Append TFS** — 16 LEDs on TCM 1, 1 flash each |
| 26 | +3.347 | `&o` 000 | `26 6f 30 30 30 0d` | — | **Sync EOF** mode |
| 27 | +3.409 | `&X` 018 | `26 58 30 31 38 0d` + 8 bytes | `00 00 00 00 00 00 00 00` | **Multi-Rate Sampling** — all zeros (SM0) |
| 28 | +3.520 | `&r` 000 | `26 72 30 30 30 0d` | — | **Upload TFS** to TCMs |
| 29 | +3.710 | `&:` 000 | `26 3a 30 30 30 0d` | — | **Refraction OFF** |
| 30 | +3.774 | `&S` 000 | `26 53 30 30 30 0d` | — | **Internal Trigger** |

#### Start Measurement

| # | Time Offset | Command | Hex | Notes |
|---|-------------|---------|-----|-------|
| 31 | +3.857 | `&3` 000 | `26 33 30 30 30 0d` | **START Periodic Sampling** — no ACK generated (PTI manual p.44) |

#### Stop Measurement

| # | Time Offset | Command | Hex | Notes |
|---|-------------|---------|-----|-------|
| 32 | +15.240 | `&5` 000 | `26 35 30 30 30 0d` | **STOP Sampling** — 1st attempt |
| 33 | +16.756 | `&5` 000 | `26 35 30 30 30 0d` | **STOP Sampling** — 2nd attempt (redundant, ensures stop) |

### 2.3 Command/Response Pattern

Between each configuration command, the capture shows:

1. **PurgeComm** (PURGE_RXCLEAR) — flush stale RX data
2. **WriteFile** — send command bytes
3. **IOCTL_SERIAL_GET_COMMSTATUS** poll loop (~1ms interval) — wait for data in RX queue
4. **ReadFile** — read 19-byte ACK response

Each ACK response (19 bytes) follows the Message Set format (PTI Section 4.4, page 19):

```
Byte 1:     Command Code echo (e.g., 0x76 for &v)
Byte 2:     Command Index echo (e.g., 0x30 for '0')
Bytes 3-13: Reserved (zeros)
Byte 14:    Message Parameter (0x06)
Byte 15:    Message ID
Bytes 16-19: Check Bits (e0 e0 80 e0)
```

### 2.4 Timing Calculation

The `&v` command parameters decode as:
- **Sampling Period**: `00 00 00 73` = 115 μs per marker
- **Sequence Intermission**: `00 0f 3a 9d` = 998,045 μs

Total frame period = (16 channels × 115 μs) + 998,045 μs = 1,840 + 998,045 = **999,885 μs ≈ 1 Hz**

General formula for a desired frequency:
```
intermission_us = (1,000,000 / frequency_Hz) - (num_channels × sampling_period_us)
```

---

## 3. Data Set Format (PTI Manual Section 4.3, page 17)

Every measurement record is exactly **19 bytes**:

```
Bytes 1-4:   Timestamp    — unsigned 32-bit, microseconds since tracker boot
Bytes 5-7:   X coordinate — signed 24-bit, units of 10 μm (divide by 100 for mm)
Bytes 8-10:  Y coordinate — signed 24-bit, units of 10 μm
Bytes 11-13: Z coordinate — signed 24-bit, units of 10 μm
Bytes 14-17: Status Word  — signal quality and computation flags
Byte 18:     LEDID        — bit 7 always 1, bits 6-0 = LED ID (1-64)
Byte 19:     TCMID        — bits 7-4 = 0xE (1110), bits 3-0 = TCM ID (1-8)
```

### 3.1 Coordinate Parsing

Signed 24-bit extraction (MSB first):
```cpp
int32_t coord = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
if (coord & 0x800000) coord |= 0xFF000000; // sign-extend
double mm = coord / 100.0;
```

### 3.2 LEDID / TCMID Extraction

```cpp
uint8_t ledId = buffer[17] & 0x7F;  // mask off bit 7
uint8_t tcmId = buffer[18] & 0x0F;  // mask off upper nibble
```

### 3.3 Data Arrival Pattern (from IRP capture)

At 1 Hz with 16 channels:
- **Per burst**: 16 records × 19 bytes = 304 bytes
- **Burst interval**: ~1000 ms
- **Read pattern**: 2–3 ReadFile calls per burst (OS buffers variable amounts)
- The first burst after START arrives within ~1 ms of the `&3` command

---

## 4. Implementation Plan

### 4.1 File Structure

```
Measure_HHD.h    — public API (structs + 3 function declarations)
Measure_HHD.cpp  — implementation
```

### 4.2 Public API

```cpp
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
//   hPort         — open COM port handle (from detection phase)
//   frequencyHz   — desired measurement rate (1-4600 Hz, clamped to valid range)
//   numChannels   — number of LED channels to enable (1-16, on TCM 1)
// Returns a session handle, or nullptr on failure.
HHD_MeasurementSession* StartMeasurement(HANDLE hPort, int frequencyHz, int numChannels);

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

### 4.3 Internal Design

#### Session State

The `HHD_MeasurementSession` struct holds:
- `HANDLE hPort` — the COM port handle (not owned, caller manages lifetime)
- `int frequencyHz`, `int numChannels` — active configuration
- `std::vector<uint8_t> residualBuffer` — leftover bytes from partial reads
  (since ReadFile may return a non-multiple of 19 bytes)

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
   - `&p 112` + `{ch} 01` × numChannels — enable channels
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
uint32_t framePeriod_us  = 1000000 / frequencyHz;
uint32_t intermission_us = framePeriod_us - (numChannels * SAMPLING_PERIOD_US);
// Clamp intermission to minimum 0
```

Encode into `&v` command:
```
&v042<CR> [sampling_period as 4 bytes MSB] [intermission as 4 bytes MSB]
```

#### SendCommand Helper

Each configuration command follows the same pattern observed in the IRP capture:

1. `PurgeComm(hPort, PURGE_RXCLEAR)` — flush stale data
2. `WriteFile(hPort, cmdBytes, cmdLen)` — send command
3. Poll with `ClearCommError` checking `cbInQue` until ≥ 19 bytes available (max ~500ms)
4. `ReadFile(hPort, ackBuf, 19)` — read 19-byte ACK
5. Validate: `ackBuf[0]` should echo the command code

#### FetchMeasurements Flow

Called in a run loop, designed for non-blocking operation:

1. **Check available data**: `ClearCommError` → `comstat.cbInQue`
2. **ReadFile** all available bytes into a working buffer
3. **Prepend residual** bytes from previous call
4. **Parse complete 19-byte records**:
   - Extract timestamp (bytes 0-3, big-endian unsigned 32-bit)
   - Extract X/Y/Z (bytes 4-12, three signed 24-bit values)
   - Extract status (bytes 13-16, big-endian unsigned 32-bit)
   - Extract LEDID (byte 17, mask 0x7F) and TCMID (byte 18, mask 0x0F)
   - Convert coordinates to mm (÷ 100.0)
   - Append to output vector
5. **Save residual** (any trailing bytes < 19) for next call
6. **Return** count of new samples

#### StopMeasurement Flow

1. **Send stop**: `&5 000` — twice, with ~1.5s gap (matches IRP capture)
2. **Drain** any remaining data in the RX buffer
3. **Free** the session struct

### 4.4 Error Handling

- `SendCommand` returns false if WriteFile fails or ACK is not received within timeout
- `StartMeasurement` returns nullptr if any config command fails
- `FetchMeasurements` returns 0 on read errors (caller can check and stop)
- `StopMeasurement` returns false if both stop attempts fail to get acknowledgment

### 4.5 Thread Safety

The implementation is **not** thread-safe. The caller is responsible for ensuring
`FetchMeasurements` is not called concurrently with `StopMeasurement`. This matches
the single-threaded run-loop pattern requested.

---

## 5. IRP Capture Command-to-PTI Manual Cross-Reference

| IRP Command | PTI Manual Section | Page | Function |
|-------------|-------------------|------|----------|
| `&`` 000 | — | — | Undocumented (firmware query, sent before config) |
| `&v` 042 | 4.7.14 | 38 | Set Timing (Sampling Period + Intermission) |
| `&L` 011 | 4.7.4 | 28 | Signal Quality Requirement (SQR) |
| `&O` 021 | 4.7.5 | 29 | Minimum Signal Requirement (MSR) |
| `&Y` A11 | 4.7.3 | 27 | Auto-Exposure Feedback Gain |
| `&U` 011 | 4.7.6 | 30 | SOT (Sample Operation Time) Limit |
| `&^` 011 | 4.7.17 | 41 | Tether Mode |
| `&Q` A00 | 4.7.1 | 25 | Single Sampling |
| `&p` 000/112 | 4.7.8 | 32 | Program TFS (Clear / Append) |
| `&o` 000 | 4.7.10 | 34 | Sync EOF |
| `&X` 018 | 4.7.7 | 31 | Multi-Rate Sampling |
| `&r` 000 | 4.7.9 | 33 | Upload TFS |
| `&:` 000 | 4.7.12 | 36 | Refraction OFF |
| `&S` 000 | 4.8.2 | 44 | Internal Trigger |
| `&3` 000 | 4.8.1 | 43 | Start Periodic Sampling (no ACK) |
| `&5` 000 | 4.8.3 | 45 | Stop Sampling |
