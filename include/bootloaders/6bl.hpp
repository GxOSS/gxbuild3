#pragma once
#include "Common.hpp"

#include <cstdint>
#include <optional>
#include <vector>

struct CfMetadata {
    // Plain - Offset 0x0 in payload / 0x10 Absolute
    uint16_t source_version;
    uint16_t target_version;
    uint32_t reserved_prefix;
    uint32_t cg_size;
    uint8_t hmac_salt[16];
};

struct CfMetadataDecrypted {
    // Decrypted - 7BL Bridge - Offset 0x20 in payload / 0x30 Absolute
    uint16_t cg_blocks_used;
    std::vector<uint16_t> cg_block_numbers; // 223 entries

    // Decrypted - PerBoxData
    uint8_t reserved_per_box[0x2B];
    uint8_t update_slot;
    uint8_t pairing_data[3];
    uint8_t lockdown_value;
    uint8_t per_box_digest[0x10];

    // Decrypted - Chain Bridge
    uint8_t signature[0x100];
    uint8_t cg_nonce[0x10];
    uint8_t cg_digest[0x14];
};

class BootloaderCf {
  public:
    bl6_header header;
    std::vector<uint8_t> data;
    bool decrypted = false;

    static BootloaderCf parse(const std::vector<uint8_t>& bytes);

    void decrypt(const uint8_t onebl_key[16]);

    bool is_decrypted() const;
    bool verify_signature() const;
    std::vector<uint8_t> serialize() const;
};
