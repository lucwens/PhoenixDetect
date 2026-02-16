#include "DmsLogReader.h"

#include <algorithm>
#include <cstring>
#include <iostream>

// Expected file-format GUID: {3423D0D9-F6E4-49E9-9F1C-E2D7953CA8EA}
static const uint8_t kFileGuid[16] = {0xD9, 0xD0, 0x23, 0x34, 0xE4, 0xF6, 0xE9, 0x49, 0x9F, 0x1C, 0xE2, 0xD7, 0x95, 0x3C, 0xA8, 0xEA};

// Helper: read a little-endian uint32 from a byte buffer.
static uint32_t readU32LE(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Helper: read a little-endian uint64 from a byte buffer.
static uint64_t readU64LE(const uint8_t *p)
{
    return uint64_t(readU32LE(p)) | (uint64_t(readU32LE(p + 4)) << 32);
}

// Helper: check if a FILETIME high-word is close to the session high-word.
// The high 32 bits of a Windows FILETIME tick over roughly every 429 seconds,
// so a long capture session may see the high word increment by 1 or more.
static bool tsHighMatches(uint32_t candidate, uint32_t session, uint32_t tolerance = 2)
{
    uint32_t diff = (candidate >= session) ? (candidate - session) : (session - candidate);
    return diff <= tolerance;
}

// Helper: read a UTF-16LE string of given char count from a byte buffer.
static std::string utf16leToAscii(const uint8_t *p, size_t charCount)
{
    std::string result;
    result.reserve(charCount);
    for (size_t i = 0; i < charCount; ++i)
    {
        uint16_t ch = uint16_t(p[i * 2]) | (uint16_t(p[i * 2 + 1]) << 8);
        if (ch == 0)
            break;
        if (ch < 128)
            result.push_back(static_cast<char>(ch));
        else
            result.push_back('?');
    }
    return result;
}

bool DmsLogReader::open(const std::string &path)
{
    m_path = path;
    m_file.open(path, std::ios::binary);
    if (!m_file.is_open())
    {
        std::cerr << "Error: cannot open file: " << path << "\n";
        return false;
    }

    m_file.seekg(0, std::ios::end);
    m_fileSize = static_cast<uint64_t>(m_file.tellg());
    m_file.seekg(0);

    if (!readHeader())
        return false;
    readSessionMetadata();
    scanForPortConfig();
    return true;
}

bool DmsLogReader::readHeader()
{
    uint8_t buf[48];
    m_file.seekg(0);
    m_file.read(reinterpret_cast<char *>(buf), 48);
    if (!m_file.good())
    {
        std::cerr << "Error: file too small for header\n";
        return false;
    }

    // Validate GUID
    if (std::memcmp(buf, kFileGuid, 16) != 0)
    {
        std::cerr << "Error: invalid dmslog8 file signature\n";
        return false;
    }

    std::memcpy(m_header.guid, buf, 16);
    m_header.sessionTimestamp = readU64LE(buf + 0x18);
    m_header.dataOffset       = readU64LE(buf + 0x20);

    m_tsHigh                  = static_cast<uint32_t>(m_header.sessionTimestamp >> 32);
    return true;
}

bool DmsLogReader::readSessionMetadata()
{
    // Session metadata starts at 0x10000
    const uint64_t metaOffset = 0x10000;
    if (m_fileSize < metaOffset + 256)
        return false;

    std::vector<uint8_t> buf(0x2000);
    m_file.seekg(metaOffset);
    m_file.read(reinterpret_cast<char *>(buf.data()), buf.size());

    // Skip two GUIDs (32 bytes) + flags (8 bytes) + FILETIME (8 bytes) = offset 0x30
    size_t pos = 0x30;

    // Read "Empty" string
    if (pos + 2 > buf.size())
        return false;
    uint16_t strLen = uint16_t(buf[pos]) | (uint16_t(buf[pos + 1]) << 8);
    pos += 2;
    pos += (strLen + 1) * 2; // skip string + null terminator

    // Skip separator (4 bytes) + FILETIME (8 bytes)
    pos += 12;

    // Read device name string
    if (pos + 2 > buf.size())
        return false;
    strLen = uint16_t(buf[pos]) | (uint16_t(buf[pos + 1]) << 8);
    pos += 2;
    if (pos + strLen * 2 <= buf.size())
    {
        m_header.deviceName = utf16leToAscii(buf.data() + pos, strLen);
    }

    return true;
}

bool DmsLogReader::scanForPortConfig()
{
    // Port config strings appear as UTF-16LE text in metadata blocks between IRP records.
    // Format: "N,NNN,NNN, data bits: N, stop bits: N, parity: Xxxx"
    // They follow a 2F 02 00 C0 marker + count(4) + FILETIME(8).
    // Scan a broad range starting from the data section area.

    const uint64_t scanStart = 0x12000;
    const uint64_t scanEnd   = std::min(m_fileSize, uint64_t(0x20000));
    if (m_fileSize < scanStart + 64)
        return false;

    size_t               scanLen = static_cast<size_t>(scanEnd - scanStart);
    std::vector<uint8_t> buf(scanLen);
    m_file.seekg(scanStart);
    m_file.read(reinterpret_cast<char *>(buf.data()), scanLen);

    // Search for "parity: " in UTF-16LE as anchor - it's at the end of each config string
    // and lets us extract the full string backwards to the baud rate.
    const uint8_t parityNeedle[] = {'p', 0, 'a', 0, 'r', 0, 'i', 0, 't', 0, 'y', 0, ':', 0, ' ', 0};
    const size_t  needleLen      = sizeof(parityNeedle);

    std::string bestConfig;

    for (size_t i = 0; i + needleLen < buf.size(); i += 2)
    {
        if (std::memcmp(buf.data() + i, parityNeedle, needleLen) != 0)
            continue;

        // Found "parity: " - read forward to get the parity value (e.g. "None")
        size_t end = i + needleLen;
        while (end + 1 < buf.size())
        {
            uint16_t ch = uint16_t(buf[end]) | (uint16_t(buf[end + 1]) << 8);
            if (ch == 0 || ch < 0x20 || ch > 0x7E)
                break;
            end += 2;
        }

        // Read backwards from "parity" to find the start of the config string.
        // Walk back to find the baud rate digits at the beginning.
        size_t start = i;
        while (start >= 2)
        {
            uint16_t ch = uint16_t(buf[start - 2]) | (uint16_t(buf[start - 1]) << 8);
            if (ch == 0 || ch < 0x20 || ch > 0x7E)
                break;
            start -= 2;
        }

        size_t charCount = (end - start) / 2;
        if (charCount < 10 || charCount > 200)
            continue;

        std::string config = utf16leToAscii(buf.data() + start, charCount);

        // Clean up boundaries:
        // 1. The DMS structure places a one-byte field right before the text that may be
        //    a printable ASCII character (e.g. '3', '0'). Locate ", data bits:" as anchor
        //    and extract the baud rate from right before it.
        auto dbPos         = config.find(", data bits:");
        if (dbPos == std::string::npos)
            continue;

        // The baud rate is before ", data bits:". Walk backwards from dbPos
        // to find where the baud rate digits start. Skip any leading non-digit/non-comma chars.
        size_t baudStart = 0;
        for (size_t k = 0; k < dbPos; ++k)
        {
            char c = config[k];
            if (c >= '0' && c <= '9')
            {
                baudStart = k;
                break;
            }
        }
        // Check if the leading digit makes the baud rate unreasonably high (>10M).
        // Standard serial baud rates are at most a few million. If >10M, the first
        // digit is likely a stray byte from the DMS structure.
        std::string baudStr;
        for (size_t k = baudStart; k < dbPos; ++k)
        {
            if (config[k] != ',')
                baudStr += config[k];
        }
        long long baudVal = 0;
        try
        {
            baudVal = std::stoll(baudStr);
        }
        catch (...)
        {
        }
        if (baudVal > 10000000 && baudStart < dbPos)
        {
            // Skip the first digit
            ++baudStart;
        }

        // 2. Trim after the parity value. Valid serial parity values: None, Even, Odd, Mark, Space.
        std::string trimmed = config.substr(baudStart);
        auto        parPos  = trimmed.find("parity: ");
        if (parPos != std::string::npos)
        {
            size_t             valStart       = parPos + 8;
            static const char *parityValues[] = {"None", "Even", "Odd", "Mark", "Space"};
            for (const char *pv : parityValues)
            {
                size_t pvLen = std::strlen(pv);
                if (trimmed.compare(valStart, pvLen, pv) == 0)
                {
                    trimmed = trimmed.substr(0, valStart + pvLen);
                    break;
                }
            }
        }
        config = trimmed;

        // Prefer configs that have actual values (not '?')
        if (config.find('?') == std::string::npos)
        {
            bestConfig = config;
            // Keep searching for the last/most complete one
        }
        else if (bestConfig.empty())
        {
            bestConfig = config;
        }
    }

    if (!bestConfig.empty())
    {
        m_header.portConfig = bestConfig;
        return true;
    }
    return false;
}

bool DmsLogReader::findFirstRecord(uint64_t &outOffset)
{
    // IRP records start after the device configuration block, around offset 0x13000-0x13200.
    // Scan forward from the data section looking for valid record chains.
    const uint64_t scanStart = m_header.dataOffset + 0x1000; // ~0x13000
    const uint64_t scanEnd   = std::min(m_fileSize, scanStart + 0x2000);

    uint8_t buf[48];

    for (uint64_t pos = scanStart; pos < scanEnd; ++pos)
    {
        m_file.seekg(pos);
        m_file.read(reinterpret_cast<char *>(buf), 48);
        if (!m_file.good())
            break;

        uint64_t tsA       = readU64LE(buf);
        uint32_t recSize   = readU32LE(buf + 8);
        uint32_t typeFlags = readU32LE(buf + 12);
        uint32_t tsAHi     = static_cast<uint32_t>(tsA >> 32);
        uint32_t recType   = typeFlags & 0x7FFFFFFF;

        if (!tsHighMatches(tsAHi, m_tsHigh) || recSize < 24 || recSize > 10000 || recType < 1 || recType > 3)
            continue;

        // Validate: the next record at pos+recSize should also be valid
        uint64_t nextPos = pos + recSize;
        if (nextPos + 24 > m_fileSize)
            continue;

        m_file.seekg(nextPos);
        m_file.read(reinterpret_cast<char *>(buf), 24);
        if (!m_file.good())
            continue;

        uint64_t tsA2       = readU64LE(buf);
        uint32_t recSize2   = readU32LE(buf + 8);
        uint32_t typeFlags2 = readU32LE(buf + 12);
        uint32_t recType2   = typeFlags2 & 0x7FFFFFFF;

        if (tsHighMatches(static_cast<uint32_t>(tsA2 >> 32), m_tsHigh) && recSize2 >= 24 && recSize2 <= 10000 && recType2 >= 1 && recType2 <= 3)
        {
            outOffset = pos;
            return true;
        }
    }

    std::cerr << "Error: could not find first IRP record\n";
    return false;
}

bool DmsLogReader::readRecords(std::vector<IrpRecord> &records)
{
    uint64_t pos;
    if (!findFirstRecord(pos))
        return false;

    std::vector<uint8_t> buf;

    while (pos + 24 <= m_fileSize)
    {
        uint8_t hdr[24];
        m_file.seekg(pos);
        m_file.read(reinterpret_cast<char *>(hdr), 24);
        if (!m_file.good())
            break;

        uint64_t tsA          = readU64LE(hdr);
        uint32_t recSize      = readU32LE(hdr + 8);
        uint32_t typeFlags    = readU32LE(hdr + 12);
        uint64_t tsB          = readU64LE(hdr + 16);
        uint32_t tsAHi        = static_cast<uint32_t>(tsA >> 32);
        uint32_t recType      = typeFlags & 0x7FFFFFFF;
        bool     isCompletion = (typeFlags >> 31) & 1;

        if (tsHighMatches(tsAHi, m_tsHigh) && recSize >= 24 && recSize <= 10000 && recType >= 1 && recType <= 3)
        {
            // Valid record
            uint32_t payloadSize = recSize - 24;
            buf.resize(payloadSize);
            m_file.read(reinterpret_cast<char *>(buf.data()), payloadSize);

            if (recType == 1 && payloadSize > 17)
            {
                uint32_t funcCode  = readU32LE(buf.data() + 5);
                size_t   serialLen = payloadSize - 17;

                bool isTx          = (funcCode == 4 && !isCompletion && serialLen > 0);
                bool isRx          = (funcCode == 3 && isCompletion && serialLen > 0);

                if (isTx || isRx)
                {
                    IrpRecord rec;
                    rec.timestamp    = tsA;
                    rec.recordSize   = recSize;
                    rec.typeFlags    = typeFlags;
                    rec.timestampB   = tsB;
                    rec.recordType   = static_cast<int>(recType);
                    rec.isCompletion = isCompletion;
                    rec.ntstatus     = readU32LE(buf.data());
                    rec.infoByte     = buf[4];
                    rec.functionCode = funcCode;
                    rec.serialData.assign(buf.begin() + 9, buf.begin() + 9 + serialLen);
                    records.push_back(std::move(rec));
                }
            }

            pos += recSize;
        }
        else
        {
            // Not a valid record header - scan forward for next valid record.
            // This handles metadata gaps and 8-byte sequence markers.
            bool     found     = false;
            uint64_t scanLimit = std::min(pos + 5000, m_fileSize - 24);
            for (uint64_t scan = pos + 1; scan < scanLimit; ++scan)
            {
                m_file.seekg(scan);
                m_file.read(reinterpret_cast<char *>(hdr), 24);
                if (!m_file.good())
                    break;

                uint64_t tsCheck = readU64LE(hdr);
                uint32_t rsCheck = readU32LE(hdr + 8);
                uint32_t tfCheck = readU32LE(hdr + 12);
                uint32_t rtCheck = tfCheck & 0x7FFFFFFF;

                if (tsHighMatches(static_cast<uint32_t>(tsCheck >> 32), m_tsHigh) && rsCheck >= 24 && rsCheck <= 10000 && rtCheck >= 1 && rtCheck <= 3)
                {
                    pos   = scan;
                    found = true;
                    break;
                }
            }
            if (!found)
                break;
        }
    }

    return !records.empty();
}
