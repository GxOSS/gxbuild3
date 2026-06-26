#include "patchers/Patcher.hpp"

#include "Utils.hpp"

#include <cstdint>
#include <cstring>

namespace Source {

    bool ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t offset, const uint8_t* payload,
                    uint32_t payloadSize) {
        if (!data || !payload || payloadSize == 0) {
            return false;
        }

        uint64_t endOffset = static_cast<uint64_t>(offset) + static_cast<uint64_t>(payloadSize);
        if (endOffset > dataSize) {
            return false;
        }

        memcpy(data + offset, payload, payloadSize);
        return true;
    }

} // namespace Source

namespace XePatch {

    bool ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t address, uint32_t length,
                    const uint32_t* patchWords) {
        if (!data || !patchWords) {
            return false;
        }

        uint64_t endOffset = static_cast<uint64_t>(address) + static_cast<uint64_t>(length) * 4;
        if (endOffset > dataSize) {
            return false;
        }

        for (uint32_t i = 0; i < length; i++) {
            uint32_t targetAddr = address + i * 4;
            uint32_t beWord = swap32(patchWords[i]);

            memcpy(data + targetAddr, &beWord, sizeof(uint32_t));
        }

        return true;
    }

    bool ApplyPatchEntry(uint8_t* data, uint32_t dataSize, const XePatchEntry& entry) {
        return ApplyPatch(data, dataSize, entry.address, entry.length, entry.words.data());
    }

    bool ApplyPatchSection(uint8_t* data, uint32_t dataSize, const XePatchSection& section) {
        for (const auto& entry : section.entries) {
            if (!ApplyPatchEntry(data, dataSize, entry)) {
                return false;
            }
        }
        return true;
    }

} // namespace XePatch