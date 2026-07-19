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

// credit to c0z
bool smc_is_encrypted(const std::vector<uint8_t>& data) {
    if (data.size() <= 0x100) {
        return false;  // too small to be a valid SMC
    }
    const auto decrypted = smc_decrypt(data);
    const uint8_t type_nibble = (decrypted[0x100] >> 4) & 0xF;
    return type_nibble >= 1 && type_nibble <= 7;
}

std::string_view smc_type_name(SmcType type) {
    switch (type) {
        case SmcType::Retail:       return "Retail";
        case SmcType::Glitch:       return "Glitch";
        case SmcType::Jtag:         return "JTAG";
        case SmcType::Cygnos:       return "Cygnos";
        case SmcType::RJtag:        return "R-JTAG";
        case SmcType::RJtagCygnos:  return "R-JTAG+Cygnos";
        default:                    return "Unknown";
    }
}

SmcType smc_get_type(const std::vector<uint8_t>& data) {
    if (data.size() < 6) {
        return SmcType::Unknown;
    }

    auto ret = SmcType::Unknown;
    bool glitch_patched = false;
    bool retail = false;

    const size_t scan_end = data.size() - 6;
    for (size_t i = 0; i < scan_end; ++i) {
        switch (data[i]) {
            case 0x05:
                // Retail: 05 ?? E5 ?? B4 05
                if (data[i+2] == 0xE5 && data[i+4] == 0xB4 && data[i+5] == 0x05) {
                    retail = true;
                    glitch_patched = false;  // not properly glitch-patched
                }
                break;
            case 0x00:
                // Glitch: 00 00 E5 ?? B4 05  (retail bytes zeroed out)
                if (data[i+1] == 0x00 && data[i+2] == 0xE5 && data[i+4] == 0xB4 && data[i+5] == 0x05) {
                    glitch_patched = true;
                }
                break;
            case 0x78:
                // Cygnos: 78 BA B6
                if (data[i+1] == 0xBA && data[i+2] == 0xB6) {
                    ret = SmcType::Cygnos;
                }
                break;
            case 0xD0:
                // JTAG: D0 00 00 1B
                if (data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x1B) {
                    ret = SmcType::Jtag;
                }
                break;
            default:
                break;
        }
    }

    if (glitch_patched && !retail) {
        switch (ret) {
            case SmcType::Jtag:   return SmcType::RJtag;
            case SmcType::Cygnos: return SmcType::RJtagCygnos;
            default:              return SmcType::Glitch;
        }
    }

    return (ret == SmcType::Unknown && retail) ? SmcType::Retail : ret;
}