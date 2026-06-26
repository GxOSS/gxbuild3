#include "patchers/BinaryParser.hpp"

#include "Utils.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>

namespace BinaryParser {

    bool ParsePatchFile(const std::string& filePath, std::vector<XePatchSection>& outSections) {
        outSections.clear();

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

        XePatchSection currentSection;

        while (true) {
            uint32_t address = 0;
            if (!file.read(reinterpret_cast<char*>(&address), sizeof(uint32_t))) {
                break;
            }
            address = swap32(address);

            if (address == 0xFFFFFFFFU) {
                if (!currentSection.entries.empty()) {
                    outSections.push_back(currentSection);
                    currentSection.entries.clear();
                }
                continue;
            }

            uint32_t length = 0;
            if (!file.read(reinterpret_cast<char*>(&length), sizeof(uint32_t))) {
                return false;
            }
            length = swap32(length);

            if (length > 0) {
                std::streampos currentPos = file.tellg();
                file.seekg(0, std::ios::end);
                std::streampos endPos = file.tellg();
                file.seekg(currentPos);

                if (currentPos == std::streampos(-1) || endPos == std::streampos(-1)) {
                    return false;
                }

                std::streamsize remaining = endPos - currentPos;

                if (remaining < 0 || (static_cast<uint64_t>(length) * sizeof(uint32_t) >
                                      static_cast<uint64_t>(remaining))) {
                    return false;
                }
            }

            XePatchEntry entry;
            entry.address = address;
            entry.length = length;
            entry.words.resize(length);

            if (length > 0) {
                if (!file.read(reinterpret_cast<char*>(entry.words.data()),
                               length * sizeof(uint32_t))) {
                    return false;
                }
                for (uint32_t i = 0; i < length; i++) {
                    entry.words[i] = swap32(entry.words[i]);
                }
            }

            currentSection.entries.push_back(entry);
        }

        if (!currentSection.entries.empty()) {
            outSections.push_back(currentSection);
        }

        return true;
    }

} // namespace BinaryParser