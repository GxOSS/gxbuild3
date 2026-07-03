#include "bootloaders/3bl.hpp"
#include "Utils.hpp"
#include <cstring>
#include <stdexcept>

BootloaderSc BootloaderSc::parse(const std::vector<uint8_t>& bytes) {
    BootloaderSc sc;
    if (bytes.size() < sizeof(bl3_header))
        throw std::runtime_error("SC/3BL data too short");
    
    std::memcpy(&sc.header, bytes.data(), sizeof(bl3_header));
    
    sc.header.header.header.magic = bswap16(sc.header.header.header.magic);
    sc.header.header.header.version = bswap16(sc.header.header.header.version);
    sc.header.header.header.flags = bswap16(sc.header.header.header.flags);
    sc.header.header.header.size = bswap32(sc.header.header.header.size);
    sc.header.header.header.entrypoint = bswap32(sc.header.header.header.entrypoint);
    
    sc.data = std::vector<uint8_t>(bytes.begin() + sizeof(bl3_header), bytes.end());
    sc.decrypted = sc.is_decrypted();
    return sc;
}

void BootloaderSc::decrypt(const uint8_t cb_key[16]) {
    uint32_t size_aligned = (header.header.header.size + 0xF) & ~0xF;
    size_t payload_len = size_aligned - sizeof(bl_header);
    
    if (data.size() + sizeof(bl3_header) - sizeof(bl_header) < payload_len)
        throw std::runtime_error("SC/3BL payload too short");
    
    uint8_t digest[20];
    ExCryptHmacSha(cb_key, 16, header.header.key, 16, nullptr, 0, nullptr, 0, digest, 20);
    
    uint8_t rc4_key[16];
    std::memcpy(rc4_key, digest, 16);
    
    // Decrypt everything after the first 0x20 bytes (bl_header + key)
    std::vector<uint8_t> temp;
    temp.insert(temp.end(), reinterpret_cast<uint8_t*>(&header) + 0x20, reinterpret_cast<uint8_t*>(&header) + sizeof(bl3_header));
    temp.insert(temp.end(), data.begin(), data.end());
    
    ExCryptRc4(rc4_key, 16, temp.data(), temp.size());
    
    std::memcpy(reinterpret_cast<uint8_t*>(&header) + 0x20, temp.data(), sizeof(bl3_header) - 0x20);
    std::copy(temp.begin() + (sizeof(bl3_header) - 0x20), temp.begin() + (sizeof(bl3_header) - 0x20) + data.size(), data.begin());
    
    decrypted = true;
}

bool BootloaderSc::is_decrypted() const {
    if (decrypted) return true;
    // Check if signature contains zeroes (usually decrypted indicators have clean padding)
    // Or we can check if the header magic is valid SC/CC
    return (header.header.header.magic == 0x5343 || header.header.header.magic == 0x4343);
}

std::vector<uint8_t> BootloaderSc::serialize() const {
    std::vector<uint8_t> out(sizeof(bl3_header));
    bl3_header temp_hdr = header;
    temp_hdr.header.header.magic = bswap16(temp_hdr.header.header.magic);
    temp_hdr.header.header.version = bswap16(temp_hdr.header.header.version);
    temp_hdr.header.header.flags = bswap16(temp_hdr.header.header.flags);
    temp_hdr.header.header.size = bswap32(temp_hdr.header.header.size);
    temp_hdr.header.header.entrypoint = bswap32(temp_hdr.header.header.entrypoint);
    
    std::memcpy(out.data(), &temp_hdr, sizeof(bl3_header));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}