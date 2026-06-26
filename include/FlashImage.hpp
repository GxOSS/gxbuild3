#pragma once
#include "bootloaders/2bl.hpp"
#include "bootloaders/3bl.hpp"
#include "bootloaders/4bl.hpp"
#include "bootloaders/5bl.hpp"
#include "bootloaders/6bl.hpp"
#include "bootloaders/7bl.hpp"
#include "bootloaders/XConfig.hpp"
#include "bootloaders/SMC.hpp"
#include "bootloaders/Keyvault.hpp"
#include "bootloaders/Common.hpp"
#include "bootloaders/FlashFileSystem.hpp"

typedef struct _patchslot_t {
    BootloaderCf cf;
    BootloaderCg cg;
} patchslot_t;

typedef struct FlashImage {
    nand_header_t header;
    std::optional<BootloaderCb> cb_or_A;
    std::optional<BootloaderCb> cb_X;
    std::optional<BootloaderCb> cb_B;
    std::optional<BootloaderSc> sc;
    std::optional<BootloaderCd> cd;
    std::optional<BootloaderCe> ce;
    std::optional<patchslot_t> patchslot_1;
    std::optional<patchslot_t> patchslot_2;
    std::optional<uint8_t> smc;
    std::optional<xconfig_master_t> xconfig;
    std::optional<CXeKeyVault> keyvault;
    
    // Filesystem components
    std::optional<gxbuild3::bootloaders::FlashFileSystem> filesystem;
    std::vector<gxbuild3::bootloaders::FlashMobileData> mobile_data;
    std::optional<gxbuild3::bootloaders::XeCoronaFsData> corona_fs_data;
} flash_image_t;

