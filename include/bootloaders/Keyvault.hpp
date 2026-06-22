#pragma once

#include <vector>
#include <cstdint>

bool XeCryptCpuKeyValid(const std::vector<uint8_t>& cpu_key);
std::vector<uint8_t> XeCryptKeyVaultDecrypt(const std::vector<uint8_t>& cpu_key, const std::vector<uint8_t>& data);
std::vector<uint8_t> XeCryptKeyVaultEncrypt(const std::vector<uint8_t>& cpu_key, const std::vector<uint8_t>& data);
bool XeCryptKeyVaultVerify(const std::vector<uint8_t>& cpu_key, const std::vector<uint8_t>& data, const std::vector<uint8_t>& pub_key);
