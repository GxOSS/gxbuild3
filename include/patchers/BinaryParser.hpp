#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GxXePatchEntry {
    uint32_t address;
    uint32_t length;
    std::vector<uint32_t> words; // host-endian patch words
};

struct GXePatchSection {
    std::string identifier;
    std::vector<GxXePatchEntry> entries;
};

class GxBinaryParser {
  public:
    static bool ParsePatchFile(const std::string& filePath,
                               std::vector<std::vector<GxXePatchEntry>>& outSections);
};