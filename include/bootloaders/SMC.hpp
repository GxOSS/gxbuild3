#pragma once

#include <cstdint>
#include <vector>

std::vector<uint8_t> smc_decrypt(const std::vector<uint8_t>& data);
std::vector<uint8_t> smc_encrypt(const std::vector<uint8_t>& data);
