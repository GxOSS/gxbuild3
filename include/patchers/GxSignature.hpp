#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Signature Patcher
// Match signature / wildcards and replace bytes in data
// Inspired by [c0z]'s SMC autopatcher

struct GxSigByte {
    bool isWildcard;
    uint8_t value;
};

class GxSignature {
  public:
    // Parses a space-delimited pattern string (e.g. "05 ?? E5") into a vector
    // of GxSigByte.
    static std::vector<GxSigByte> ParsePattern(const std::string& patternStr);

    // Matches the search pattern in 'data' and replaces matches with the
    // replacement pattern. 'searchPatternStr' is the pattern to search for.
    // 'replacePatternStr' is the pattern to overwrite with.
    // Returns the number of matches found and patched.
    static uint32_t ApplyPatch(uint8_t* data, uint32_t dataSize, const std::string& searchPatternStr,
                            const std::string& replacePatternStr);
};