#include "bootloaders/2bl.hpp"
#include "excrypt.h"
#include <cstring>
#include <stdexcept>

BootloaderCb BootloaderCb::parse(const std::vector<uint8_t>& bytes) {
    BootloaderCb cb;

    if (bytes.size() < sizeof(BootloaderHeader))
        throw std::runtime_error("CB data too short");

    std::memcpy(&cb.header, bytes.data(), sizeof(BootloaderHeader));

    cb.header.magic = __builtin_bswap16(cb.header.magic);
    cb.header.version = __builtin_bswap16(cb.header.version);
    cb.header.flags = __builtin_bswap16(cb.header.flags);
    cb.header.size = __builtin_bswap32(cb.header.size);
    cb.header.entrypoint = __builtin_bswap32(cb.header.entrypoint);

    cb.data = std::vector<uint8_t>(bytes.begin() + sizeof(BootloaderHeader), bytes.end());
    cb.decrypted = cb.verify_decrypted();
    if (cb.decrypted) cb.populate_metadata();
    return cb;
}

bool BootloaderCb::verify_decrypted() const {
    if (data.size() < 0x380) return false;

    for (size_t i = 0x260; i < 0x380; i++)
        if (data[i] != 0) return false;

    return true;
}

bool BootloaderCb::is_decrypted() const {
    return decrypted || verify_decrypted();
}

void BootloaderCb::do_rc4_decrypt(const uint8_t key[16], size_t payload_len) {
    ExCryptRc4(key, 16, data.data() + 0x10, payload_len - 0x10);
}

void BootloaderCb::decrypt(const uint8_t onebl_key[16]) {
    uint32_t size_aligned = (header.size + 0xF) & ~0xF;
    size_t payload_len = size_aligned - sizeof(BootloaderHeader);
    uint8_t digest[20];
    std::array<uint8_t, 16> key;

    if (data.size() < payload_len)
        throw std::runtime_error("CB data too short");

    ExCryptHmacSha(onebl_key, 16, data.data(), 0x10, nullptr, 0, nullptr, 0, digest, 20);

    std::memcpy(key.data(), digest, 16);
    derived_key = key;

    do_rc4_decrypt(key.data(), payload_len);
    decrypted = !decrypted;

    if (decrypted)
        populate_metadata();
}

void BootloaderCb::decrypt_v1(const uint8_t cb_a_key[16], const uint8_t cpu_key[16]) {
    uint32_t size_aligned = (header.size + 0xF) & ~0xF;
    size_t payload_len = size_aligned - sizeof(BootloaderHeader);
    uint8_t digest[20];
    std::array<uint8_t, 16> key;

    if (data.size() < payload_len)
        throw std::runtime_error("CB data too short");

    ExCryptHmacSha(cb_a_key, 16, data.data(), 0x10, cpu_key, 16, nullptr, 0, digest, 20);

    std::memcpy(key.data(), digest, 16);
    derived_key = key;

    do_rc4_decrypt(key.data(), payload_len);
    decrypted = !decrypted;

    if (decrypted)
        populate_metadata();
}

void BootloaderCb::decrypt_v2(const BootloaderHeader& cb_a_hdr, const uint8_t cb_a_key[16], const uint8_t cpu_key[16]) {
    uint8_t digest[20];
    uint8_t cb_a_hdr_copy[16];
    std::array<uint8_t, 16> key;

    uint32_t size_aligned = (header.size + 0xF) & ~0xF;
    size_t payload_len = size_aligned - sizeof(BootloaderHeader);

    if (data.size() < payload_len)
        throw std::runtime_error("CB data too short");

    std::memcpy(cb_a_hdr_copy, &cb_a_hdr, 16);
    cb_a_hdr_copy[6] = 0;
    cb_a_hdr_copy[7] = 0;

    EXCRYPT_HMACSHA_STATE state;
    ExCryptHmacShaInit(&state, cb_a_key, 16);
    ExCryptHmacShaUpdate(&state, data.data(), 0x10);
    ExCryptHmacShaUpdate(&state, cpu_key, 16);
    ExCryptHmacShaUpdate(&state, cb_a_hdr_copy, 16);
    ExCryptHmacShaFinal(&state, digest, 20);

    std::memcpy(key.data(), digest, 16);
    derived_key = key;

    do_rc4_decrypt(key.data(), payload_len);
    decrypted = !decrypted;

    if (decrypted)
        populate_metadata();
}
void BootloaderCb::decrypt_mfg(const uint8_t cb_a_key[16]) {
    uint32_t size_aligned = (header.size + 0xF) & ~0xF;
    size_t payload_len = size_aligned - sizeof(BootloaderHeader);
    uint8_t hmac_input[0x20];
    uint8_t zero_key[16] = {};
    uint8_t digest[20];

    if (data.size() < payload_len)
        throw std::runtime_error("CB data too short");

    std::memcpy(hmac_input, data.data(), 0x10);
    std::memcpy(hmac_input + 0x10, cb_a_key, 0x10);


    ExCryptHmacSha(zero_key, 16, hmac_input, 0x20, nullptr, 0, nullptr, 0, digest, 20);

    std::array<uint8_t, 16> key;
    std::memcpy(key.data(), digest, 16);
    derived_key = key;

    do_rc4_decrypt(key.data(), payload_len);
    decrypted = !decrypted;
    if (decrypted)
        populate_metadata();
}

void BootloaderCb::populate_metadata() {
    CbMetadata meta = {};
    uint8_t ldv = data[0x13];
    uint64_t tmp = 0;

    if (!is_decrypted() || data.size() < 0x3B0) return;

    meta.b_flags = header.flags;

    meta.pairing_data[0] = data[0x12];
    meta.pairing_data[1] = data[0x11];
    meta.pairing_data[2] = data[0x10];

    meta.lockdown_value = (ldv <= 16) ? ldv : 0;

    std::memcpy(meta.reserved_per_box, &data[0x14], 0xC);
    std::memcpy(meta.per_box_digest, &data[0x20], 0x10);
    std::memcpy(meta.signature, &data[0x30], 0x100);

    std::memcpy(&tmp, &data[0x240], 8);
    meta.post_output_addr = __builtin_bswap64(tmp);

    std::memcpy(&tmp, &data[0x248], 8);
    meta.sb_flash_addr = __builtin_bswap64(tmp);

    std::memcpy(&tmp, &data[0x250], 8);
    meta.soc_mmio_addr = __builtin_bswap64(tmp);

    std::memcpy(meta.rsa_pub_key, &data[0x258], 0x110);
    std::memcpy(meta.nonce_3bl, &data[0x368], 0x10);
    std::memcpy(meta.salt_3bl, &data[0x378], 0xA);
    std::memcpy(meta.salt_4bl, &data[0x382], 0xA);
    std::memcpy(meta.digest_4bl, &data[0x38C], 0x14);
    std::memcpy(meta.console_allow, &data[0x3A0], 4);

    metadata = meta;
}

std::vector<uint8_t> BootloaderCb::serialize() const {
    std::vector<uint8_t> out(sizeof(BootloaderHeader));
    std::memcpy(out.data(), &header, sizeof(BootloaderHeader));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
