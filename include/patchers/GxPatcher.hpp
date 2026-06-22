#pragma once

#include "GxBinaryParser.hpp"
#include <cstdint>

// GXS Parser and patcher
// Parse and apply compiled GXS to data
class GxSource {
  public:
    // Applies a payload to the data buffer at the specified offset.
    // Returns true if the patch was successfully applied, or false if it goes
    // out of bounds or input is invalid.
    static bool ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t offset, const uint8_t* payload,
                           uint32_t payloadSize);
};

// XePatch parser and patcher
// Parse and apply XeBuild / freeBOOT patches to data
class GxXePatch {
  public:
    // Applies a single patch to the provided data buffer.
    // 'address' is the offset into the data buffer.
    // 'length' is the number of 4-byte words to patch.
    // 'patchWords' is the array of 32-bit words to insert.
    // Returns true if the patch was successfully applied, or false if it goes
    // out of bounds or input is invalid.
    static bool ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t address, uint32_t length,
                           const uint32_t* patchWords);

    // Applies a GxXePatchEntry to the data buffer.
    // Returns true if the patch was successfully applied, or false on error.
    static bool ApplyPatchEntry(uint8_t* data, uint32_t dataSize, const GxXePatchEntry& entry);

    // Applies all entries in a GXePatchSection to the data buffer.
    // Returns true if all patches were successfully applied, or false on error.
    static bool ApplyPatchSection(uint8_t* data, uint32_t dataSize, const GXePatchSection& section);
};