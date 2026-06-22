#pragma once
#include "Common.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

struct CbMetadata {
    uint16_t b_flags;
    uint8_t pairing_data[3];
    uint8_t lockdown_value;
    uint8_t reserved_per_box[0xC];
    uint8_t per_box_digest[0x10];
    uint8_t signature[0x100];
    uint8_t rsa_pub_key[0x110];
    uint8_t nonce_3bl[0x10];
    uint8_t salt_3bl[0xA];
    uint8_t salt_4bl[0xA];
    uint8_t digest_4bl[0x14];
    uint64_t post_output_addr;
    uint64_t sb_flash_addr;
    uint64_t soc_mmio_addr;
    uint8_t console_allow[4];
};

class BootloaderCb {
  public:
    bl2_header header;
    std::vector<uint8_t> data;
    std::optional<CbMetadata> metadata;
    std::optional<std::array<uint8_t, 16>> derived_key;
    bool decrypted = false;

    static BootloaderCb parse(const std::vector<uint8_t>& bytes);

    void decrypt(const uint8_t onebl_key[16]);
    void decrypt_v1(const uint8_t cb_a_key[16], const uint8_t cpu_key[16]);
    void decrypt_v2(const bl2_header& cb_a_hdr, const uint8_t cb_a_key[16],
                    const uint8_t cpu_key[16]);
    void decrypt_mfg(const uint8_t cb_a_key[16]);

    bool is_decrypted() const;
    bool verify_decrypted() const;
    void populate_metadata();
    std::vector<uint8_t> serialize() const;

  private:
    void do_rc4_decrypt(const uint8_t key[16], size_t payload_len);
};