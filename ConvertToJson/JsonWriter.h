#pragma once

#include "PhoenixDecoder.h"
#include "DmsLogReader.h"

#include <string>
#include <vector>

class JsonWriter
{
  public:
    // Write decoded frames to a JSON file.
    // The output includes:
    //   - File metadata (source file, device, port config, session timestamp)
    //   - Summary statistics
    //   - All decoded frames in chronological order
    bool write(const std::string &outputPath, const DmsLogHeader &header, const std::string &sourceFile, const std::vector<PhoenixFrame> &frames);

  private:
    // JSON serialization helpers (no external dependency)
    static std::string escapeJson(const std::string &s);
    static std::string hexString(const uint8_t *data, size_t len);
    static std::string filetimeToIso8601(uint64_t filetime);
};
