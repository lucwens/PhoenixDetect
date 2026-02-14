#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cctype>

// VisualEyez Configuration
// Based on logs: 2,500,000 baud, 8 data bits, 1 stop bit, No parity.
const int TARGET_BAUDRATE                     = 2500000;
const int TARGET_DATA_BITS                    = 8;
const int TARGET_STOP_BITS                    = ONESTOPBIT;
const int TARGET_PARITY                       = NOPARITY;

// Pattern to detect: 01 02 03 04 (Init Message Header)
// Full Init Message from logs: 01 02 03 04 [8 bytes Serial] 0d 0e 01 10 11 12 13
// Serial Number is at offset 4 (0-indexed) with length 8 bytes.
const std::vector<unsigned char> INIT_PATTERN = {0x01, 0x02, 0x03, 0x04};

std::string BytesToHex(const std::vector<unsigned char> &bytes)
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char b : bytes)
    {
        ss << std::setw(2) << (int)b;
    }
    return ss.str();
}

std::string BytesToString(const std::vector<unsigned char> &bytes)
{
    std::string str;
    for (unsigned char b : bytes)
    {
        if (std::isprint(b))
        {
            str += (char)b;
        }
        else
        {
            str += '.';
        }
    }
    return str;
}

bool CheckPort(int portNum)
{
    std::string portName = "\\\\.\\COM" + std::to_string(portNum);

    HANDLE hSerial       = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hSerial == INVALID_HANDLE_VALUE)
    {
        // Port cannot be opened (doesn't exist or in use)
        return false;
    }

    std::cout << "Checking " << portName << "..." << std::endl;

    DCB dcbSerialParams       = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams))
    {
        std::cerr << "  Error getting state for " << portName << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    dcbSerialParams.BaudRate    = TARGET_BAUDRATE;
    dcbSerialParams.ByteSize    = TARGET_DATA_BITS;
    dcbSerialParams.StopBits    = TARGET_STOP_BITS;
    dcbSerialParams.Parity      = TARGET_PARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE; // Ensure DTR is ON
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE; // Ensure RTS is ON

    if (!SetCommState(hSerial, &dcbSerialParams))
    {
        std::cerr << "  Error setting state for " << portName << ". Baud rate " << TARGET_BAUDRATE << " might not be supported." << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    COMMTIMEOUTS timeouts                = {0};
    // Wait up to 500ms for the init message
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 500;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts))
    {
        std::cerr << "  Error setting timeouts for " << portName << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    // Purge buffers to ensure we read fresh data
    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Buffer for reading
    std::vector<unsigned char> buffer(256);
    DWORD                      bytesRead;

    // Attempt to read. The tracker sends an init message on connection/reset.
    if (ReadFile(hSerial, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL))
    {
        if (bytesRead >= INIT_PATTERN.size())
        {
            // Check for pattern
            for (size_t i = 0; i <= bytesRead - INIT_PATTERN.size(); ++i)
            {
                bool match = true;
                for (size_t k = 0; k < INIT_PATTERN.size(); ++k)
                {
                    if (buffer[i + k] != INIT_PATTERN[k])
                    {
                        match = false;
                        break;
                    }
                }

                if (match)
                {
                    // Found header
                    // Ensure we have enough bytes for the serial number (header + 8 bytes)
                    if (i + INIT_PATTERN.size() + 8 <= bytesRead)
                    {
                        // Extract Serial Number (8 bytes starting at index i+4)
                        std::vector<unsigned char> serialBytes;
                        for (int j = 0; j < 8; ++j)
                        {
                            serialBytes.push_back(buffer[i + INIT_PATTERN.size() + j]);
                        }

                        std::cout << "\n*** VisualEyez Tracker DETECTED on " << portName << " ***" << std::endl;
                        std::cout << "Serial Number (Hex): " << BytesToHex(serialBytes) << std::endl;
                        std::cout << "Serial Number (ASCII): " << BytesToString(serialBytes) << std::endl;
                        std::cout << "******************************************\n" << std::endl;

                        CloseHandle(hSerial);
                        return true;
                    }
                }
            }
        }
    }

    // Optional: Send Query command if needed.
    // Based on logs, the device sends the init message spontaneously.
    // If required, we could send "&?100\r" here.

    CloseHandle(hSerial);
    return false;
}

int main()
{
    std::cout << "VisualEyez Tracker Detector" << std::endl;
    std::cout << "Scanning COM ports for VisualEyez devices..." << std::endl;
    std::cout << "Config: " << TARGET_BAUDRATE << " baud, 8N1" << std::endl;

    bool found = false;
    // Scan typical range of COM ports
    for (int i = 1; i <= 32; ++i)
    {
        if (CheckPort(i))
        {
            found = true;
            // Stop after finding one? Or keep scanning?
            // Usually we stop unless looking for multiple.
            // Let's break for this console tool.
            break;
        }
    }

    if (!found)
    {
        std::cout << "No VisualEyez Tracker detected." << std::endl;
    }

    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();
    return 0;
}
