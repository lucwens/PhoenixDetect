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

HANDLE CheckPort(int portNum) {
    std::string portName = "\\\\.\\COM" + std::to_string(portNum);

    HANDLE hSerial       = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hSerial == INVALID_HANDLE_VALUE)
    {
        // Port cannot be opened (doesn't exist or in use)
        return INVALID_HANDLE_VALUE;
    }

    std::cout << "Checking " << portName << "..." << std::endl;

    DCB dcbSerialParams       = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams))
    {
        std::cerr << "  Error getting state for " << portName << std::endl;
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
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
        return INVALID_HANDLE_VALUE;
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
        return INVALID_HANDLE_VALUE;
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

                        return hSerial;
                    }
                }
            }
        }
    }

    // Optional: Send Query command if needed.
    // Based on logs, the device sends the init message spontaneously.
    // If required, we could send "&?100\r" here.

    CloseHandle(hSerial);
    return INVALID_HANDLE_VALUE;
}

bool PingDevice(HANDLE hSerial) {
    // Command: &7000\r (Ping)
    // Hex: 26 37 30 30 30 0D
    std::string command = "&7000\r";
    DWORD bytesWritten;

    std::cout << "Sending Ping command (&70)..." << std::endl;

    if (!WriteFile(hSerial, command.c_str(), (DWORD)command.length(), &bytesWritten, NULL)) {
        std::cerr << "Error writing ping command." << std::endl;
        return false;
    }

    // Wait for response
    std::vector<unsigned char> buffer(256);
    DWORD bytesRead;

    // Give it some time to respond
    Sleep(200);

    if (ReadFile(hSerial, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL)) {
        if (bytesRead > 0) {
            // Trim buffer to actual read size
            buffer.resize(bytesRead);
            std::cout << "Received response (" << bytesRead << " bytes):" << std::endl;
            std::cout << "Hex: " << BytesToHex(buffer) << std::endl;
            std::cout << "ASCII: " << BytesToString(buffer) << std::endl;

            // Check for expected response pattern
            // Expected: 37 30 ('7' '0') or ACK
            // If the buffer starts with '7' '0' (0x37 0x30), it is an echo/ack of the ping.
            if (bytesRead >= 2 && buffer[0] == 0x37 && buffer[1] == 0x30) {
                std::cout << "Ping Acknowledged!" << std::endl;
                return true;
            } else {
                std::cout << "Response does not match standard ACK (70...)." << std::endl;
                // Note: driver.json showed 00000... response, so this might happen.
                return false;
            }
        } else {
            std::cout << "No response received." << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error reading response." << std::endl;
        return false;
    }
}

int main()
{
    std::cout << "VisualEyez Tracker Detector" << std::endl;
    std::cout << "Scanning COM ports for VisualEyez devices..." << std::endl;
    std::cout << "Config: " << TARGET_BAUDRATE << " baud, 8N1" << std::endl;

    bool found = false;
    // Scan typical range of COM ports
    for (int i = 1; i <= 32; ++i) {
        HANDLE hDevice = CheckPort(i);
        if (hDevice != INVALID_HANDLE_VALUE) {
            found = true;

            // Test communication with Ping
            PingDevice(hDevice);

            CloseHandle(hDevice);
            // Stop after finding one
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
