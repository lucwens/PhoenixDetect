#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---- Phoenix VZK10 protocol structures ----

// 3D coordinate data set (19 bytes from tracker)
struct PhoenixDataSet
{
    uint32_t timestamp_us; // Microseconds since capture start
    int32_t  x;            // X in units of 10um (signed 24-bit)
    int32_t  y;            // Y in units of 10um (signed 24-bit)
    int32_t  z;            // Z in units of 10um (signed 24-bit)
    uint32_t statusWord;   // 32-bit status bit field
    uint8_t  ledId;        // LED ID (1-64)
    uint8_t  tcmId;        // TCM ID (1-8)

    // Decoded status fields
    bool    endOfFrame;
    uint8_t coordStatus;     // HHH: 0=no error
    uint8_t ambientLight;    // mmmm: max ambient light level
    uint8_t rightEyeSignal;  // La
    uint8_t rightEyeStatus;  // AAAA
    uint8_t centerEyeSignal; // Lb
    uint8_t centerEyeStatus; // BBBB
    uint8_t leftEyeSignal;   // Lc
    uint8_t leftEyeStatus;   // CCCC
    uint8_t triggerIndex;    // 6-bit trigger index

    // Convenience: coordinates in mm
    double x_mm() const { return x * 0.01; }
    double y_mm() const { return y * 0.01; }
    double z_mm() const { return z * 0.01; }
};

// ACK/ERR message set (19 bytes from tracker)
struct PhoenixMessage
{
    char    commandCode;
    char    commandIndex;
    uint8_t messageParam;
    uint8_t messageId; // 0x06 = ACK
    uint8_t checkBytes[4];

    bool isAck() const { return messageId == 0x06; }
};

// Initialization message (19 bytes, first response after reset)
struct PhoenixInitMessage
{
    uint8_t serialNumber[8];
    uint8_t statusByte; // 0x01 = initialized
    uint8_t checkBytes[4];

    std::string serialNumberHex() const;
};

// Command sent from host to tracker
struct PhoenixCommand
{
    char                 commandCode;
    char                 commandIndex;
    uint8_t              bytesPerParam; // ASCII digit '0'-'9' -> 0-9
    uint8_t              numParams;     // ASCII digit '0'-'9' -> 0-9
    std::vector<uint8_t> params;

    std::string description() const;
};

// Type of a decoded Phoenix frame
enum class PhoenixFrameType
{
    Command,     // TX: host -> tracker
    DataSet,     // RX: 3D coordinate data
    Message,     // RX: ACK/ERR message
    InitMessage, // RX: initialization message
    Unknown
};

// A single decoded frame with its IRP timestamp
struct PhoenixFrame
{
    PhoenixFrameType type;
    uint64_t         irpTimestamp; // Windows FILETIME from the IRP record
    bool             isTx;         // true = host->device, false = device->host

    // Only one of these is populated depending on type
    PhoenixCommand       command;
    PhoenixDataSet       dataSet;
    PhoenixMessage       message;
    PhoenixInitMessage   initMessage;
    std::vector<uint8_t> rawBytes;
};

class PhoenixDecoder
{
  public:
    // Decode all frames from IRP serial data records.
    // irpRecords: pairs of (irpTimestamp, serialData) - already separated into TX/RX by DmsLogReader.
    void decode(const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> &txRecords,
                const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> &rxRecords, std::vector<PhoenixFrame> &frames);

    // Get human-readable command name
    static std::string commandName(char code);

    // Get eye status description
    static std::string eyeStatusDescription(uint8_t status);

  private:
    void               decodeTxRecord(uint64_t irpTimestamp, const std::vector<uint8_t> &data, std::vector<PhoenixFrame> &frames);
    void               decodeRxRecord(uint64_t irpTimestamp, const std::vector<uint8_t> &data, std::vector<PhoenixFrame> &frames);
    PhoenixDataSet     decodeDataSet(const uint8_t *p);
    PhoenixMessage     decodeMessage(const uint8_t *p);
    PhoenixInitMessage decodeInitMessage(const uint8_t *p);
};
