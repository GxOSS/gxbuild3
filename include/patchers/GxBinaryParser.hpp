#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Patch Binary Parser
// Parses BIN, GXP, and RGLP into patchs sections

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
    // Parses a XePatch .bin file.
    // 'filePath' must end with .bin (case-insensitive).
    // Splits the file into sections separated by 0xFFFFFFFF.
    // Returns true if the file was successfully parsed, or false on error.
    static bool ParsePatchFile(const std::string& filePath,
                               std::vector<std::vector<GxXePatchEntry>>& outSections);
};