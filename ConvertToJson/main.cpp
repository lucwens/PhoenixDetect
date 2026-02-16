#include "DmsLogReader.h"
#include "PhoenixDecoder.h"
#include "JsonWriter.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void printUsage(const char *progName)
{
    std::cerr << "PTIConvert - Phoenix Visualeyez DMS Log to JSON Converter\n\n"
              << "Reads .dmslog8 files captured by HHD Device Monitoring Studio\n"
              << "from a Phoenix Visualeyez VZK10 RS-422 serial port and converts\n"
              << "the protocol data into human-readable JSON.\n\n"
              << "Usage:\n"
              << "  " << progName << " <input.dmslog8> [output.json]\n"
              << "  " << progName << " <directory>   (converts all .dmslog8 files)\n\n"
              << "If no output path is given, the output file is created alongside\n"
              << "the input with a .json extension.\n";
}

static bool convertFile(const std::string &inputPath, const std::string &outputPath)
{
    std::cout << "Converting: " << inputPath << "\n";

    // 1. Read the dmslog8 file
    DmsLogReader reader;
    if (!reader.open(inputPath))
        return false;

    const auto &hdr = reader.header();
    std::cout << "  Device: " << hdr.deviceName << "\n";
    if (!hdr.portConfig.empty())
    {
        std::cout << "  Port:   " << hdr.portConfig << "\n";
    }

    // 2. Extract serial data records
    std::vector<IrpRecord> records;
    if (!reader.readRecords(records))
    {
        std::cerr << "  Error: no serial data records found\n";
        return false;
    }

    // Separate into TX and RX
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> txRecords, rxRecords;
    for (const auto &rec : records)
    {
        auto entry = std::make_pair(rec.timestamp, rec.serialData);
        if (rec.functionCode == 4 && !rec.isCompletion)
        {
            txRecords.push_back(std::move(entry));
        }
        else
        {
            rxRecords.push_back(std::move(entry));
        }
    }

    std::cout << "  TX packets: " << txRecords.size() << "\n";
    std::cout << "  RX packets: " << rxRecords.size() << "\n";

    // 3. Decode Phoenix protocol
    PhoenixDecoder            decoder;
    std::vector<PhoenixFrame> frames;
    decoder.decode(txRecords, rxRecords, frames);

    // Count frame types
    int commands = 0, dataSets = 0, messages = 0, initMsgs = 0;
    for (const auto &f : frames)
    {
        switch (f.type)
        {
            case PhoenixFrameType::Command:
                ++commands;
                break;
            case PhoenixFrameType::DataSet:
                ++dataSets;
                break;
            case PhoenixFrameType::Message:
                ++messages;
                break;
            case PhoenixFrameType::InitMessage:
                ++initMsgs;
                break;
            default:
                break;
        }
    }

    std::cout << "  Decoded frames: " << frames.size() << " (commands=" << commands << ", dataSets=" << dataSets << ", messages=" << messages
              << ", init=" << initMsgs << ")\n";

    // 4. Write JSON output
    JsonWriter writer;
    if (!writer.write(outputPath, hdr, inputPath, frames))
        return false;

    std::cout << "  Output: " << outputPath << "\n";
    return true;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputArg = argv[1];
    fs::path    inputPath(inputArg);

    if (!fs::exists(inputPath))
    {
        std::cerr << "Error: path does not exist: " << inputArg << "\n";
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> filesToConvert;

    if (fs::is_directory(inputPath))
    {
        // Convert all .dmslog8 files in the directory
        for (const auto &entry : fs::directory_iterator(inputPath))
        {
            if (entry.is_regular_file())
            {
                std::string ext = entry.path().extension().string();
                // Match .dmslog8 or similar extensions
                if (ext.find(".dmslog") == 0)
                {
                    std::string outPath = entry.path().string();
                    // Replace extension with .json
                    size_t      dotPos  = outPath.rfind(".dmslog");
                    if (dotPos != std::string::npos)
                    {
                        outPath = outPath.substr(0, dotPos) + ".json";
                    }
                    else
                    {
                        outPath += ".json";
                    }
                    filesToConvert.emplace_back(entry.path().string(), outPath);
                }
            }
        }
        if (filesToConvert.empty())
        {
            std::cerr << "No .dmslog files found in: " << inputArg << "\n";
            return 1;
        }
    }
    else
    {
        // Single file
        std::string outPath;
        if (argc >= 3)
        {
            outPath = argv[2];
        }
        else
        {
            outPath       = inputPath.string();
            size_t dotPos = outPath.rfind(".dmslog");
            if (dotPos != std::string::npos)
            {
                outPath = outPath.substr(0, dotPos) + ".json";
            }
            else
            {
                outPath += ".json";
            }
        }
        filesToConvert.emplace_back(inputPath.string(), outPath);
    }

    int successCount = 0;
    for (const auto &[inFile, outFile] : filesToConvert)
    {
        if (convertFile(inFile, outFile))
        {
            ++successCount;
        }
    }

    std::cout << "\nConverted " << successCount << "/" << filesToConvert.size() << " file(s).\n";
    return (successCount == static_cast<int>(filesToConvert.size())) ? 0 : 1;
}
