#include "Detect_HHD.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>

// --------------------------------------------------------------------------
// Internal helpers and constants
// --------------------------------------------------------------------------
namespace
{
    // Configuration for each baud-rate detection pass
    struct BaudRatePass
    {
        DWORD baudRate;
        WORD  xonLimit1; // XonLimit for first handshake configuration
        WORD  xonLimit2; // XonLimit for repeated handshake configuration
    };

    // Baud rates and XonLimit values from the IRP capture.
    // Pass 1 tries 2 Mbaud with XonLimit 14/22.
    // Pass 2 tries 2.5 Mbaud with XonLimit 74/82.
    const BaudRatePass g_DetectionPasses[] = {
        {2000000, 14, 22}, // Pass 1: 2 Mbaud
        {2500000, 74, 82}, // Pass 2: 2.5 Mbaud
    };

    const int CONFIG_SIZE_MAX_RETRIES            = 14;
    const int CONFIG_SIZE_POLL_INTERVAL_MS       = 110;
    const int DTR_TOGGLE_DELAY_MS                = 10;
    const int DTR_SETTLE_DELAY_MS                = 190;

    // PTI Initial Message Set (Section 4.5, page 20):
    //   19 bytes total, sent by tracker after hardware reset.
    //   Bytes 1-4:   01 02 03 04 (header)
    //   Bytes 5-12:  Tracker Serial Number (8 bytes, MSB first)
    //   Bytes 13-14: Reserved
    //   Byte 15:     01 (Initialized)
    //   Bytes 16-19: 10 11 12 13 (trailer)
    const int           INIT_MSG_SIZE            = 19;
    const int           INIT_SERIAL_OFFSET       = 4; // serial number starts at byte 5 (0-indexed: 4)
    const int           INIT_SERIAL_LENGTH       = 8;
    const unsigned char INIT_HEADER[]            = {0x01, 0x02, 0x03, 0x04};
    const int           INIT_HEADER_SIZE         = sizeof(INIT_HEADER);
    const unsigned char INIT_STATUS_BYTE         = 0x01; // byte 15: "Initialized"
    const unsigned char INIT_TRAILER[]           = {0x10, 0x11, 0x12, 0x13};
    const int           INIT_MSG_READ_TIMEOUT_MS = 2500; // time to wait for init message after reset
    // Open the COM port with GENERIC_READ | GENERIC_WRITE, exclusive access
    HANDLE              OpenPort(const std::string &portPath)
    {
        HANDLE h = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
        {
            std::cerr << "  [HHD] CreateFile failed for " << portPath << " (error " << GetLastError() << ")" << std::endl;
        }
        return h;
    }

    // Phase 3, Step 2: Toggle DTR line — device reset/wake-up pattern.
    // Sequence: CLR_DTR -> SET_DTR -> CLR_DTR -> SET_DTR with ~10ms spacing,
    // followed by a ~190ms settle delay before the next phase.
    void ToggleDTR(HANDLE hPort)
    {
        EscapeCommFunction(hPort, CLRDTR);
        Sleep(DTR_TOGGLE_DELAY_MS);
        EscapeCommFunction(hPort, SETDTR);
        Sleep(DTR_TOGGLE_DELAY_MS);
        EscapeCommFunction(hPort, CLRDTR);
        Sleep(DTR_TOGGLE_DELAY_MS);
        EscapeCommFunction(hPort, SETDTR);
        Sleep(DTR_SETTLE_DELAY_MS);
    }

    // Phase 4, Steps 3/5: Configure handshake/flow control.
    //   ControlHandShake = 0x2D:
    //     Bit 0 (0x01): SERIAL_DTR_CONTROL
    //     Bit 2 (0x04): SERIAL_DTR_HANDSHAKE
    //     Bit 3 (0x08): SERIAL_CTS_HANDSHAKE
    //     Bit 5 (0x20): SERIAL_DSR_HANDSHAKE
    //   FlowReplace = 0x01: SERIAL_XOFF_CONTINUE
    bool ConfigureHandshake(HANDLE hPort, WORD xonLimit)
    {
        DCB dcb       = {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(hPort, &dcb))
        {
            std::cerr << "  [HHD] GetCommState failed (error " << GetLastError() << ")" << std::endl;
            return false;
        }

        // ControlHandShake = 0x2D
        dcb.fDtrControl       = DTR_CONTROL_ENABLE; // SERIAL_DTR_CONTROL
        dcb.fOutxCtsFlow      = TRUE;               // SERIAL_CTS_HANDSHAKE
        dcb.fOutxDsrFlow      = TRUE;               // SERIAL_DSR_HANDSHAKE
        dcb.fDsrSensitivity   = TRUE;               // SERIAL_DCD_HANDSHAKE

        // FlowReplace = 0x01
        dcb.fTXContinueOnXoff = TRUE; // SERIAL_XOFF_CONTINUE

        dcb.XonLim            = xonLimit;
        dcb.XoffLim           = 0;

        if (!SetCommState(hPort, &dcb))
        {
            std::cerr << "  [HHD] SetCommState (handshake, XonLim=" << xonLimit << ") failed (error " << GetLastError() << ")" << std::endl;
            return false;
        }
        return true;
    }

    // Phase 4, Step 4: Query port status (modem status, comm errors, properties).
    // These are informational queries matching the IRP capture.
    void QueryPortStatus(HANDLE hPort)
    {
        DWORD modemStatus = 0;
        GetCommModemStatus(hPort, &modemStatus);

        DWORD   errors  = 0;
        COMSTAT comstat = {};
        ClearCommError(hPort, &errors, &comstat);

        COMMPROP commProp      = {};
        commProp.wPacketLength = sizeof(COMMPROP);
        GetCommProperties(hPort, &commProp);
    }

    // Phase 4, Step 6: Set baud rate (separate SetCommState call)
    bool SetBaudRate(HANDLE hPort, DWORD baudRate)
    {
        DCB dcb       = {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(hPort, &dcb))
            return false;

        dcb.BaudRate = baudRate;

        if (!SetCommState(hPort, &dcb))
        {
            std::cerr << "  [HHD] SetCommState (baud=" << baudRate << ") failed (error " << GetLastError() << ")" << std::endl;
            return false;
        }
        return true;
    }

    // Phase 4, Step 8: Set line control (8 data bits, 1 stop bit, no parity)
    bool SetLineControl8N1(HANDLE hPort)
    {
        DCB dcb       = {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(hPort, &dcb))
            return false;

        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity   = NOPARITY;

        if (!SetCommState(hPort, &dcb))
        {
            std::cerr << "  [HHD] SetCommState (8N1) failed (error " << GetLastError() << ")" << std::endl;
            return false;
        }
        return true;
    }

    // Phase 4, Step 9: Query DTR/RTS state via driver IOCTL (read-only)
    void QueryDtrRts(HANDLE hPort)
    {
        DWORD dtrRts        = 0;
        DWORD bytesReturned = 0;
        DeviceIoControl(hPort, IOCTL_SERIAL_GET_DTRRTS, NULL, 0, &dtrRts, sizeof(dtrRts), &bytesReturned, NULL);
    }

    // Phase 6: Read the 19-byte Initial Message sent by the tracker after reset.
    // The DTR toggle triggers a hardware reset; the tracker responds with the
    // Initial Message containing the 8-byte serial number (PTI manual, Section 4.5).
    // Returns true if the message was received and parsed successfully.
    bool ReadInitialMessage(HANDLE hPort, std::string &serialNumber)
    {
        // Set read timeouts — the tracker needs time to boot after reset
        COMMTIMEOUTS timeouts                = {};
        timeouts.ReadIntervalTimeout         = 50;
        timeouts.ReadTotalTimeoutConstant    = INIT_MSG_READ_TIMEOUT_MS;
        timeouts.ReadTotalTimeoutMultiplier  = 10;
        timeouts.WriteTotalTimeoutConstant   = 50;
        timeouts.WriteTotalTimeoutMultiplier = 10;
        SetCommTimeouts(hPort, &timeouts);

        // Read into a buffer large enough for the message plus any preceding noise
        unsigned char buffer[256] = {};
        DWORD         bytesRead   = 0;

        if (!ReadFile(hPort, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead < INIT_MSG_SIZE)
        {
            if (bytesRead > 0)
                std::cout << "  [HHD] Read " << bytesRead << " bytes but need at least " << INIT_MSG_SIZE << " for Initial Message" << std::endl;
            return false;
        }

        // Scan for the 01 02 03 04 header in the received data
        for (DWORD i = 0; i + INIT_MSG_SIZE <= bytesRead; i++)
        {
            // Check header
            if (memcmp(&buffer[i], INIT_HEADER, INIT_HEADER_SIZE) != 0)
                continue;

            // Check status byte (byte 15, 0-indexed: i+14)
            if (buffer[i + 14] != INIT_STATUS_BYTE)
                continue;

            // Check trailer (bytes 16-19, 0-indexed: i+15..i+18)
            if (memcmp(&buffer[i + 15], INIT_TRAILER, sizeof(INIT_TRAILER)) != 0)
                continue;

            // Valid Initial Message found — extract serial number (bytes 5-12)
            // 8 bytes, big-endian unsigned integer, displayed as decimal
            {
                uint64_t sn = 0;
                for (int j = 0; j < INIT_SERIAL_LENGTH; j++)
                    sn = (sn << 8) | buffer[i + INIT_SERIAL_OFFSET + j];
                serialNumber = std::to_string(sn);
            }

            std::cout << "  [HHD] Initial Message received — Serial Number: " << serialNumber << std::endl;
            return true;
        }

        std::cout << "  [HHD] No valid Initial Message found in " << bytesRead << " bytes read" << std::endl;
        return false;
    }

    // Phase 5: Poll CONFIG_SIZE in a loop, checking for device response.
    // Returns true if configSize becomes non-zero (device detected).
    bool PollConfigSize(HANDLE hPort, DWORD &configSize)
    {
        DWORD bytesReturned = 0;
        configSize          = 0;

        for (int i = 0; i < CONFIG_SIZE_MAX_RETRIES; i++)
        {
            BOOL ok = DeviceIoControl(hPort, IOCTL_SERIAL_CONFIG_SIZE, NULL, 0, &configSize, sizeof(configSize), &bytesReturned, NULL);

            if (!ok && i == 0)
            {
                std::cerr << "  [HHD] CONFIG_SIZE IOCTL not supported (error " << GetLastError() << ")" << std::endl;
                return false;
            }

            if (configSize != 0)
            {
                std::cout << "  [HHD] CONFIG_SIZE = " << configSize << " on poll #" << (i + 1) << std::endl;
                return true;
            }

            Sleep(CONFIG_SIZE_POLL_INTERVAL_MS);
        }

        return false;
    }

    // Run a single detection pass: configure port, poll for device response,
    // then read the Initial Message for definitive serial number confirmation.
    // Phases 3-6 from the IRP capture + PTI manual Section 4.5.
    bool RunDetectionPass(HANDLE hPort, const BaudRatePass &pass, DWORD &configSize, std::string &serialNumber)
    {
        // Phase 3, Step 1: Read current handshake settings
        DCB dcb       = {};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(hPort, &dcb);

        // Phase 3, Step 2: DTR toggle (device reset/wake-up)
        ToggleDTR(hPort);

        // Purge any stale data from buffers before configuring
        PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

        // Phase 4, Step 3: Configure handshake with first XonLimit
        if (!ConfigureHandshake(hPort, pass.xonLimit1))
            return false;

        // Phase 4, Step 4: Query port status
        QueryPortStatus(hPort);

        // Phase 4, Step 5: Repeat handshake config with adjusted XonLimit
        if (!ConfigureHandshake(hPort, pass.xonLimit2))
            return false;

        // Query status again
        QueryPortStatus(hPort);

        // Phase 4, Step 6: Set baud rate
        if (!SetBaudRate(hPort, pass.baudRate))
            return false;

        // Phase 4, Step 7: Assert control lines
        EscapeCommFunction(hPort, SETRTS);
        EscapeCommFunction(hPort, SETDTR);

        // Phase 4, Step 8: Set line control (8N1)
        if (!SetLineControl8N1(hPort))
            return false;

        // Phase 4, Step 9: Verify DTR/RTS state (read-only)
        QueryDtrRts(hPort);

        // Phase 5: Poll CONFIG_SIZE for device response
        bool configSizeOk = PollConfigSize(hPort, configSize);

        // Phase 6: Read Initial Message — definitive tracker detection.
        // The DTR toggle triggered a hardware reset; if a tracker is present
        // it will have sent the 19-byte Initial Message containing its serial
        // number (PTI manual Section 4.5, page 20).
        bool initMsgOk    = ReadInitialMessage(hPort, serialNumber);

        // Detection succeeds if we got the Initial Message (definitive) or
        // CONFIG_SIZE responded (driver-level confirmation).
        return initMsgOk || configSizeOk;
    }

} // anonymous namespace

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

HHD_DetectionResult Detect_HHD(const std::string &portName)
{
    HHD_DetectionResult result = {};
    result.deviceFound         = false;
    result.portName            = portName;

    std::string portPath       = "\\\\.\\" + portName;

    std::cout << "[HHD] Starting detection on " << portName << std::endl;

    // Phase 1: Open port
    HANDLE hPort = OpenPort(portPath);
    if (hPort == INVALID_HANDLE_VALUE)
        return result;

    // Phase 2: Close and re-open for clean state
    CloseHandle(hPort);
    hPort = OpenPort(portPath);
    if (hPort == INVALID_HANDLE_VALUE)
        return result;

    // Try each baud rate pass
    for (size_t passIdx = 0; passIdx < _countof(g_DetectionPasses); passIdx++)
    {
        const BaudRatePass &pass       = g_DetectionPasses[passIdx];
        DWORD               configSize = 0;

        std::cout << "  [HHD] Pass " << (passIdx + 1) << ": trying " << pass.baudRate << " baud" << std::endl;

        std::string serialNumber;
        if (RunDetectionPass(hPort, pass, configSize, serialNumber))
        {
            result.deviceFound      = true;
            result.detectedBaudRate = pass.baudRate;
            result.configSize       = configSize;
            result.serialNumber     = serialNumber;

            // Read config data if CONFIG_SIZE returned non-zero
            if (configSize > 0)
            {
                result.configData.resize(configSize);
                DWORD bytesRead = 0;
                ReadFile(hPort, result.configData.data(), configSize, &bytesRead, NULL);
                result.configData.resize(bytesRead);
            }

            std::cout << "[HHD] Device DETECTED on " << portName << " at " << pass.baudRate << " baud"
                      << " (configSize=" << configSize << ")" << std::endl;
            if (!serialNumber.empty())
                std::cout << "[HHD] Tracker Serial Number: " << serialNumber << std::endl;

            CloseHandle(hPort);
            return result;
        }

        std::cout << "  [HHD] No response at " << pass.baudRate << " baud" << std::endl;

        // Between passes: close and re-open for clean state (matches IRP capture)
        if (passIdx + 1 < _countof(g_DetectionPasses))
        {
            CloseHandle(hPort);
            hPort = OpenPort(portPath);
            if (hPort == INVALID_HANDLE_VALUE)
                return result;
        }
    }

    std::cout << "[HHD] No device detected on " << portName << std::endl;

    CloseHandle(hPort);
    return result;
}
