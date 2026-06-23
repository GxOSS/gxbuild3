#include "bootloaders/SMC.hpp"

const uint8_t XECRYPT_SMC_KEY[4] = {0x42, 0x75, 0x4E, 0x79};

std::vector<uint8_t> smc_decrypt(const std::vector<uint8_t>& data) {
    uint32_t key[4] = {XECRYPT_SMC_KEY[0], XECRYPT_SMC_KEY[1], XECRYPT_SMC_KEY[2],
                       XECRYPT_SMC_KEY[3]};
    std::vector<uint8_t> decrypted;
    decrypted.reserve(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t j = data[i];
        uint32_t mod = j * 0xFB;
        decrypted.push_back(j ^ (key[i & 3] & 0xFF));
        key[(i + 1) & 3] += mod;
        key[(i + 2) & 3] += mod >> 8;
    }

    return decrypted;
}

std::vector<uint8_t> smc_encrypt(const std::vector<uint8_t>& data) {
    uint32_t key[4] = {XECRYPT_SMC_KEY[0], XECRYPT_SMC_KEY[1], XECRYPT_SMC_KEY[2],
                       XECRYPT_SMC_KEY[3]};
    std::vector<uint8_t> encrypted;
    encrypted.reserve(data.size());

    for (size_t i = 0; i < data.size(); ++i) {
        uint8_t j = data[i] ^ (key[i & 3] & 0xFF);
        uint32_t mod = j * 0xFB;
        encrypted.push_back(j);
        key[(i + 1) & 3] += mod;
        key[(i + 2) & 3] += mod >> 8;
    }

    return encrypted;
}