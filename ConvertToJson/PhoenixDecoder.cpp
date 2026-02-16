#include "PhoenixDecoder.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>

// Read big-endian unsigned 32-bit from buffer
static uint32_t readU32BE(const uint8_t *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Read big-endian signed 24-bit from buffer
static int32_t readS24BE(const uint8_t *p)
{
    int32_t val = (int32_t(p[0]) << 16) | (int32_t(p[1]) << 8) | int32_t(p[2]);
    // Sign-extend from 24 to 32 bits
    if (val & 0x800000)
        val |= 0xFF000000;
    return val;
}

std::string PhoenixInitMessage::serialNumberHex() const
{
    std::ostringstream ss;
    for (int i = 0; i < 8; ++i)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << int(serialNumber[i]);
    }
    return ss.str();
}

std::string PhoenixCommand::description() const
{
    std::string        name = PhoenixDecoder::commandName(commandCode);
    std::ostringstream ss;
    ss << "&" << commandCode << commandIndex;
    ss << " (" << name << ")";

    if (!params.empty())
    {
        ss << " params=[";
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                ss << " ";
            ss << std::hex << std::setw(2) << std::setfill('0') << int(params[i]);
        }
        ss << "]";
    }
    return ss.str();
}

std::string PhoenixDecoder::commandName(char code)
{
    switch (code)
    {
        // Reset
        case '`':
            return "Software Reset";

        // Settings
        case 'L':
            return "Set Signal Quality Requirement (SQR)";
        case 'O':
            return "Set Minimum Signal Requirement (MSR)";
        case 'P':
            return "Enable Double Sampling";
        case 'Q':
            return "Enable Single Sampling";
        case 'S':
            return "Enable Internal Triggering";
        case 'U':
            return "Set Sample Operation Time (SOT)";
        case 'V':
            return "Set Manual Exposure";
        case 'W':
            return "Enable Automatic Exposure";
        case 'X':
            return "Set Multi-Rate Sampling Mode";
        case 'Y':
            return "Set Auto-Exposure Gain";
        case '6':
            return "Set Number of Capture Cycles";
        case '7':
            return "Ping";
        case 'u':
            return "Toggle Marker On/Off";
        case 'v':
            return "Set Sampling/Intermission Period";
        case '^':
            return "Enable Tether Mode";
        case '_':
            return "Enable Tetherless Mode";

        // Marker control
        case 'n':
            return "TCM Sync on First-TCMID";
        case 'o':
            return "TCM Sync on End-Of-Frame";
        case 'p':
            return "Target Flashing Sequence (TFS)";
        case 'q':
            return "Ready All TCMs";
        case 'r':
            return "Program TFS Into TCMs";
        case ']':
            return "Reset All TCMs";

        // Capture actions
        case '3':
            return "Start Periodic Sampling";
        case '5':
            return "Stop Periodic Sampling";
        case 'G':
            return "Activate Vibrator";
        case 'N':
            return "Wait for Pulse then Start";
        case 'R':
            return "Enable External Triggering";

        // Internal/factory
        case '=':
            return "Return Raw Sensor Data";
        case '<':
            return "Return 3D Coordinates";
        case ';':
            return "Return Raw + 3D";
        case '9':
            return "Enable Refraction Compensation";
        case ':':
            return "Disable Refraction Compensation";
        case 'Z':
            return "Set Desired Signal Peak";
        case 'K':
            return "External Start + External Trigger";
        case 'J':
            return "Fetch Misalignment Parameter";
        case 'M':
            return "Change Misalignment Parameter";
        case 'x':
            return "Burn Misalignment to ROM";

        case '?':
            return "Query/Identify";

        default:
        {
            std::string s = "Unknown Command '";
            s += code;
            s += "'";
            return s;
        }
    }
}

std::string PhoenixDecoder::eyeStatusDescription(uint8_t status)
{
    switch (status)
    {
        case 0:
            return "No anomaly";
        case 2:
            return "Raw signal weak (NUC_PEAK_LOW)";
        case 3:
            return "Processed signal too weak (COR_HUMP_LOW)";
        case 4:
            return "Raw signal saturated (NUC_PEAK_HIGH)";
        case 5:
            return "Processed signal out of range (COR_SPACING_RANGE)";
        case 6:
            return "Signal noisy (NUC_HUMPS_FEW)";
        case 9:
            return "LR indeterminate (COR_ID_INDETERM_LR)";
        case 10:
            return "UD indeterminate (COR_ID_INDETERM_UD)";
        case 12:
            return "No signal (NUC_NOISE_ONLY)";
        case 14:
            return "Center out of range (COR_CENT_OUT_RANGE)";
        default:
            return "Unknown (" + std::to_string(status) + ")";
    }
}

PhoenixDataSet PhoenixDecoder::decodeDataSet(const uint8_t *p)
{
    PhoenixDataSet ds{};
    ds.timestamp_us    = readU32BE(p);
    ds.x               = readS24BE(p + 4);
    ds.y               = readS24BE(p + 7);
    ds.z               = readS24BE(p + 10);
    ds.statusWord      = readU32BE(p + 13);
    ds.ledId           = p[17] & 0x7F;
    ds.tcmId           = p[18] & 0x0F;

    // Decode status word fields
    // Byte 14 (MSB of status): E | HHH | mmmm
    uint8_t b14        = p[13];
    ds.endOfFrame      = (b14 >> 7) & 1;
    ds.coordStatus     = (b14 >> 4) & 0x07;
    ds.ambientLight    = b14 & 0x0F;

    // Byte 15: 111 | La | AAAA  (but 111 are part of check pattern? No, they're status)
    // Actually per the protocol doc:
    // Status word is bytes 14-17 of the 19-byte frame (0-indexed: bytes 13-16)
    // Byte 14: E_HHH_mmmm
    // Byte 15: TTT_La_AAAA -> wait, let me re-check
    // From the protocol doc:
    //   Byte 14 (MSB): E | HHH | mmmm
    //   Byte 15:        111 | La | AAAA
    //   Byte 16:        TTT | Lb | BBBB
    //   Byte 17 (LSB):  TTT | Lc | CCCC
    uint8_t b15        = p[14];
    ds.rightEyeSignal  = (b15 >> 4) & 0x01;
    ds.rightEyeStatus  = b15 & 0x0F;

    uint8_t b16        = p[15];
    ds.centerEyeSignal = (b16 >> 4) & 0x01;
    ds.centerEyeStatus = b16 & 0x0F;

    uint8_t b17        = p[16];
    ds.leftEyeSignal   = (b17 >> 4) & 0x01;
    ds.leftEyeStatus   = b17 & 0x0F;

    // Trigger index: upper 3 bits of byte 16 + upper 3 bits of byte 17
    ds.triggerIndex    = ((b16 >> 5) & 0x07) << 3 | ((b17 >> 5) & 0x07);

    return ds;
}

PhoenixMessage PhoenixDecoder::decodeMessage(const uint8_t *p)
{
    PhoenixMessage msg{};
    msg.commandCode  = static_cast<char>(p[0]);
    msg.commandIndex = static_cast<char>(p[1]);
    msg.messageParam = p[13];
    msg.messageId    = p[14];
    std::memcpy(msg.checkBytes, p + 15, 4);
    return msg;
}

PhoenixInitMessage PhoenixDecoder::decodeInitMessage(const uint8_t *p)
{
    PhoenixInitMessage init{};
    std::memcpy(init.serialNumber, p + 4, 8);
    init.statusByte = p[14];
    std::memcpy(init.checkBytes, p + 15, 4);
    return init;
}

void PhoenixDecoder::decodeTxRecord(uint64_t irpTimestamp, const std::vector<uint8_t> &data, std::vector<PhoenixFrame> &frames)
{
    // TX data contains one or more commands.
    // Each command: '&' + code + index + bytesPerParam + numParams + CR + binary params
    size_t pos = 0;
    while (pos < data.size())
    {
        // Find the next '&' (command start marker)
        if (data[pos] != 0x26)
        { // '&'
            ++pos;
            continue;
        }

        if (pos + 6 > data.size())
            break;

        PhoenixFrame frame;
        frame.type          = PhoenixFrameType::Command;
        frame.irpTimestamp  = irpTimestamp;
        frame.isTx          = true;

        PhoenixCommand &cmd = frame.command;
        cmd.commandCode     = static_cast<char>(data[pos + 1]);
        cmd.commandIndex    = static_cast<char>(data[pos + 2]);
        cmd.bytesPerParam   = data[pos + 3] - '0';
        cmd.numParams       = data[pos + 4] - '0';
        // data[pos + 5] should be 0x0D (CR)

        size_t paramBytes   = static_cast<size_t>(cmd.bytesPerParam) * cmd.numParams;
        size_t totalLen     = 6 + paramBytes;

        // Collect raw bytes
        size_t rawEnd       = std::min(pos + totalLen, data.size());
        frame.rawBytes.assign(data.begin() + pos, data.begin() + rawEnd);

        // Extract parameter bytes
        if (paramBytes > 0 && pos + 6 + paramBytes <= data.size())
        {
            cmd.params.assign(data.begin() + pos + 6, data.begin() + pos + 6 + paramBytes);
        }

        frames.push_back(std::move(frame));
        pos += totalLen;
    }
}

void PhoenixDecoder::decodeRxRecord(uint64_t irpTimestamp, const std::vector<uint8_t> &data, std::vector<PhoenixFrame> &frames)
{
    // RX data contains one or more 19-byte frames.
    // All RX packets are exact multiples of 19 bytes.
    if (data.size() % 19 != 0)
    {
        // Unexpected size - store as unknown
        PhoenixFrame frame;
        frame.type         = PhoenixFrameType::Unknown;
        frame.irpTimestamp = irpTimestamp;
        frame.isTx         = false;
        frame.rawBytes     = data;
        frames.push_back(std::move(frame));
        return;
    }

    size_t numFrames = data.size() / 19;
    for (size_t i = 0; i < numFrames; ++i)
    {
        const uint8_t *p = data.data() + i * 19;

        PhoenixFrame frame;
        frame.irpTimestamp = irpTimestamp;
        frame.isTx         = false;
        frame.rawBytes.assign(p, p + 19);

        // Classify the 19-byte frame:
        // 1. Init message: bytes 0-3 = {01, 02, 03, 04}
        if (p[0] == 0x01 && p[1] == 0x02 && p[2] == 0x03 && p[3] == 0x04)
        {
            frame.type        = PhoenixFrameType::InitMessage;
            frame.initMessage = decodeInitMessage(p);
        }
        // 2. MESSAGE set: byte 14 == ACK (0x06) and byte 0 is a printable command code
        else if (p[14] == 0x06 && p[0] >= 0x20 && p[0] < 0x7F)
        {
            frame.type    = PhoenixFrameType::Message;
            frame.message = decodeMessage(p);
        }
        // 3. DATA set: byte 18 has upper nibble 0xE0 and byte 17 has bit 7 set
        else if ((p[18] & 0xF0) == 0xE0 && (p[17] & 0x80) == 0x80)
        {
            frame.type    = PhoenixFrameType::DataSet;
            frame.dataSet = decodeDataSet(p);
        }
        // 4. Also check for MESSAGE sets with error codes (messageId != 0x06)
        else if (p[0] >= 0x20 && p[0] < 0x7F && p[1] >= 0x20 && p[1] < 0x7F)
        {
            // Could be an error message response
            frame.type    = PhoenixFrameType::Message;
            frame.message = decodeMessage(p);
        }
        else
        {
            frame.type = PhoenixFrameType::Unknown;
        }

        frames.push_back(std::move(frame));
    }
}

void PhoenixDecoder::decode(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> &txRecords,
                            const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> &rxRecords, std::vector<PhoenixFrame> &frames)
{

    for (const auto &[ts, data] : txRecords)
    {
        decodeTxRecord(ts, data, frames);
    }

    for (const auto &[ts, data] : rxRecords)
    {
        decodeRxRecord(ts, data, frames);
    }

    // Sort all frames by IRP timestamp for chronological output
    std::stable_sort(frames.begin(), frames.end(), [](const PhoenixFrame &a, const PhoenixFrame &b) { return a.irpTimestamp < b.irpTimestamp; });
}
