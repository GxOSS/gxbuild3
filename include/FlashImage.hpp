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

typedef struct NandFlashImage {
    nand_header_t header;
    BootloaderCb cb_or_A;
    BootloaderCb cb_B;
    BootloaderCb cb_X;
    BootloaderCd cd;
    BootloaderCe ce;
    BootloaderCf cf;
    BootloaderCg cg;
    SmcFirmware smc;
    xconfig_master_t xconfig;
    CXeKeyVault keyvault;
} nand_flash_image_t;