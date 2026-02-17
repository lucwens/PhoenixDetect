#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Serial IOCTL codes not exposed by the standard Win32 API.
// These require DeviceIoControl() to invoke directly.
#ifndef IOCTL_SERIAL_GET_DTRRTS
#define IOCTL_SERIAL_GET_DTRRTS 0x001b0064
#endif

#ifndef IOCTL_SERIAL_CONFIG_SIZE
#define IOCTL_SERIAL_CONFIG_SIZE 0x001b006c
#endif

// Result of the HHD detection sequence
struct HHD_DetectionResult
{
    bool              deviceFound;
    DWORD             detectedBaudRate;
    DWORD             configSize;
    std::vector<BYTE> configData;
    std::string       portName;
    std::string       serialNumber; // 8-byte tracker serial number from Initial Message (decimal)
};

// Performs the HHD Software detection sequence on the specified COM port.
//
// Replicates the exact IRP-level serial I/O traffic captured from the reference
// application (Multi-Tracker Detection_x64.exe) using HHD Software's Device
// Monitoring Studio.
//
// Detection sequence:
//   Phase 1-2: Open port, close and re-open for clean state
//   Phase 3:   GET_HANDFLOW + DTR toggle (CLR-SET-CLR-SET, ~10ms spacing)
//   Phase 4:   Configure handshake, set baud, assert RTS/DTR, set 8N1
//   Phase 5:   Poll CONFIG_SIZE (14 retries, ~110ms interval)
//   Phase 6:   Read Initial Message (19 bytes: header 01 02 03 04 +
//              8-byte serial number + reserved + 01 + trailer 10 11 12 13)
//              per PTI Low Level Control Kit manual, Section 4.5, page 20.
//              This is the definitive confirmation of tracker presence.
//   Phase 7:   If no response, repeat at next baud rate (2M -> 2.5M)
//
// Parameters:
//   portName - COM port name, e.g. "COM9"
//
// Returns:
//   HHD_DetectionResult with deviceFound=true if a device responded.
HHD_DetectionResult Detect_HHD(const std::string &portName);
