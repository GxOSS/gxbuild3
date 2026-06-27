#pragma once
#include "bootloaders/2bl.hpp"
#include "bootloaders/3bl.hpp"
#include "bootloaders/4bl.hpp"
//#include "bootloaders/5bl.hpp"
#include "bootloaders/6bl.hpp"
#include "bootloaders/7bl.hpp"
#include "bootloaders/Common.hpp"
#include "bootloaders/FlashFileSystem.hpp"

// NAND Header structure based on NANDFS.md
// Note: This appears to start with a bl_header (magic, version, pairing, flags, entrypoint, size)
// followed by NAND-specific fields
typedef struct _raw_nand_header {
    uint16_t magic;             // 0x00
    uint16_t version;           // 0x02
    uint16_t pairing;           // 0x04
    uint16_t flags;             // 0x06
    uint32_t entrypoint;         // 0x08
    uint32_t size;              // 0x0C
    uint8_t reserved_0x10[0x50 - 0x10]; // 0x10-0x4F (40 bytes)
    uint32_t payload_indicator;  // 0x50
    uint8_t reserved_0x54[0x60 - 0x54]; // 0x54-0x5F (12 bytes)
    uint32_t kv_size;            // 0x60
    uint32_t cf1_offset;         // 0x64
    uint16_t patch_slots;        // 0x68
    uint16_t kv_version;         // 0x6A
    uint32_t kv_offset;          // 0x6C
    uint32_t fs_offset;          // 0x70 (FlashFS offset)
    uint32_t smc_config_offset;  // 0x74
    uint32_t smc_size;           // 0x78
    uint32_t smc_offset;         // 0x7C
} raw_nand_header_t;

typedef struct BlResults {
    uint32_t offset;
    uint8_t version;
    uint32_t size;
} bl_results_t;

typedef struct NandResults {
    bool valid;
    bl_results_t cb_or_a;
    std::optional<bl_results_t> cbx;
    std::optional<bl_results_t> cbb;
    bl_results_t cd;
    std::optional<bl_results_t> ce;
    // patchslot 0
    std::optional<bl_results_t> cf0;
    std::optional<bl_results_t> cg0;
    std::optional<bl_results_t> cf1;
    std::optional<bl_results_t> cg1;
    uint32_t kv_offset;
    uint32_t kv_size;
    uint32_t kv_version;
    uint32_t smc_size;
    uint32_t smc_offset;
    uint32_t smc_config_offset;
    uint32_t fs_offset;
    uint32_t payload_indicator;
    uint16_t patch_slots;
} nand_results_t;

typedef struct _patchslot_t {
    std::optional<std::vector<uint8_t>> cf;
    std::optional<std::vector<uint8_t>> cg;
} patchslot_t;

typedef struct _xellblock_t {
    uint8_t xell_main;
    uint8_t xell_backup;
} xellblock_t;

typedef struct _payloads_t {
    std::optional<std::vector<uint8_t>> addon_patches;
    std::optional<uint8_t> jtag_rebooter;
    std::optional<uint8_t> jtag_payload;
    std::optional<uint8_t> vfuses;
} payloads_t;

struct FlashImage {
    raw_nand_header_t header;
    std::optional<std::vector<uint8_t>> cb_or_A;
    std::optional<std::vector<uint8_t>> cb_X;
    std::optional<std::vector<uint8_t>> cb_B;
    std::optional<std::vector<uint8_t>> sc;
    std::optional<std::vector<uint8_t>> cd;
    std::optional<std::vector<uint8_t>> ce;
    std::optional<patchslot_t> patchslot_0;
    std::optional<patchslot_t> patchslot_1;
    std::optional<std::vector<uint8_t>> smc;
    std::optional<std::vector<uint8_t>> xconfig;
    std::optional<std::vector<uint8_t>> keyvault;

    std::optional<xellblock_t> xellblock;
    std::optional<payloads_t> payloads;
    
    std::optional<gxbuild3::bootloaders::FlashFileSystem> filesystem;
    std::optional<std::vector<gxbuild3::bootloaders::FlashMobileData>> mobile_data;
    std::optional<gxbuild3::bootloaders::XeCoronaFsData> corona_fs_data;

    static flash_image_t parse(const std::vector<uint8_t>& data);
};

typedef FlashImage flash_image_t;

typedef struct FlashImageMetadata {
    std::optional<std::vector<uint8_t>> cpu_key;
    std::optional<uint8_t> cbldv;
    std::optional<uint8_t> cfldv;
    std::optional<std::vector<uint8_t>> pairingdata;
} flash_image_metadata_t;

nand_results_t read(const std::vector<uint8_t>& data);

flash_image_t parse(const std::vector<uint8_t>& data);

// bool decrypt_all(const flash_image_t& flash, const std::vector<uint8_t>& cpu_key);

// bool encrypt_all(const flash_image_t& flash, const std::vector<uint8_t>& cpu_key);

// bool extract_all(const flash_image_t& flash, std::string output_path);

bool build(const flash_image_t& flash, std::string output_path);