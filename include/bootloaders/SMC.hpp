#pragma once

#include <vector>
#include <cstdint>

std::vector<uint8_t> XeCryptSmcDecrypt(const std::vector<uint8_t>& data);
std::vector<uint8_t> XeCryptSmcEncrypt(const std::vector<uint8_t>& data);
