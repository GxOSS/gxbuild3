#pragma once
#include <cstdint>

inline uint32_t swap32(uint32_t x) {
    return (x & 0xFF000000U) >> 24 | (x & 0x00FF0000U) >> 8 | (x & 0x0000FF00U) << 8 |
           (x & 0x000000FFU) << 24;
}