#pragma once
#include "Common.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class BootloaderCg {
  public:
    bl7_header header;
    std::vector<uint8_t> data;
    bool decrypted = false;

    static BootloaderCg parse(const std::vector<uint8_t>& bytes);

    void decrypt(const uint8_t cpu_or_onebl_key[16]);

    bool is_decrypted() const;
    bool apply_patch(const std::vector<uint8_t>& base_data, std::vector<uint8_t>& out) const;
    std::vector<uint8_t> serialize() const;
};
