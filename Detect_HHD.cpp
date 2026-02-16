#include "Detect_HHD.h"
#include <iostream>

// --------------------------------------------------------------------------
// Configuration for each baud-rate detection pass
// --------------------------------------------------------------------------
struct BaudRatePass
{
    DWORD baudRate;
    WORD  xonLimit1; // XonLimit for first handshake configuration
    WORD  xonLimit2; // XonLimit for repeated handshake configuration
};

// Baud rates and XonLimit values from the IRP capture.
// Pass 1 tries 2 Mbaud with XonLimit 14/22.
// Pass 2 tries 2.5 Mbaud with XonLimit 74/82.
static const BaudRatePass g_DetectionPasses[] = {
    {2000000, 14, 22}, // Pass 1: 2 Mbaud
    {2500000, 74, 82}, // Pass 2: 2.5 Mbaud
};

static const int CONFIG_SIZE_MAX_RETRIES     = 14;
static const int CONFIG_SIZE_POLL_INTERVAL_MS = 110;
static const int DTR_TOGGLE_DELAY_MS          = 10;
static const int DTR_SETTLE_DELAY_MS          = 190;

// --------------------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------------------
namespace
{
    // Open the COM port with GENERIC_READ | GENERIC_WRITE, exclusive access
    HANDLE OpenPort(const std::string &portPath)
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
        dcb.fDtrControl     = DTR_CONTROL_ENABLE; // SERIAL_DTR_CONTROL
        dcb.fOutxCtsFlow    = TRUE;                // SERIAL_CTS_HANDSHAKE
        dcb.fOutxDsrFlow    = TRUE;                // SERIAL_DSR_HANDSHAKE
        dcb.fDsrSensitivity = TRUE;                // SERIAL_DCD_HANDSHAKE

        // FlowReplace = 0x01
        dcb.fTXContinueOnXoff = TRUE; // SERIAL_XOFF_CONTINUE

        dcb.XonLim  = xonLimit;
        dcb.XoffLim = 0;

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

    // Run a single detection pass: configure port, then poll for device response.
    // Phases 3-5 from the IRP capture.
    bool RunDetectionPass(HANDLE hPort, const BaudRatePass &pass, DWORD &configSize)
    {
        // Phase 3, Step 1: Read current handshake settings
        DCB dcb       = {};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(hPort, &dcb);

        // Phase 3, Step 2: DTR toggle (device reset/wake-up)
        ToggleDTR(hPort);

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
        return PollConfigSize(hPort, configSize);
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

    std::string portPath = "\\\\.\\" + portName;

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

        if (RunDetectionPass(hPort, pass, configSize))
        {
            result.deviceFound      = true;
            result.detectedBaudRate = pass.baudRate;
            result.configSize       = configSize;

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
