#include "bootloaders/7bl.hpp"
#include "Utils.hpp"
#include <cstring>
#include <stdexcept>

BootloaderCg BootloaderCg::parse(const std::vector<uint8_t>& bytes) {
    BootloaderCg cg;
    if (bytes.size() < sizeof(bl7_header))
        throw std::runtime_error("CG/7BL data too short");
    
    std::memcpy(&cg.header, bytes.data(), sizeof(bl7_header));
    
    cg.header.header.magic = bswap16(cg.header.header.magic);
    cg.header.header.version = bswap16(cg.header.header.version);
    cg.header.header.flags = bswap16(cg.header.header.flags);
    cg.header.header.size = bswap32(cg.header.header.size);
    cg.header.header.entrypoint = bswap32(cg.header.header.entrypoint);
    
    cg.data = std::vector<uint8_t>(bytes.begin() + sizeof(bl7_header), bytes.end());
    cg.decrypted = cg.is_decrypted();
    return cg;
}

void BootloaderCg::decrypt(const uint8_t cg_hmac[16]) {
    uint32_t size_aligned = (header.header.size + 0xF) & ~0xF;
    size_t payload_len = size_aligned - sizeof(bl_header);
    
    if (data.size() + sizeof(bl7_header) - sizeof(bl_header) < payload_len)
        throw std::runtime_error("CG/7BL payload too short");
    
    uint8_t derived_key[16];
    ExCryptHmacSha(cg_hmac, 16, header.key, 16, nullptr, 0, nullptr, 0, derived_key, 16);
    
    // Decrypt everything after the first 0x20 bytes (bl_header + key)
    std::vector<uint8_t> temp;
    temp.insert(temp.end(), reinterpret_cast<uint8_t*>(&header) + 0x20, reinterpret_cast<uint8_t*>(&header) + sizeof(bl7_header));
    temp.insert(temp.end(), data.begin(), data.end());
    
    ExCryptRc4(derived_key, 16, temp.data(), temp.size());
    
    std::memcpy(reinterpret_cast<uint8_t*>(&header) + 0x20, temp.data(), sizeof(bl7_header) - 0x20);
    std::copy(temp.begin() + (sizeof(bl7_header) - 0x20), temp.begin() + (sizeof(bl7_header) - 0x20) + data.size(), data.begin());
    
    decrypted = true;
}

bool BootloaderCg::is_decrypted() const {
    if (decrypted) return true;
    return (header.header.magic == 0x5347 || header.header.magic == 0x4347);
}

std::vector<uint8_t> BootloaderCg::serialize() const {
    std::vector<uint8_t> out(sizeof(bl7_header));
    bl7_header temp_hdr = header;
    temp_hdr.header.magic = bswap16(temp_hdr.header.magic);
    temp_hdr.header.version = bswap16(temp_hdr.header.version);
    temp_hdr.header.flags = bswap16(temp_hdr.header.flags);
    temp_hdr.header.size = bswap32(temp_hdr.header.size);
    temp_hdr.header.entrypoint = bswap32(temp_hdr.header.entrypoint);
    
    std::memcpy(out.data(), &temp_hdr, sizeof(bl7_header));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
