#include "Detect_HHD.h"
#include "Measure_HHD.h"
#include "DmsLogReader.h"
#include "PhoenixDecoder.h"
#include "JsonWriter.h"

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <conio.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>

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

// Saved detection info for a single tracker
struct DetectedTracker
{
    std::string portName;
    DWORD       baudRate;
    std::string serialNumber;
};

// Save detected marker configuration to Settings/MarkerConfig.json
void SaveMarkerConfig(const HHD_ConfigDetectResult &config)
{
    CreateDirectoryA("Settings", NULL);
    std::ofstream ofs("Settings/MarkerConfig.json");
    if (!ofs.is_open())
        return;

    ofs << "{\n  \"tcms\": [\n";
    for (size_t t = 0; t < config.tcms.size(); t++)
    {
        const auto &tcm = config.tcms[t];
        ofs << "    {\n      \"tcmId\": " << (int)tcm.tcmId << ",\n      \"markers\": [\n";
        for (size_t m = 0; m < tcm.markers.size(); m++)
        {
            const auto &mk = tcm.markers[m];
            ofs << "        { \"ledId\": " << (int)mk.ledId << ", \"detectionRate\": " << std::fixed << std::setprecision(2) << mk.detectionRate << " }";
            if (m + 1 < tcm.markers.size())
                ofs << ",";
            ofs << "\n";
        }
        ofs << "      ]\n    }";
        if (t + 1 < config.tcms.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n  \"markerList\": [\n";
    for (size_t i = 0; i < config.markerList.size(); i++)
    {
        const auto &m = config.markerList[i];
        ofs << "    { \"tcmId\": " << (int)m.tcmId << ", \"ledId\": " << (int)m.ledId << ", \"flashCount\": " << (int)m.flashCount << " }";
        if (i + 1 < config.markerList.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "  ]\n}\n";
    ofs.close();
    std::cout << "Marker config saved to Settings/MarkerConfig.json" << std::endl;
}

// Load marker configuration from Settings/MarkerConfig.json
// Returns true if at least one marker was loaded.
bool LoadMarkerConfig(std::vector<HHD_MarkerEntry> &markers)
{
    markers.clear();
    std::ifstream ifs("Settings/MarkerConfig.json");
    if (!ifs.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    // Parse markerList entries: { "tcmId": N, "ledId": N, "flashCount": N }
    std::string search = "\"markerList\"";
    size_t      mlPos  = content.find(search);
    if (mlPos == std::string::npos)
        return false;

    // Find each marker object after markerList
    size_t pos = mlPos;
    while ((pos = content.find('{', pos)) != std::string::npos)
    {
        size_t blockEnd = content.find('}', pos);
        if (blockEnd == std::string::npos)
            break;

        std::string block = content.substr(pos, blockEnd - pos + 1);

        // Extract tcmId
        auto extractInt   = [&](const std::string &key) -> int
        {
            std::string k  = "\"" + key + "\": ";
            size_t      kp = block.find(k);
            if (kp == std::string::npos)
                return -1;
            return std::stoi(block.substr(kp + k.size()));
        };

        int tcm = extractInt("tcmId");
        int led = extractInt("ledId");
        int fc  = extractInt("flashCount");

        if (tcm >= 1 && tcm <= 8 && led >= 1 && led <= 64 && fc >= 1)
            markers.push_back({static_cast<uint8_t>(tcm), static_cast<uint8_t>(led), static_cast<uint8_t>(fc)});

        pos = blockEnd + 1;
    }

    return !markers.empty();
}

// Save all detected trackers to Settings/Detect.json
void SaveDetectionSettings(const std::vector<DetectedTracker> &trackers)
{
    CreateDirectoryA("Settings", NULL);
    std::ofstream ofs("Settings/Detect.json");
    if (!ofs.is_open())
        return;

    ofs << "[\n";
    for (size_t i = 0; i < trackers.size(); i++)
    {
        const auto &t = trackers[i];
        ofs << "  {\n";
        ofs << "    \"portName\": \"" << t.portName << "\",\n";
        ofs << "    \"baudRate\": " << t.baudRate << ",\n";
        ofs << "    \"serialNumber\": \"" << t.serialNumber << "\"\n";
        ofs << "  }";
        if (i + 1 < trackers.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    ofs.close();
    std::cout << "Detection settings saved to Settings/Detect.json (" << trackers.size() << " tracker(s))" << std::endl;
}

// Load detected trackers from Settings/Detect.json
// Returns true if at least one valid tracker was loaded.
bool LoadDetectionSettings(std::vector<DetectedTracker> &trackers)
{
    trackers.clear();
    std::ifstream ifs("Settings/Detect.json");
    if (!ifs.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    // Helper lambdas to extract values starting from a given position
    auto extractString = [&](const std::string &key, size_t startPos) -> std::string
    {
        std::string search = "\"" + key + "\": \"";
        size_t      pos    = content.find(search, startPos);
        if (pos == std::string::npos)
            return "";
        pos += search.size();
        size_t end = content.find('"', pos);
        if (end == std::string::npos)
            return "";
        return content.substr(pos, end - pos);
    };

    auto extractNumber = [&](const std::string &key, size_t startPos) -> DWORD
    {
        std::string search = "\"" + key + "\": ";
        size_t      pos    = content.find(search, startPos);
        if (pos == std::string::npos)
            return 0;
        pos += search.size();
        return static_cast<DWORD>(std::stoul(content.substr(pos)));
    };

    // Parse each object block delimited by '{' ... '}'
    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos)
    {
        size_t blockEnd = content.find('}', pos);
        if (blockEnd == std::string::npos)
            break;

        DetectedTracker t;
        t.portName     = extractString("portName", pos);
        t.baudRate     = extractNumber("baudRate", pos);
        t.serialNumber = extractString("serialNumber", pos);

        if (!t.portName.empty() && t.baudRate > 0)
            trackers.push_back(t);

        pos = blockEnd + 1;
    }

    return !trackers.empty();
}

std::string GenerateLogFilename()
{
    auto      now  = std::chrono::system_clock::now();
    auto      time = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    localtime_s(&tm, &time);

    CreateDirectoryA("Output", NULL);

    std::ostringstream ss;
    ss << "Output/Measure_" << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << std::setw(2) << (tm.tm_mon + 1) << std::setw(2) << tm.tm_mday << "_"
       << std::setw(2) << tm.tm_hour << std::setw(2) << tm.tm_min << ".ndjson";
    return ss.str();
}

void WriteFrameNdjson(std::ofstream &logFile, const std::vector<HHD_MeasurementSample> &frameSamples)
{
    if (frameSamples.empty() || !logFile.is_open())
        return;

    logFile << "{\"frame\":{\"timestamp_us\":" << frameSamples[0].timestamp_us << ",\"markerCount\":" << frameSamples.size()
            << ",\"triggerIndex\":" << (int)frameSamples[0].triggerIndex << "},\"markers\":[";

    for (size_t i = 0; i < frameSamples.size(); i++)
    {
        const auto &s = frameSamples[i];
        if (i > 0)
            logFile << ",";
        logFile << std::fixed << std::setprecision(2) << "{\"tcmId\":" << (int)s.tcmId << ",\"ledId\":" << (int)s.ledId << ",\"position\":{\"x\":" << s.x_mm
                << ",\"y\":" << s.y_mm << ",\"z\":" << s.z_mm << "}"
                << ",\"quality\":{\"ambientLight\":" << (int)s.ambientLight << ",\"coordStatus\":" << (int)s.coordStatus
                << ",\"rightEye\":{\"signal\":" << (int)s.rightEyeSignal << ",\"status\":" << (int)s.rightEyeStatus << "}"
                << ",\"centerEye\":{\"signal\":" << (int)s.centerEyeSignal << ",\"status\":" << (int)s.centerEyeStatus << "}"
                << ",\"leftEye\":{\"signal\":" << (int)s.leftEyeSignal << ",\"status\":" << (int)s.leftEyeStatus << "}"
                << "}}";
    }

    logFile << "]}\n";
    logFile.flush();
}

HANDLE CheckPort(int portNum)
{
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

bool PingDevice(HANDLE hSerial)
{
    // Command: &7000\r (Ping)
    // Hex: 26 37 30 30 30 0D
    std::string command = "&7000\r";
    DWORD       bytesWritten;

    std::cout << "Sending Ping command (&70)..." << std::endl;

    if (!WriteFile(hSerial, command.c_str(), (DWORD)command.length(), &bytesWritten, NULL))
    {
        std::cerr << "Error writing ping command." << std::endl;
        return false;
    }

    // Wait for response
    std::vector<unsigned char> buffer(256);
    DWORD                      bytesRead;

    // Give it some time to respond
    Sleep(200);

    if (ReadFile(hSerial, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL))
    {
        if (bytesRead > 0)
        {
            // Trim buffer to actual read size
            buffer.resize(bytesRead);
            std::cout << "Received response (" << bytesRead << " bytes):" << std::endl;
            std::cout << "Hex: " << BytesToHex(buffer) << std::endl;
            std::cout << "ASCII: " << BytesToString(buffer) << std::endl;

            // Check for expected response pattern
            // Expected: 37 30 ('7' '0') or ACK
            // If the buffer starts with '7' '0' (0x37 0x30), it is an echo/ack of the ping.
            if (bytesRead >= 2 && buffer[0] == 0x37 && buffer[1] == 0x30)
            {
                std::cout << "Ping Acknowledged!" << std::endl;
                return true;
            }
            else
            {
                std::cout << "Response does not match standard ACK (70...)." << std::endl;
                // Note: driver.json showed 00000... response, so this might happen.
                return false;
            }
        }
        else
        {
            std::cout << "No response received." << std::endl;
            return false;
        }
    }
    else
    {
        std::cerr << "Error reading response." << std::endl;
        return false;
    }
}

namespace fs = std::filesystem;

static bool convertFile(const std::string &inputPath, const std::string &outputPath)
{
    std::cout << "Converting: " << inputPath << "\n";

    DmsLogReader reader;
    if (!reader.open(inputPath))
        return false;

    const auto &hdr = reader.header();
    std::cout << "  Device: " << hdr.deviceName << "\n";
    if (!hdr.portConfig.empty())
        std::cout << "  Port:   " << hdr.portConfig << "\n";

    std::vector<IrpRecord> records;
    if (!reader.readRecords(records))
    {
        std::cerr << "  Error: no serial data records found\n";
        return false;
    }

    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> txRecords, rxRecords;
    for (const auto &rec : records)
    {
        auto entry = std::make_pair(rec.timestamp, rec.serialData);
        if (rec.functionCode == 4 && !rec.isCompletion)
            txRecords.push_back(std::move(entry));
        else
            rxRecords.push_back(std::move(entry));
    }

    std::cout << "  TX packets: " << txRecords.size() << "\n";
    std::cout << "  RX packets: " << rxRecords.size() << "\n";

    PhoenixDecoder            decoder;
    std::vector<PhoenixFrame> frames;
    decoder.decode(txRecords, rxRecords, frames);

    std::cout << "  Decoded frames: " << frames.size() << "\n";

    JsonWriter writer;
    if (!writer.write(outputPath, hdr, inputPath, frames))
        return false;

    std::cout << "  Output: " << outputPath << "\n";
    return true;
}

static int convertDirectory(const std::string &dirPath)
{
    fs::path dir(dirPath);
    if (!fs::is_directory(dir))
    {
        std::cerr << "Error: not a directory: " << dirPath << "\n";
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> filesToConvert;
    for (const auto &entry : fs::recursive_directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        std::string ext = entry.path().extension().string();
        if (ext.find(".dmslog") == 0)
        {
            std::string outPath = entry.path().string();
            size_t      dotPos  = outPath.rfind(".dmslog");
            if (dotPos != std::string::npos)
                outPath = outPath.substr(0, dotPos) + ".json";
            else
                outPath += ".json";
            filesToConvert.emplace_back(entry.path().string(), outPath);
        }
    }

    if (filesToConvert.empty())
    {
        std::cerr << "No .dmslog files found in: " << dirPath << "\n";
        return 1;
    }

    int successCount = 0;
    for (const auto &[inFile, outFile] : filesToConvert)
    {
        if (convertFile(inFile, outFile))
            ++successCount;
    }

    std::cout << "\nConverted " << successCount << "/" << filesToConvert.size() << " file(s).\n";
    return (successCount == static_cast<int>(filesToConvert.size())) ? 0 : 1;
}

int main(int argc, char *argv[])
{
    // If a directory argument is given, convert all .dmslog8 files to JSON
    if (argc >= 2)
    {
        return convertDirectory(argv[1]);
    }

    HANDLE                       hPort   = INVALID_HANDLE_VALUE;
    HHD_MeasurementSession      *session = nullptr;
    std::vector<DetectedTracker> detectedTrackers;
    std::vector<HHD_MarkerEntry> activeMarkers; // discovered or loaded marker config

    // Load saved detection settings from previous session
    if (LoadDetectionSettings(detectedTrackers))
    {
        std::cout << "Loaded saved detection settings (" << detectedTrackers.size() << " tracker(s)):" << std::endl;
        for (size_t i = 0; i < detectedTrackers.size(); i++)
        {
            const auto &t = detectedTrackers[i];
            std::cout << "  [" << (i + 1) << "] " << t.portName << "  Baud: " << t.baudRate;
            if (!t.serialNumber.empty())
                std::cout << "  Serial: " << t.serialNumber;
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }

    // Load saved marker configuration
    if (LoadMarkerConfig(activeMarkers))
    {
        std::cout << "Loaded saved marker config (" << activeMarkers.size() << " markers):";
        for (const auto &m : activeMarkers)
            std::cout << " TCM" << (int)m.tcmId << "/LED" << (int)m.ledId;
        std::cout << std::endl << std::endl;
    }

    const DWORD MEASURE_DURATION_MS = 3000;

    std::cout << "VisualEyez Tracker Interactive Console" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "  h - Detect HHD on COM1-COM16" << std::endl;
    std::cout << "  d - Detect marker configuration (auto-scan connected TCMs and LEDs)" << std::endl;
    std::cout << "  s - Start measurement (10 Hz, auto-stops after " << (MEASURE_DURATION_MS / 1000) << "s)" << std::endl;
    std::cout << "  c - Cycle: start/stop every " << (MEASURE_DURATION_MS / 1000) << "s continuously" << std::endl;
    std::cout << "  t - Stop measurement (also stops cycling)" << std::endl;
    std::cout << "  q - Quit" << std::endl;
    if (!detectedTrackers.empty())
    {
        std::cout << "  [Ready: " << detectedTrackers.size() << " tracker(s) — ";
        for (size_t i = 0; i < detectedTrackers.size(); i++)
        {
            if (i > 0)
                std::cout << ", ";
            std::cout << detectedTrackers[i].portName;
        }
        std::cout << "]" << std::endl;
    }
    if (!activeMarkers.empty())
    {
        std::cout << "  [Markers: " << activeMarkers.size() << " configured]" << std::endl;
    }
    std::cout << std::endl;
    ULONGLONG                          measureStartTick = 0;
    std::ofstream                      logFile;
    std::vector<HHD_MeasurementSample> frameBuffer;
    bool                               cycling    = false;
    int                                cycleCount = 0;

    // Helper: open port, configure, and start a measurement session.
    // Returns true if session started successfully.
    auto startMeasurementOnTracker                = [&]() -> bool
    {
        if (detectedTrackers.empty())
            return false;

        const auto &tracker  = detectedTrackers[0];
        std::string portPath = "\\\\.\\" + tracker.portName;
        hPort                = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hPort == INVALID_HANDLE_VALUE)
        {
            std::cout << "Failed to open " << tracker.portName << std::endl;
            return false;
        }

        DCB dcb       = {};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(hPort, &dcb);
        dcb.BaudRate          = tracker.baudRate;
        dcb.ByteSize          = 8;
        dcb.StopBits          = ONESTOPBIT;
        dcb.Parity            = NOPARITY;
        dcb.fDtrControl       = DTR_CONTROL_ENABLE;
        dcb.fOutxCtsFlow      = TRUE;
        dcb.fOutxDsrFlow      = TRUE;
        dcb.fDsrSensitivity   = TRUE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.XonLim            = (tracker.baudRate == 2000000) ? 22 : 82;
        dcb.XoffLim           = 0;
        SetCommState(hPort, &dcb);

        EscapeCommFunction(hPort, SETRTS);
        EscapeCommFunction(hPort, SETDTR);

        // Use discovered markers if available, otherwise fall back to defaults
        std::vector<HHD_MarkerEntry> markers;
        if (!activeMarkers.empty())
        {
            markers = activeMarkers;
        }
        else
        {
            // Default: TCM 1-2, LED 1-3
            for (uint8_t tcm = 1; tcm <= 2; ++tcm)
                for (uint8_t led = 1; led <= 3; ++led)
                    markers.push_back({tcm, led, 1});
        }

        std::cout << "Starting measurement on " << tracker.portName << " at 10 Hz (" << markers.size() << " markers)..." << std::endl;

        session = StartMeasurement(hPort, 10, markers);
        if (session)
        {
            measureStartTick        = GetTickCount64();
            std::string logFilename = GenerateLogFilename();
            logFile.open(logFilename, std::ios::out | std::ios::trunc);
            if (logFile.is_open())
                std::cout << "Logging to " << logFilename << std::endl;
            frameBuffer.clear();
            return true;
        }
        else
        {
            std::cout << "Failed to start measurement." << std::endl;
            CloseHandle(hPort);
            hPort = INVALID_HANDLE_VALUE;
            return false;
        }
    };

    // Helper: stop the current measurement session and close port.
    auto stopCurrentMeasurement = [&]()
    {
        WriteFrameNdjson(logFile, frameBuffer);
        frameBuffer.clear();
        if (logFile.is_open())
            logFile.close();
        StopMeasurement(session);
        session = nullptr;
        if (hPort != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hPort);
            hPort = INVALID_HANDLE_VALUE;
        }
    };

    while (true)
    {
        // --- Check for keyboard input (non-blocking) ---
        if (_kbhit())
        {
            int ch = _getch();

            // 'h' — scan COM1-COM16 for HHD devices
            if (ch == 'h' || ch == 'H')
            {
                std::cout << "\n--- Scanning COM1-COM16 for HHD devices ---" << std::endl;
                detectedTrackers.clear();
                for (int i = 1; i <= 16; ++i)
                {
                    std::string portName = "COM" + std::to_string(i);
                    std::string portPath = "\\\\.\\" + portName;
                    HANDLE      hTest    = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hTest == INVALID_HANDLE_VALUE)
                        continue;
                    CloseHandle(hTest);

                    std::cout << "Probing " << portName << "..." << std::endl;
                    HHD_DetectionResult result = Detect_HHD(portName);
                    if (result.deviceFound)
                    {
                        std::cout << "  FOUND on " << result.portName;
                        if (!result.serialNumber.empty())
                            std::cout << "  Serial: " << result.serialNumber;
                        std::cout << "  Baud: " << result.detectedBaudRate << std::endl;
                        detectedTrackers.push_back({result.portName, result.detectedBaudRate, result.serialNumber});
                    }
                }
                if (!detectedTrackers.empty())
                    SaveDetectionSettings(detectedTrackers);
                std::cout << "--- Scan complete: " << detectedTrackers.size() << " tracker(s) found ---\n" << std::endl;
            }

            // 'd' — detect marker configuration (auto-scan)
            else if (ch == 'd' || ch == 'D')
            {
                if (session)
                {
                    std::cout << "Measurement already running. Press 't' to stop first." << std::endl;
                    continue;
                }
                if (detectedTrackers.empty())
                {
                    std::cout << "No device detected yet. Press 'h' to scan first." << std::endl;
                    continue;
                }

                const auto &tracker  = detectedTrackers[0];
                std::string portPath = "\\\\.\\" + tracker.portName;
                HANDLE      hProbe   = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hProbe == INVALID_HANDLE_VALUE)
                {
                    std::cout << "Failed to open " << tracker.portName << std::endl;
                    continue;
                }

                // Configure port for the probe
                DCB dcb       = {};
                dcb.DCBlength = sizeof(dcb);
                GetCommState(hProbe, &dcb);
                dcb.BaudRate          = tracker.baudRate;
                dcb.ByteSize          = 8;
                dcb.StopBits          = ONESTOPBIT;
                dcb.Parity            = NOPARITY;
                dcb.fDtrControl       = DTR_CONTROL_ENABLE;
                dcb.fOutxCtsFlow      = TRUE;
                dcb.fOutxDsrFlow      = TRUE;
                dcb.fDsrSensitivity   = TRUE;
                dcb.fTXContinueOnXoff = TRUE;
                dcb.XonLim            = (tracker.baudRate == 2000000) ? 22 : 82;
                dcb.XoffLim           = 0;
                SetCommState(hProbe, &dcb);
                EscapeCommFunction(hProbe, SETRTS);
                EscapeCommFunction(hProbe, SETDTR);

                std::cout << "\n--- Auto-detecting marker configuration ---" << std::endl;

                HHD_ConfigDetectOptions opts;
                opts.maxTcmId = 8;
                opts.maxLedId = 16;

                auto config   = ConfigDetect(hProbe, opts);
                CloseHandle(hProbe);

                if (config.success && !config.markerList.empty())
                {
                    activeMarkers = config.markerList;
                    SaveMarkerConfig(config);
                    std::cout << "--- " << config.summary << " ---\n" << std::endl;
                }
                else
                {
                    std::cout << "--- No markers detected";
                    if (!config.summary.empty())
                        std::cout << ": " << config.summary;
                    std::cout << " ---\n" << std::endl;
                }
            }

            // 's' — start measurement (single run)
            else if (ch == 's' || ch == 'S')
            {
                if (session)
                {
                    std::cout << "Measurement already running. Press 't' to stop first." << std::endl;
                    continue;
                }
                if (detectedTrackers.empty())
                {
                    std::cout << "No device detected yet. Press 'h' to scan first." << std::endl;
                    continue;
                }

                cycling = false;
                if (startMeasurementOnTracker())
                    std::cout << "Measurement started (will auto-stop in " << (MEASURE_DURATION_MS / 1000) << "s)." << std::endl;
            }

            // 'c' — cycle: start/stop repeatedly
            else if (ch == 'c' || ch == 'C')
            {
                if (session)
                {
                    std::cout << "Measurement already running. Press 't' to stop first." << std::endl;
                    continue;
                }
                if (detectedTrackers.empty())
                {
                    std::cout << "No device detected yet. Press 'h' to scan first." << std::endl;
                    continue;
                }

                cycling    = true;
                cycleCount = 1;
                std::cout << "\n=== CYCLE MODE: measuring " << (MEASURE_DURATION_MS / 1000) << "s per cycle (press 't' to stop) ===" << std::endl;
                std::cout << "--- Cycle " << cycleCount << " ---" << std::endl;
                if (!startMeasurementOnTracker())
                    cycling = false;
            }

            // 't' — stop measurement (also stops cycling)
            else if (ch == 't' || ch == 'T')
            {
                if (!session && !cycling)
                {
                    std::cout << "No measurement running." << std::endl;
                    continue;
                }
                if (cycling)
                    std::cout << "Stopping cycle mode..." << std::endl;
                else
                    std::cout << "Stopping measurement..." << std::endl;
                cycling = false;
                if (session)
                    stopCurrentMeasurement();
                std::cout << "Measurement stopped." << std::endl;
            }

            // 'q' — quit
            else if (ch == 'q' || ch == 'Q')
            {
                std::cout << "SHUTTING DOWN " << std::endl;
                cycling = false;
                if (session)
                    stopCurrentMeasurement();
                break;
            }
        }

        // --- Auto-stop after MEASURE_DURATION_MS ---
        if (session && (GetTickCount64() - measureStartTick) >= MEASURE_DURATION_MS)
        {
            std::cout << "\n" << (MEASURE_DURATION_MS / 1000) << " seconds elapsed — stopping measurement." << std::endl;
            stopCurrentMeasurement();

            if (cycling)
            {
                // Restart for the next cycle
                cycleCount++;
                std::cout << "--- Cycle " << cycleCount << " ---" << std::endl;
                if (!startMeasurementOnTracker())
                {
                    std::cout << "Cycle aborted — failed to restart measurement." << std::endl;
                    cycling = false;
                }
            }
            else
            {
                std::cout << "Measurement stopped." << std::endl;
            }
        }

        // --- Fetch and display measurement data ---
        if (session)
        {
            std::vector<HHD_MeasurementSample> samples;
            FetchMeasurements(session, samples);
            for (const auto &s : samples)
            {
                std::cout << "t=" << std::setw(10) << s.timestamp_us << " TCM" << (int)s.tcmId << " LED" << std::setw(2) << (int)s.ledId << std::fixed
                          << std::setprecision(2) << " x=" << std::setw(9) << s.x_mm << " y=" << std::setw(9) << s.y_mm << " z=" << std::setw(9) << s.z_mm
                          << "  amb=" << (int)s.ambientLight << " R:" << (int)s.rightEyeStatus << " C:" << (int)s.centerEyeStatus
                          << " L:" << (int)s.leftEyeStatus << (s.endOfFrame ? " EOF" : "") << std::endl;

                frameBuffer.push_back(s);
                if (s.endOfFrame)
                {
                    WriteFrameNdjson(logFile, frameBuffer);
                    frameBuffer.clear();
                }
            }
        }

        Sleep(1); // avoid busy-waiting
    }

    return 0;
}
