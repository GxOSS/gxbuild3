#pragma once

#include <vector>
#include <cstdint>

bool cpukey_valid(const std::vector<uint8_t>& cpu_key);
std::vector<uint8_t> keyvault_decrypt(const std::vector<uint8_t>& cpu_key, const std::vector<uint8_t>& data);
std::vector<uint8_t> keyvault_encrypt(const std::vector<uint8_t>& cpu_key, const std::vector<uint8_t>& data);
bool keyvault_verify(const std::vector<uint8_t>& cpu_key, const std::vector<uint8_t>& data, const std::vector<uint8_t>& pub_key);
