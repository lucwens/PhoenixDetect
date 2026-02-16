\# Visualeyez VZ10K/10K5 Low-Level Command Cheat Sheet



\[cite\_start]\*\*Hardware:\*\* Phoenix Technologies Visualeyez VZ10K/VZ10K5 Tracker \[cite: 3]

\[cite\_start]\*\*Protocol:\*\* RS-422 Serial (2.5 Mbps, 8-N-1, DTR/RTS Disabled/Enabled depending on phase) \[cite: 149]



---



\## 1. Command Format Structure

\[cite\_start]Every command sent to the tracker is exactly \*\*6 bytes long\*\* (plus optional parameters)\[cite: 247, 249].



| Byte 1 | Byte 2 | Byte 3 | Byte 4 | Byte 5 | Byte 6 | Optional |

| :--- | :--- | :--- | :--- | :--- | :--- | :--- |

| `\&` | `\[Code]` | `\[Index]` | `\[# Bytes]` | `\[# Params]` | `<CR>` | `\[Param Data]` |



\* \[cite\_start]\*\*Byte 1:\*\* Always `\&` (0x26)\[cite: 251].

\* \[cite\_start]\*\*Byte 2:\*\* Command Code (e.g., `L`, `3`, `p`)\[cite: 251].

\* \[cite\_start]\*\*Byte 3:\*\* Command Index (often `0` if ignored, or `1`-`8` for TCM IDs)\[cite: 251, 826, 871].

\* \[cite\_start]\*\*Byte 4:\*\* Number of bytes per parameter (if any, otherwise `0`)\[cite: 251].

\* \[cite\_start]\*\*Byte 5:\*\* Number of parameters to follow (if any, otherwise `0`)\[cite: 251].

\* \[cite\_start]\*\*Byte 6:\*\* Carriage Return `\\r` (0x0D)\[cite: 251].

\* \[cite\_start]\*\*Optional:\*\* Binary parameter data (sent MSB first)\[cite: 251, 274, 276].



\[cite\_start]\*Example:\* `\&p512\\r{08h}{03h}` appends LEDID 8 to TCM 5, flashing 3 times\[cite: 853, 861].



---



\## 2. Command Shortlists



\### System \& Reset Commands

| Command | Description |

| :--- | :--- |

| `\& ` | \[cite\_start]\*\*Reset:\*\* Make core processor reset the rest of the tracker hardware\[cite: 386]. |

| `\&7` | \[cite\_start]\*\*Ping/Test:\*\* Do nothing, just return an ACK message immediately\[cite: 388, 671]. |



\### Settings Commands

| Command | Description |

| :--- | :--- |

| `\&Q` | \[cite\_start]\*\*Single Sampling:\*\* Turn on the 'Single Sampling' process (System Default)\[cite: 388, 504]. |

| `\&P` | \[cite\_start]\*\*Double Sampling:\*\* Turn on the 'Double Sampling' process\[cite: 388]. |

| `\&W` | \[cite\_start]\*\*Auto Exposure ON:\*\* Turn on the 'Automatic Exposure' process (System Default)\[cite: 388, 585]. |

| `\&V` | \[cite\_start]\*\*Manual Exposure:\*\* Set exposure to user-specified values (disables Auto Exposure)\[cite: 388, 552]. |

| `\&Y` | \[cite\_start]\*\*Exposure Gain:\*\* Set the auto-exposure feedback control gain\[cite: 388]. |

| `\&U` | \[cite\_start]\*\*SOT Limit:\*\* Specify the 'Sample Operation Time' limit (max time spent per marker)\[cite: 388, 529]. |

| `\&L` | \[cite\_start]\*\*Signal Quality:\*\* Set the Signal Quality Requirement (SQR) level\[cite: 388, 430]. |

| `\&O` | \[cite\_start]\*\*Min Signal:\*\* Set the Minimum Signal Requirement (MSR) level\[cite: 388, 448]. |

| `\&X` | \[cite\_start]\*\*Multi-Rate Sampling:\*\* Set multi-rate sampling option for the TCMKs (SM0, SM1, SM2, SM3)\[cite: 388, 603, 607]. |

| `\&6` | \[cite\_start]\*\*Cycle Limit:\*\* Set the number of cycles (frames) to repeat sampling the markers before stopping (Default = Infinite/0xFFFFFFFF)\[cite: 388, 659]. |

| `\&u` | \[cite\_start]\*\*Toggle Marker:\*\* Toggle on/off a specified marker for visual display/spotting\[cite: 388, 685]. |

| `\&v` | \[cite\_start]\*\*Timing:\*\* Set Sampling Period and Sequence Intermission Period\[cite: 388]. |

| `\&^` | \[cite\_start]\*\*Tether Mode:\*\* Enable tether-mode control of the TCMs\[cite: 388]. |

| `\& ` | \[cite\_start]\*\*Tetherless Mode:\*\* Enable tetherless-mode control of the TCMs (System Default)\[cite: 388, 759]. |



\### Marker Control \& Programming Commands (TCMs)

| Command | Description |

| :--- | :--- |

| `\&p` | \[cite\_start]\*\*Program TFS:\*\* Clear the existing target flashing sequence (TFS) OR append a specific LED marker (LEDID, #flash pair)\[cite: 392, 824]. |

| `\&r` | \[cite\_start]\*\*Upload TFS:\*\* Make tracker program the target flashing sequence into all TCMs\[cite: 392, 893]. |

| `\&q` | \[cite\_start]\*\*Ready TCMs:\*\* Make all TCMs ready to flash their respective first LED targets\[cite: 392, 870]. |

| `\&]` | \[cite\_start]\*\*Reset TCMs:\*\* Reset all TCMs to their default states and indicate so by flashing once\[cite: 392, 911]. |

| `\&o` | \[cite\_start]\*\*Sync EOF:\*\* Make all TCMs synchronize on detection of the End Of Frame signal (System Default)\[cite: 392]. |

| `\&n` | \[cite\_start]\*\*Sync First-TCMID:\*\* Make all TCMs synchronize on detection of the first TCMID of the TFS\[cite: 392]. |



\### Capture Action Commands (Start/Stop)

| Command | Description |

| :--- | :--- |

| `\&3` | \[cite\_start]\*\*Start Periodic:\*\* Initiate periodic sampling according to the preset Sampling Period\[cite: 394, 930]. |

| `\&5` | \[cite\_start]\*\*Stop:\*\* Stop the periodic sampling process\[cite: 394, 953]. |

| `\&S` | \[cite\_start]\*\*Internal Trigger:\*\* Enable sampling by 'Internal Triggering'\[cite: 388]. |

| `\&R` | \[cite\_start]\*\*External Trigger:\*\* Arm the tracker for sampling by 'External Triggering' (System Default)\[cite: 394, 1048, 1063]. |

| `\&N` | \[cite\_start]\*\*Wait for Pulse:\*\* Make tracker wait for an external pulse before starting periodic sampling\[cite: 394, 1025]. |

| `\&G` | \[cite\_start]\*\*Activate Vibrator:\*\* Enroll ID of a vibrator for activation when its associated marker flashes\[cite: 394, 970]. |



\### Internal / Advanced Settings Commands

| Command | Description |

| :--- | :--- |

| `\&<` | \[cite\_start]\*\*Output 3D:\*\* Return 3D coordinates as results (System Default)\[cite: 398]. |

| `\&=` | \[cite\_start]\*\*Output Raw:\*\* Return raw sensor data as results (for factory calibration)\[cite: 398]. |

| `\&;` | \[cite\_start]\*\*Output Both:\*\* Return both raw sensor data and 3D coordinates as results\[cite: 398]. |

| `\&9` | \[cite\_start]\*\*Refraction ON:\*\* Enable refraction compensation (System Default)\[cite: 398]. |

| `\&:` | \[cite\_start]\*\*Refraction OFF:\*\* Disable refraction compensation\[cite: 398]. |

| `\&Z` | \[cite\_start]\*\*Auto-Exposure Target:\*\* Set desired signal peak value for Automatic Exposure to achieve\[cite: 398]. |

| `\&J` | \[cite\_start]\*\*Fetch Misalignment:\*\* Fetch the specified misalignment parameter\[cite: 400]. |

| `\&M` | \[cite\_start]\*\*Change Misalignment:\*\* Temporarily change the specified misalignment parameter\[cite: 400]. |

| `\&x` | \[cite\_start]\*\*Burn ROM:\*\* Burn the current misalignment parameters into the ROM\[cite: 400]. |



---

\## 3. Typical Capture Sequence Example

\[cite\_start]To program a sequence and start capturing, a typical command flow looks like this\[cite: 882, 885, 886, 887, 903, 906, 907]:

1\. \[cite\_start]`\&p000\\r` \*(Clear existing TFS)\* \[cite: 852]

2\. \[cite\_start]`\&p...` \*(Append markers to TFS)\* \[cite: 853, 854]

3\. \[cite\_start]`\&r000\\r` \*(Upload the TFS to the TCMs)\* \[cite: 885, 906]

4\. \[cite\_start]`\&q000\\r` \*(Ready the TCMs to start at the first marker)\* \[cite: 886, 907]

5\. \[cite\_start]`\&3000\\r` \*(Start the periodic capture process)\* \[cite: 887]

