#include "bootloaders/3bl.hpp"
#include "Utils.hpp"
#include <cstring>
#include <stdexcept>

BootloaderSc BootloaderSc::parse(const std::vector<uint8_t>& bytes) {
    BootloaderSc sc;
    if (bytes.size() < sizeof(bl3_header))
        throw std::runtime_error("SC/3BL data too short");
    
    std::memcpy(&sc.header, bytes.data(), sizeof(bl3_header));
    
    sc.header.header.magic = bswap16(sc.header.header.magic);
    sc.header.header.version = bswap16(sc.header.header.version);
    sc.header.header.flags = bswap16(sc.header.header.flags);
    sc.header.header.size = bswap32(sc.header.header.size);
    sc.header.header.entrypoint = bswap32(sc.header.header.entrypoint);
    
    sc.data = std::vector<uint8_t>(bytes.begin() + sizeof(bl3_header), bytes.end());
    sc.decrypted = sc.is_decrypted();
    return sc;
}

void BootloaderSc::decrypt(const uint8_t cb_key[16]) {
    uint32_t size_aligned = (header.header.size + 0xF) & ~0xF;
    size_t encrypted_len = size_aligned - sizeof(bl3_header);
    
    if (data.size() < encrypted_len)
        throw std::runtime_error("SC/3BL payload too short");
    
    uint8_t digest[20];
    ExCryptHmacSha(cb_key, 16, header.key, 16, nullptr, 0, nullptr, 0, digest, 20);
    
    uint8_t rc4_key[16];
    std::memcpy(rc4_key, digest, 16);
    
    ExCryptRc4(rc4_key, 16, data.data(), static_cast<uint32_t>(encrypted_len));
    
    decrypted = true;
}

bool BootloaderSc::is_decrypted() const {
    if (decrypted) return true;
    // Check if signature contains zeroes (usually decrypted indicators have clean padding)
    // Or we can check if the header magic is valid SC/CC
    return (header.header.magic == 0x5343 || header.header.magic == 0x4343);
}

std::vector<uint8_t> BootloaderSc::serialize() const {
    std::vector<uint8_t> out(sizeof(bl3_header));
    bl3_header temp_hdr = header;
    temp_hdr.header.magic = bswap16(temp_hdr.header.magic);
    temp_hdr.header.version = bswap16(temp_hdr.header.version);
    temp_hdr.header.flags = bswap16(temp_hdr.header.flags);
    temp_hdr.header.size = bswap32(temp_hdr.header.size);
    temp_hdr.header.entrypoint = bswap32(temp_hdr.header.entrypoint);
    
    std::memcpy(out.data(), &temp_hdr, sizeof(bl3_header));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
