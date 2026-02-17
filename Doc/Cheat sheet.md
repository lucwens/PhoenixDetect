# Visualeyez VZ10K/10K5 Low-Level Command Cheat Sheet

**Hardware:** Phoenix Technologies Visualeyez VZ10K/VZ10K5 Tracker

**Protocol:** RS-422 Serial (2.5 Mbps, 8-N-1, DTR/RTS Disabled/Enabled depending on phase)

---

## 1. Command Format Structure

Every command sent to the tracker is exactly **6 bytes long** (plus optional parameters).

| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Optional |
|:---|:---|:---|:---|:---|:---|:---|
| `&` | `[Code]` | `[Index]` | `[# Bytes]` | `[# Params]` | `<CR>` | `[Param Data]` |

- **Byte 1:** Always `&` (0x26)
- **Byte 2:** Command Code (e.g., `L`, `3`, `p`)
- **Byte 3:** Command Index (often `0` if ignored, or `1`-`8` for TCM IDs)
- **Byte 4:** Number of bytes per parameter (if any, otherwise `0`)
- **Byte 5:** Number of parameters to follow (if any, otherwise `0`)
- **Byte 6:** Carriage Return `\r` (0x0D)
- **Optional:** Binary parameter data (sent MSB first)

*Example:* `&p512\r{08h}{03h}` appends LEDID 8 to TCM 5, flashing 3 times.

---

## 2. Command Shortlists

### System & Reset Commands

| Command | Description |
|:---|:---|
| `&`` | **Reset:** Make core processor reset the rest of the tracker hardware. |
| `&7` | **Ping/Test:** Do nothing, just return an ACK message immediately. |

### Settings Commands

| Command | Description |
|:---|:---|
| `&Q` | **Single Sampling:** Turn on the 'Single Sampling' process (System Default). |
| `&P` | **Double Sampling:** Turn on the 'Double Sampling' process. |
| `&W` | **Auto Exposure ON:** Turn on the 'Automatic Exposure' process (System Default). |
| `&V` | **Manual Exposure:** Set exposure to user-specified values (disables Auto Exposure). |
| `&Y` | **Exposure Gain:** Set the auto-exposure feedback control gain. |
| `&U` | **SOT Limit:** Specify the 'Sample Operation Time' limit (max time spent per marker). |
| `&L` | **Signal Quality:** Set the Signal Quality Requirement (SQR) level. |
| `&O` | **Min Signal:** Set the Minimum Signal Requirement (MSR) level. |
| `&X` | **Multi-Rate Sampling:** Set multi-rate sampling option for the TCMKs (SM0-SM3). |
| `&6` | **Cycle Limit:** Set the number of cycles (frames) before stopping (Default = Infinite). |
| `&u` | **Toggle Marker:** Toggle on/off a specified marker for visual display/spotting. |
| `&v` | **Timing:** Set Sampling Period and Sequence Intermission Period. |
| `&^` | **Tether Mode:** Enable tether-mode control of the TCMs. |
| `& ` | **Tetherless Mode:** Enable tetherless-mode control of the TCMs (System Default). |

### Marker Control & Programming Commands (TCMs)

| Command | Description |
|:---|:---|
| `&p` | **Program TFS:** Clear the existing TFS OR append a specific LED marker (LEDID, flash count). |
| `&r` | **Upload TFS:** Program the target flashing sequence into all TCMs. |
| `&q` | **Ready TCMs:** Make all TCMs ready to flash their respective first LED targets. |
| `&]` | **Reset TCMs:** Reset all TCMs to their default states (flash once to indicate). |
| `&o` | **Sync EOF:** Make all TCMs synchronize on End Of Frame signal (System Default). |
| `&n` | **Sync First-TCMID:** Make all TCMs synchronize on the first TCMID of the TFS. |

### Capture Action Commands (Start/Stop)

| Command | Description |
|:---|:---|
| `&3` | **Start Periodic:** Initiate periodic sampling at the preset Sampling Period. |
| `&5` | **Stop:** Stop the periodic sampling process. |
| `&S` | **Internal Trigger:** Enable sampling by 'Internal Triggering'. |
| `&R` | **External Trigger:** Arm the tracker for 'External Triggering' (System Default). |
| `&N` | **Wait for Pulse:** Wait for an external pulse before starting periodic sampling. |
| `&G` | **Activate Vibrator:** Enroll a vibrator ID for activation when its marker flashes. |

### Internal / Advanced Settings Commands

| Command | Description |
|:---|:---|
| `&<` | **Output 3D:** Return 3D coordinates as results (System Default). |
| `&=` | **Output Raw:** Return raw sensor data as results (for factory calibration). |
| `&;` | **Output Both:** Return both raw sensor data and 3D coordinates as results. |
| `&9` | **Refraction ON:** Enable refraction compensation (System Default). |
| `&:` | **Refraction OFF:** Disable refraction compensation. |
| `&Z` | **Auto-Exposure Target:** Set desired signal peak value for Automatic Exposure. |
| `&J` | **Fetch Misalignment:** Fetch the specified misalignment parameter. |
| `&M` | **Change Misalignment:** Temporarily change the specified misalignment parameter. |
| `&x` | **Burn ROM:** Burn the current misalignment parameters into the ROM. |

---

## 3. Typical Capture Sequence Example

To program a sequence and start capturing, a typical command flow looks like this:

1. `&p000\r` — Clear existing TFS
2. `&p...` — Append markers to TFS
3. `&r000\r` — Upload the TFS to the TCMs
4. `&q000\r` — Ready the TCMs to start at the first marker
5. `&3000\r` — Start the periodic capture process
