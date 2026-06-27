#pragma once
#include "Common.hpp"

#include <cstdint>
#include <optional>
#include <vector>

struct CdMetadata {
    uint8_t signature[0x100];
    uint8_t rsa_pub_key[0x10];
    uint8_t nonce_6bl[0x10];
    uint8_t salt_6bl[10];
    uint8_t digest_5bl[0x14];
};

class BootloaderCd {
  public:
    bl4_header header;
    std::vector<uint8_t> data;
    bool decrypted = false;

    static BootloaderCd parse(const std::vector<uint8_t>& bytes);

    void decrypt(const uint8_t cb_b_key[16], const uint8_t cpu_key[16]);

    bool is_decrypted() const;
    std::vector<uint8_t> serialize() const;
};
