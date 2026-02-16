#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// A single IRP record extracted from a dmslog8 file.
struct IrpRecord
{
    uint64_t timestamp; // Windows FILETIME (100ns intervals since 1601-01-01)
    uint32_t recordSize;
    uint32_t typeFlags;
    uint64_t timestampB;

    // Parsed from typeFlags
    int  recordType;   // 1=DATA(R/W), 2=CREATE, 3=IOCTL
    bool isCompletion; // false=REQUEST, true=COMPLETION

    // Parsed from payload (valid when recordType==1 and payload is large enough)
    uint32_t             ntstatus;
    uint8_t              infoByte;
    uint32_t             functionCode; // 3=IRP_MJ_READ, 4=IRP_MJ_WRITE (for type 1)
    std::vector<uint8_t> serialData;
};

// Metadata from the dmslog8 file header.
struct DmsLogHeader
{
    uint8_t     guid[16];
    uint64_t    sessionTimestamp; // Windows FILETIME
    uint64_t    dataOffset;
    std::string deviceName; // e.g. "PCI Express UART Port(COM9)"
    std::string portConfig; // e.g. "2,500,000, data bits: 8, stop bits: 1, parity: None"
};

class DmsLogReader
{
  public:
    // Open a dmslog8 file and parse header/metadata.
    bool open(const std::string &path);

    // Read all IRP records from the file.
    // Only returns Type 1 (serial data) records that carry actual serial bytes:
    //   - WRITE REQUESTs (TX: host -> device)
    //   - READ COMPLETIONs (RX: device -> host)
    bool readRecords(std::vector<IrpRecord> &records);

    const DmsLogHeader &header() const { return m_header; }
    const std::string  &filePath() const { return m_path; }

  private:
    bool readHeader();
    bool findFirstRecord(uint64_t &outOffset);
    bool readSessionMetadata();
    bool scanForPortConfig();

    std::string   m_path;
    std::ifstream m_file;
    DmsLogHeader  m_header;
    uint64_t      m_fileSize = 0;
    uint32_t      m_tsHigh   = 0; // high 32 bits of the session FILETIME for validation
};
