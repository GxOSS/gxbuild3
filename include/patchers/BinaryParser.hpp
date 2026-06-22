#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct XePatchEntry {
    uint32_t address;
    uint32_t length;
    std::vector<uint32_t> words;
};

struct XePatchSection {
    std::string identifier;
    std::vector<XePatchEntry> entries;
};

namespace BinaryParser {
    bool ParsePatchFile(const std::string& filePath, std::vector<XePatchSection>& outSections);
}