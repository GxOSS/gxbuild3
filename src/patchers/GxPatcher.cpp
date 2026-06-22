#include "patchers/GxPatcher.hpp"

#include <cstdint>
#include <cstring>

bool GxSource::ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t offset, const uint8_t* payload,
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

bool GxXePatch::ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t address, uint32_t length,
                           const uint32_t* patchWords) {
    if (!data || !patchWords) {
        return false;
    }

    // Check for bounds. Each word is 4 bytes.
    // Check if address + length * 4 overflows, or is beyond dataSize.
    uint64_t endOffset = static_cast<uint64_t>(address) + static_cast<uint64_t>(length) * 4;
    if (endOffset > dataSize) {
        return false;
    }

    for (uint32_t i = 0; i < length; i++) {
        uint32_t targetAddr = address + i * 4;
        *(reinterpret_cast<uint32_t*>(data + targetAddr)) = patchWords[i];
    }

    return true;
}

bool GxXePatch::ApplyPatchEntry(uint8_t* data, uint32_t dataSize, const GxXePatchEntry& entry) {
    return ApplyPatch(data, dataSize, entry.address, entry.length, entry.words.data());
}

bool GxXePatch::ApplyPatchSection(uint8_t* data, uint32_t dataSize,
                                  const GXePatchSection& section) {
    for (const auto& entry : section.entries) {
        if (!ApplyPatchEntry(data, dataSize, entry)) {
            return false;
        }
    }
    return true;
}
