#pragma once

#include <vector>
#include <cstdint>

std::vector<uint8_t> smc_decrypt(const std::vector<uint8_t>& data);
std::vector<uint8_t> smc_encrypt(const std::vector<uint8_t>& data);
