#pragma once

#include "GxBinaryParser.hpp"

#include <cstdint>

class GxSource {
  public:
    static bool ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t offset,
                           const uint8_t* payload, uint32_t payloadSize);
};

class GxXePatch {
  public:
    static bool ApplyPatch(uint8_t* data, uint32_t dataSize, uint32_t address, uint32_t length,
                           const uint32_t* patchWords);

    static bool ApplyPatchEntry(uint8_t* data, uint32_t dataSize, const GxXePatchEntry& entry);

    static bool ApplyPatchSection(uint8_t* data, uint32_t dataSize, const GXePatchSection& section);
};