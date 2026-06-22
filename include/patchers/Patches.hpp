typedef struct _smc_patch {
    std::string addr;
    std::string value;
} smc_patch_t;

smc_patch_t Glitch {
    "05 ?? E5 ?? B4 05",
    "00 00 ?? ?? ?? ??"
};

