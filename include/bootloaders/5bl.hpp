#pragma once
#include "Common.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class BootloaderCe {
  public:
    bl5_header header;
    std::vector<uint8_t> data;
    bool decrypted = false;

    static BootloaderCe parse(const std::vector<uint8_t>& bytes);

    void decrypt(const uint8_t cd_key[16]);

    bool is_decrypted() const;
    std::vector<uint8_t> decompress(std::vector<uint8_t>& out) const;
    std::vector<uint8_t> serialize() const;
};
