# VisualEyez Tracker Detector

This is a C++ console application that detects a connected VisualEyez tracker on COM ports and retrieves its serial number.

## Requirements

-   Windows OS (due to Win32 API usage for serial communication).
-   A C++ compiler (MSVC or MinGW).
-   Hardware: VisualEyez Tracker connected via Serial/USB (supporting 2.5 Mbps baud rate).

## Compilation

### Using MinGW (g++)

```bash
g++ -o detect.exe VisualEyezDetector.cpp
```

### Using Visual Studio (MSVC)

1.  Open Visual Studio Developer Command Prompt.
2.  Run:
    ```cmd
    cl /EHsc VisualEyezDetector.cpp
    ```

## Usage

Run the executable from the command line:

```cmd
detect.exe
```

The program will scan COM ports (COM1 to COM32) and attempt to detect the tracker.

## How it works

1.  It iterates through available COM ports.
2.  It attempts to open each port with the following configuration (derived from provided data logs):
    -   Baud Rate: 2,500,000
    -   Data Bits: 8
    -   Stop Bits: 1
    -   Parity: None
3.  It listens for an initialization message `01 02 03 04 ...` which the device sends upon connection/reset.
4.  If the message is received, it extracts the 8-byte serial number following the header and prints it in Hex and ASCII.

## Protocol Details

Based on `Data/detect.json` and other logs:
-   **Protocol**: Phoenix Visualeyez VZK10 RS-422
-   **Init Message**: `01 02 03 04 [Serial Number 8 bytes] ...`
-   **Serial Number**: Found at offset 4 of the init message.
