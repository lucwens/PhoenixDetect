# ProcMon Logfile Analysis: COM9 Communication Parameters

## Overview

**File Analyzed:** Logfile.CSV (Process Monitor export)
**Application:** Multi-Tracker Detection_x64.exe (Phoenix Technologies Visualeyez VZSoft 3.0.2_x64)
**Process IDs:** 13396, 10048, 6456
**Log Dimensions:** 3,551 rows × 13 columns

---

## 1. Application-Level Parameters (VZServer Registry)

**Registry Key:** `HKLM\SOFTWARE\Phoenix Technologies Inc.\VZServer\Parameters`
**Read by:** PID 10048 at 6:04:45.9373–9374 PM
**Log Rows:** 633–640

| Parameter | Value | Type | Interpretation |
|---|---|---|---|
| **Host Connection** | **16** | REG_DWORD | Likely the COM port number (COM16) or a connection type identifier |
| **Comm Rate** | **3** | REG_DWORD | Baud rate index — most likely maps to **57,600 baud** (if 0=9600) or **9,600 baud** (if 0=1200) |
| Priority | 128 | REG_DWORD | Thread/process priority |
| Jumping Filter | 1 | REG_DWORD | Motion data filter enabled |
| Jumping Filter Constant | 300, 300, 300 | REG_BINARY (12 bytes: `2C 01 00 00` ×3) | Filter threshold for X, Y, Z axes |
| Adaptive Calibration | 1 | REG_DWORD | Auto-calibration enabled |

### Comm Rate Interpretation

The "Comm Rate" value of **3** is an index into a baud rate table. Without VZSoft's internal documentation, two common mappings are possible:

**Mapping A (starting at 9600):**

| Index | Baud Rate |
|---|---|
| 0 | 9,600 |
| 1 | 19,200 |
| 2 | 38,400 |
| **3** | **57,600** ← |
| 4 | 115,200 |
| 5 | 230,400 |

**Mapping B (starting at 1200):**

| Index | Baud Rate |
|---|---|
| 0 | 1,200 |
| 1 | 2,400 |
| 2 | 4,800 |
| **3** | **9,600** ← |
| 4 | 19,200 |
| 5 | 38,400 |
| 6 | 57,600 |
| 7 | 115,200 |

### Host Connection Interpretation

- Decimal 16 = 0x10 = binary `0001 0000`
- Could represent a COM port number (COM16), a connection type enum, or a bitfield identifier

---

## 2. Hardware-Level Parameters (Oxford Semiconductor UART)

**Device:** Oxford Semiconductor PCI Express Multifunction Card (OXPCIEMF)
**PnP ID:** `*PNP0501` (Standard Serial Port)
**Read by:** PID 6456 at 6:06:45 PM
**Log Rows:** 3388–3399

### Port 1 — Instance 65000001

**Registry Path:** `HKLM\System\CurrentControlSet\Enum\OXPCIEMF\*PNP0501\65000001\Device Parameters`

| Parameter | Value | Type |
|---|---|---|
| **ClockRate** | **62,500,000** (62.5 MHz) | REG_DWORD |
| 95xSpecific | 148 bytes (see below) | REG_BINARY |

**95xSpecific raw data:**
01 00 00 00 0D 50 C9 16 20 00 01 00 00 00 00 00 0E 01 00 00 B4 09 00 00 02 00 00 00 02 00 03 91 18 61 1A 00 66 00 00 00

**Decoded DWORD values (little-endian):** | Offset | Hex Bytes | Decimal | Hex Value | |---|---|---|---| | 0x00 | `01 00 00 00` | 1 | 0x00000001 | | 0x04 | `0D 50 C9 16` | 382,291,981 | 0x16C9500D | | 0x08 | `20 00 01 00` | 65,568 | 0x00010020 | | 0x0C | `00 00 00 00` | 0 | 0x00000000 | | 0x10 | `0E 01 00 00` | 270 | 0x0000010E | | 0x14 | `B4 09 00 00` | 2,484 | 0x000009B4 | | 0x18 | `02 00 00 00` | 2 | 0x00000002 | | 0x1C | `02 00 03 91` | 2,432,892,930 | 0x91030002 | | 0x20 | `18 61 1A 00` | 1,728,792 | 0x001A6118 | | 0x24 | `66 00 00 00` | 102 | 0x00000066 | ### Port 0 — Instance 65000000 **Registry Path:** `HKLM\System\CurrentControlSet\Enum\OXPCIEMF\*PNP0501\65000000\Device Parameters` | Parameter | Value | Type | |---|---|---| | **ClockRate** | **62,500,000** (62.5 MHz) | REG_DWORD | | 95xSpecific | 148 bytes (see below) | REG_BINARY | **95xSpecific raw data:**
02 00 00 00 0D 50 C9 16 20 00 01 00 04 64 10 70 0E 01 00 00 B4 09 00 00 02 00 00 00 02 00 2E 00 BE 61 1A 00 57 00 00 00

**Decoded DWORD values (little-endian):** | Offset | Hex Bytes | Decimal | Hex Value | |---|---|---|---| | 0x00 | `02 00 00 00` | 2 | 0x00000002 | | 0x04 | `0D 50 C9 16` | 382,291,981 | 0x16C9500D | | 0x08 | `20 00 01 00` | 65,568 | 0x00010020 | | 0x0C | `04 64 10 70` | 1,880,122,372 | 0x70106404 | | 0x10 | `0E 01 00 00` | 270 | 0x0000010E | | 0x14 | `B4 09 00 00` | 2,484 | 0x000009B4 | | 0x18 | `02 00 00 00` | 2 | 0x00000002 | | 0x1C | `02 00 2E 00` | 3,014,658 | 0x002E0002 | | 0x20 | `BE 61 1A 00` | 1,728,958 | 0x001A61BE | | 0x24 | `57 00 00 00` | 87 | 0x00000057 | ### Clock Rate & Baud Rate Divisor Reference With a 62.5 MHz clock and standard 16× oversampling (`Baud = Clock / (16 × Divisor)`): | Baud Rate | Required Divisor | |---|---| | 300 | 13,020.83 | | 1,200 | 3,255.21 | | 2,400 | 1,627.60 | | 4,800 | 813.80 | | 9,600 | 406.90 | | 19,200 | 203.45 | | 38,400 | 101.73 | | 57,600 | 67.82 | | 115,200 | 33.91 | | 230,400 | 16.95 | | 460,800 | 8.48 | | 921,600 | 4.24 | > **Note:** The 95xSpecific blob is a proprietary Oxford Semiconductor OX95x-series structure. Full decoding requires manufacturer documentation. --- ## 3. Application Configuration File Attempt **Log Row:** 643 **Timestamp:** 6:04:45.9486 PM **Operation:** CreateFile **Path:** `C:\Program Files (x86)\Phoenix Technologies\VZSoft 3.0.2_x64\Settings.ini` **Result:** **NAME NOT FOUND** The application attempted to load a `Settings.ini` file (which might have contained additional communication parameters in plaintext), but the file did not exist on disk. The app fell back to registry-stored parameters. --- ## 4. What is NOT in the Log The ProcMon capture does **not** contain: - A direct `CreateFile` call to `\\.\COM9` (the ProcMon filter likely excluded device handle operations) - Any `DeviceIoControl` / `IOCTL_SERIAL_SET_BAUD_RATE` / `IOCTL_SERIAL_SET_LINE_CONTROL` calls - Explicit baud rate, parity, stop bits, or flow control settings via Windows Serial API - `SetCommState`, `GetCommState`, or `DCB` structure operations This strongly suggests the ProcMon capture was filtered to registry and file system operations only, excluding device I/O control operations that would show the actual `SetCommState()` call with full serial parameters (baud rate, data bits, parity, stop bits, flow control). --- ## 5. Search Strategy & Methodology Over 30 keyword searches were performed across the logfile: ### Phase 1 — Direct Device Searches (No Results) - `COM9`, `com9`, `COM`, `baud`, `9600`, `115200`, `19200` - `PortName`, `SetCommState`, `BaudRate`, `Parity`, `StopBit` ### Phase 2 — API & Device I/O Searches (No Results) - `CreateFile` (on device paths), `DeviceIoControl`, `IOCTL`, `IRP_MJ` - `IOCTL_SERIAL`, `SERIALCOMM`, `FTDIBUS`, `serenum` - `HARDWARE\\DEVICEMAP`, `DCB`, `Timeout` ### Phase 3 — Device Enumeration (Partial Success) - Found Oxford Semiconductor (OXPCIEMF) serial port device enumeration - Two port instances identified with ClockRate and 95xSpecific data ### Phase 4 — Application Configuration (Breakthrough) - Found `Settings.ini` attempt (file not found) - **Found VZServer\Parameters registry key with Comm Rate and Host Connection values** --- ## 6. Conclusion The communication parameters available in the logfile are: | Parameter | Value | Source | |---|---|---| | **Comm Rate** | **3** (index → likely 57,600 or 9,600 baud) | VZServer Registry | | **Host Connection** | **16** (port/connection identifier) | VZServer Registry | | **UART Clock** | **62,500,000 Hz** | Oxford Semiconductor Device Parameters | | **Serial Hardware** | Oxford Semiconductor OXPCIEMF PCI Express | Device Enumeration | | **Port Instances** | 65000000, 65000001 | Device Enumeration | To determine the exact baud rate, parity, stop bits, and flow control settings, either: 1. **Re-capture with ProcMon** including device I/O control operations (unfilter `DeviceIoControl`) 2. **Consult VZSoft documentation** for the Comm Rate index-to-baud-rate mapping 3. **Use a serial port monitor** (e.g., HHD Serial Monitor, Eltima Serial Port Monitor) to capture the actual `SetCommState` DCB structure