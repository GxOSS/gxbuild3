#pragma once
#include <cstdint>

inline uint32_t swap32(uint32_t x) {
    return (x & 0xFF000000U) >> 24 | (x & 0x00FF0000U) >> 8 | (x & 0x0000FF00U) << 8 |
           (x & 0x000000FFU) << 24;
}

#if defined(_MSC_VER)
#include <cstdlib>
inline uint16_t bswap16(uint16_t x) {
    return _byteswap_ushort(x);
}
inline uint32_t bswap32(uint32_t x) {
    return _byteswap_ulong(x);
}
inline uint64_t bswap64(uint64_t x) {
    return _byteswap_uint64(x);
}
#else
inline uint16_t bswap16(uint16_t x) {
    return __builtin_bswap16(x);
}
inline uint32_t bswap32(uint32_t x) {
    return __builtin_bswap32(x);
}
inline uint64_t bswap64(uint64_t x) {
    return __builtin_bswap64(x);
}
#endif