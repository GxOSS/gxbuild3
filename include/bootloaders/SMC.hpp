#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

enum class SmcType {
    Unknown    = -1,
    Retail     = 0,
    Glitch     = 1,
    Jtag       = 2,
    Cygnos     = 3,
    RJtag      = 4,   // Glitch + JTAG
    RJtagCygnos = 5,  // Glitch + JTAG + Cygnos
};

std::string_view smc_type_name(SmcType type);

std::vector<uint8_t> smc_decrypt(const std::vector<uint8_t>& data);
std::vector<uint8_t> smc_encrypt(const std::vector<uint8_t>& data);

bool smc_is_encrypted(const std::vector<uint8_t>& data);

SmcType smc_get_type(const std::vector<uint8_t>& data);
