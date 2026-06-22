#include "patchers/GxBinaryParser.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>

static inline uint32_t swap32(uint32_t x) {
    return (x & 0xFF000000U) >> 24 | (x & 0x00FF0000U) >> 8 | (x & 0x0000FF00U) << 8 |
           (x & 0x000000FFU) << 24;
}

bool GxBinaryParser::ParsePatchFile(const std::string& filePath,
                                    std::vector<std::vector<GxXePatchEntry>>& outSections) {
    outSections.clear();

    // Only accept .bin files (case-insensitive check)
    if (filePath.length() < 4) {
        return false;
    }
    std::string ext = filePath.substr(filePath.length() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".bin") {
        return false;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::vector<GxXePatchEntry> currentSection;

    while (true) {
        uint32_t address = 0;
        if (!file.read(reinterpret_cast<char*>(&address), sizeof(uint32_t))) {
            break; // EOF
        }
        address = swap32(address);

        if (address == 0xFFFFFFFFU) {
            outSections.push_back(currentSection);
            currentSection.clear();
            continue;
        }

        uint32_t length = 0;
        if (!file.read(reinterpret_cast<char*>(&length), sizeof(uint32_t))) {
            // Unexpected EOF
            return false;
        }
        length = swap32(length);

        GxXePatchEntry entry;
        entry.address = address;
        entry.length = length;
        entry.words.resize(length);

        if (length > 0) {
            if (!file.read(reinterpret_cast<char*>(entry.words.data()), length * sizeof(uint32_t))) {
                // Unexpected EOF
                return false;
            }
            for (uint32_t i = 0; i < length; i++) {
                entry.words[i] = swap32(entry.words[i]);
            }
        }

        currentSection.push_back(entry);
    }

    // If the file did not end with a final 0xFFFFFFFF, push the last section if
    // we have parsed any entries.
    if (!currentSection.empty()) {
        outSections.push_back(currentSection);
    }

    return true;
}
