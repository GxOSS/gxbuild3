#pragma once
#include "bootloaders/2bl.hpp"
#include "bootloaders/3bl.hpp"
#include "bootloaders/4bl.hpp"
#include "bootloaders/5bl.hpp"
#include "bootloaders/6bl.hpp"
#include "bootloaders/7bl.hpp"
#include "bootloaders/Common.hpp"
#include "bootloaders/FlashFileSystem.hpp"

typedef struct NandHeader {
    uint8_t magic;
    uint8_t version;
    uint8_t cb_offset;
    uint8_t cf_offset;
    uint8_t copyright;
    uint8_t kv_offset;
    uint8_t kv_size;
    uint8_t smc_size;
    uint8_t smc_offset;
} nand_header_t;

typedef struct NandResults {
    bool valid;
    uint8_t cb_or_a_offset;
    uint8_t cb_or_a_version;
    uint8_t cb_or_a_size;
    std::optional<uint8_t> cbx_offset;
    std::optional<uint8_t> cbx_version;
    std::optional<uint8_t> cbx_size;
    std::optional<uint8_t> cbb_offset;
    std::optional<uint8_t> cbb_version;
    std::optional<uint8_t> cbb_size;
    uint8_t cd_offset;
    uint8_t cd_version;
    uint8_t cd_size;
    std::optional<uint8_t> ce_offset;
    std::optional<uint8_t> ce_version;
    std::optional<uint8_t> ce_size;
    // patchslot 0
    std::optional<uint8_t> cf0_offset;
    std::optional<uint8_t> cf0_version;
    std::optional<uint8_t> cf0_size;
    std::optional<uint8_t> cg0_offset;
    std::optional<uint8_t> cg0_version;
    std::optional<uint8_t> cg0_size;
    // patchslot 1
    std::optional<uint8_t> cf1_offset;
    std::optional<uint8_t> cf1_version;
    std::optional<uint8_t> cf1_size;
    std::optional<uint8_t> cg1_offset;
    std::optional<uint8_t> cg1_version;
    std::optional<uint8_t> cg1_size;
    uint8_t kv_offset;
    uint8_t kv_size;
    uint8_t smc_size;
    uint8_t smc_offset;
} nand_results_t;

typedef struct _patchslot_t {
    uint8_t cf;
    uint8_t cg;
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

typedef struct FlashImage {
    nand_header_t header;
    std::optional<uint8_t> cb_or_A;
    std::optional<uint8_t> cb_X;
    std::optional<uint8_t> cb_B;
    std::optional<uint8_t> sc;
    std::optional<uint8_t> cd;
    std::optional<uint8_t> ce;
    std::optional<patchslot_t> patchslot_1;
    std::optional<patchslot_t> patchslot_2;
    std::optional<uint8_t> smc;
    std::optional<uint8_t> xconfig;
    std::optional<uint8_t> keyvault;

    std::optional<xellblock_t> xellblock;
    std::optional<payloads_t> payloads;
    
    std::optional<gxbuild3::bootloaders::FlashFileSystem> filesystem;
    std::optional<std::vector<gxbuild3::bootloaders::FlashMobileData>> mobile_data;
    std::optional<gxbuild3::bootloaders::XeCoronaFsData> corona_fs_data;
} flash_image_t;

typedef struct FlashImageMetadata {
    std::optional<std::vector<uint8_t>> cpu_key;
    std::optional<uint8_t> cbldv;
    std::optional<uint8_t> cfldv;
    std::optional<std::vector<uint8_t>> pairingdata;
} flash_image_metadata_t;

nand_results_t read(const std::vector<uint8_t>& data);

flash_image_t parse(const std::vector<uint8_t>& data);

bool decrypt_all(const flash_image_t& flash, const std::vector<uint8_t>& cpu_key);

bool encrypt_all(const flash_image_t& flash, const std::vector<uint8_t>& cpu_key);

bool extract_all(const flash_image_t& flash, std::string output_path);

bool build(const flash_image_t& flash, std::string output_path);