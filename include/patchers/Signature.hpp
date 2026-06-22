#pragma once

#include <cstdint>
#include <string>
#include <vector>
// Inspired by [c0z]'s SMC autopatcher

struct GxSigByte {
    bool isWildcard;
    uint8_t value;
};

class GxSignature {
  public:
    static std::vector<GxSigByte> ParsePattern(const std::string& patternStr);

    static uint32_t ApplyPatch(uint8_t* data, uint32_t dataSize,
                               const std::string& searchPatternStr,
                               const std::string& replacePatternStr);
};