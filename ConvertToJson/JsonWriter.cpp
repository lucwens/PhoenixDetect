#include "JsonWriter.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

std::string JsonWriter::escapeJson(const std::string &s)
{
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                }
                else
                {
                    result += c;
                }
        }
    }
    return result;
}

std::string JsonWriter::hexString(const uint8_t *data, size_t len)
{
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << int(data[i]);
    }
    return ss.str();
}

std::string JsonWriter::filetimeToIso8601(uint64_t filetime)
{
    // Windows FILETIME: 100ns intervals since 1601-01-01
    // Convert to Unix epoch: subtract 116444736000000000 (100ns intervals between 1601 and 1970)
    const uint64_t kUnixEpochDiff = 116444736000000000ULL;
    if (filetime < kUnixEpochDiff)
        return "1970-01-01T00:00:00Z";

    uint64_t unixTime100ns = filetime - kUnixEpochDiff;
    time_t   unixSeconds   = static_cast<time_t>(unixTime100ns / 10000000ULL);
    uint32_t microseconds  = static_cast<uint32_t>((unixTime100ns % 10000000ULL) / 10);

    struct tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &unixSeconds);
#else
    gmtime_r(&unixSeconds, &tm_buf);
#endif

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06uZ", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min,
             tm_buf.tm_sec, microseconds);
    return std::string(buf);
}

bool JsonWriter::write(const std::string &outputPath, const DmsLogHeader &header, const std::string &sourceFile, const std::vector<PhoenixFrame> &frames)
{
    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::cerr << "Error: cannot create output file: " << outputPath << "\n";
        return false;
    }

    // Count frame types for summary
    int commandCount = 0, dataSetCount = 0, messageCount = 0, initCount = 0, unknownCount = 0;
    for (const auto &f : frames)
    {
        switch (f.type)
        {
            case PhoenixFrameType::Command:
                ++commandCount;
                break;
            case PhoenixFrameType::DataSet:
                ++dataSetCount;
                break;
            case PhoenixFrameType::Message:
                ++messageCount;
                break;
            case PhoenixFrameType::InitMessage:
                ++initCount;
                break;
            case PhoenixFrameType::Unknown:
                ++unknownCount;
                break;
        }
    }

    out << "{\n";

    // Metadata
    out << "  \"metadata\": {\n";
    out << "    \"sourceFile\": \"" << escapeJson(sourceFile) << "\",\n";
    out << "    \"sessionTimestamp\": \"" << filetimeToIso8601(header.sessionTimestamp) << "\",\n";
    out << "    \"device\": \"" << escapeJson(header.deviceName) << "\",\n";
    out << "    \"portConfig\": \"" << escapeJson(header.portConfig) << "\",\n";
    out << "    \"protocol\": \"Phoenix Visualeyez VZK10 RS-422\"\n";
    out << "  },\n";

    // Summary
    out << "  \"summary\": {\n";
    out << "    \"totalFrames\": " << frames.size() << ",\n";
    out << "    \"commands\": " << commandCount << ",\n";
    out << "    \"dataSets\": " << dataSetCount << ",\n";
    out << "    \"messages\": " << messageCount << ",\n";
    out << "    \"initMessages\": " << initCount << ",\n";
    out << "    \"unknownFrames\": " << unknownCount << "\n";
    out << "  },\n";

    // Frames
    out << "  \"frames\": [\n";

    for (size_t i = 0; i < frames.size(); ++i)
    {
        const auto &f = frames[i];
        out << "    {\n";
        out << "      \"index\": " << i << ",\n";
        out << "      \"timestamp\": \"" << filetimeToIso8601(f.irpTimestamp) << "\",\n";
        out << "      \"direction\": \"" << (f.isTx ? "TX" : "RX") << "\",\n";

        switch (f.type)
        {
            case PhoenixFrameType::Command:
            {
                const auto &cmd = f.command;
                out << "      \"type\": \"command\",\n";
                out << "      \"command\": {\n";
                out << "        \"code\": \"" << escapeJson(std::string(1, cmd.commandCode)) << "\",\n";
                out << "        \"index\": \"" << escapeJson(std::string(1, cmd.commandIndex)) << "\",\n";
                out << "        \"name\": \"" << escapeJson(PhoenixDecoder::commandName(cmd.commandCode)) << "\",\n";
                out << "        \"bytesPerParam\": " << int(cmd.bytesPerParam) << ",\n";
                out << "        \"numParams\": " << int(cmd.numParams) << ",\n";
                out << "        \"description\": \"" << escapeJson(cmd.description()) << "\"";
                if (!cmd.params.empty())
                {
                    out << ",\n        \"params\": [";
                    for (size_t j = 0; j < cmd.params.size(); ++j)
                    {
                        if (j > 0)
                            out << ", ";
                        out << int(cmd.params[j]);
                    }
                    out << "]";

                    // Decode multi-byte parameter values
                    if (cmd.bytesPerParam > 1 && cmd.numParams > 0)
                    {
                        out << ",\n        \"paramValues\": [";
                        for (int pi = 0; pi < cmd.numParams; ++pi)
                        {
                            if (pi > 0)
                                out << ", ";
                            size_t   offset = pi * cmd.bytesPerParam;
                            uint32_t val    = 0;
                            for (int bi = 0; bi < cmd.bytesPerParam && offset + bi < cmd.params.size(); ++bi)
                            {
                                val = (val << 8) | cmd.params[offset + bi];
                            }
                            out << val;
                        }
                        out << "]";
                    }
                }
                out << "\n      }";
                break;
            }

            case PhoenixFrameType::DataSet:
            {
                const auto &ds = f.dataSet;
                out << "      \"type\": \"dataSet\",\n";
                out << "      \"dataSet\": {\n";
                out << "        \"timestamp_us\": " << ds.timestamp_us << ",\n";
                out << std::fixed << std::setprecision(2);
                out << "        \"x_mm\": " << ds.x_mm() << ",\n";
                out << "        \"y_mm\": " << ds.y_mm() << ",\n";
                out << "        \"z_mm\": " << ds.z_mm() << ",\n";
                out << "        \"ledId\": " << int(ds.ledId) << ",\n";
                out << "        \"tcmId\": " << int(ds.tcmId) << ",\n";
                out << "        \"endOfFrame\": " << (ds.endOfFrame ? "true" : "false") << ",\n";

                out << "        \"status\": {\n";
                out << "          \"raw\": \"0x" << std::hex << std::setw(8) << std::setfill('0') << ds.statusWord << std::dec << "\",\n";
                out << "          \"coordStatus\": " << int(ds.coordStatus) << ",\n";
                out << "          \"ambientLight\": " << int(ds.ambientLight) << ",\n";
                out << "          \"triggerIndex\": " << int(ds.triggerIndex) << ",\n";
                out << "          \"rightEye\": {\n";
                out << "            \"signalLow\": " << (ds.rightEyeSignal ? "true" : "false") << ",\n";
                out << "            \"status\": " << int(ds.rightEyeStatus) << ",\n";
                out << "            \"description\": \"" << escapeJson(PhoenixDecoder::eyeStatusDescription(ds.rightEyeStatus)) << "\"\n";
                out << "          },\n";
                out << "          \"centerEye\": {\n";
                out << "            \"signalLow\": " << (ds.centerEyeSignal ? "true" : "false") << ",\n";
                out << "            \"status\": " << int(ds.centerEyeStatus) << ",\n";
                out << "            \"description\": \"" << escapeJson(PhoenixDecoder::eyeStatusDescription(ds.centerEyeStatus)) << "\"\n";
                out << "          },\n";
                out << "          \"leftEye\": {\n";
                out << "            \"signalLow\": " << (ds.leftEyeSignal ? "true" : "false") << ",\n";
                out << "            \"status\": " << int(ds.leftEyeStatus) << ",\n";
                out << "            \"description\": \"" << escapeJson(PhoenixDecoder::eyeStatusDescription(ds.leftEyeStatus)) << "\"\n";
                out << "          }\n";
                out << "        }\n";
                out << "      }";
                break;
            }

            case PhoenixFrameType::Message:
            {
                const auto &msg = f.message;
                out << "      \"type\": \"message\",\n";
                out << "      \"message\": {\n";
                out << "        \"commandCode\": \"" << escapeJson(std::string(1, msg.commandCode)) << "\",\n";
                out << "        \"commandIndex\": \"" << escapeJson(std::string(1, msg.commandIndex)) << "\",\n";
                out << "        \"commandName\": \"" << escapeJson(PhoenixDecoder::commandName(msg.commandCode)) << "\",\n";
                out << "        \"isAck\": " << (msg.isAck() ? "true" : "false") << ",\n";
                out << "        \"messageId\": " << int(msg.messageId) << ",\n";
                out << "        \"messageParam\": " << int(msg.messageParam) << "\n";
                out << "      }";
                break;
            }

            case PhoenixFrameType::InitMessage:
            {
                const auto &init = f.initMessage;
                out << "      \"type\": \"initMessage\",\n";
                out << "      \"initMessage\": {\n";
                out << "        \"serialNumber\": \"" << init.serialNumberHex() << "\",\n";
                out << "        \"status\": " << int(init.statusByte) << ",\n";
                out << "        \"initialized\": " << (init.statusByte == 0x01 ? "true" : "false") << "\n";
                out << "      }";
                break;
            }

            case PhoenixFrameType::Unknown:
            {
                out << "      \"type\": \"unknown\",\n";
                out << "      \"rawHex\": \"" << hexString(f.rawBytes.data(), f.rawBytes.size()) << "\"";
                break;
            }
        }

        if (!f.rawBytes.empty() && f.type != PhoenixFrameType::Unknown)
        {
            out << ",\n      \"rawHex\": \"" << hexString(f.rawBytes.data(), f.rawBytes.size()) << "\"";
        }

        out << "\n    }";
        if (i + 1 < frames.size())
            out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    if (!out.good())
    {
        std::cerr << "Error: write failed for " << outputPath << "\n";
        return false;
    }

    return true;
}
