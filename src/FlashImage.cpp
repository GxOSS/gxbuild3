#include "FlashImage.hpp"
#include "Utils.hpp"
#include "Log.hpp"
#include "bootloaders/Common.hpp"
#include "bootloaders/Keyvault.hpp"
#include "bootloaders/SMC.hpp"
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>

using namespace gxbuild3::bootloaders;

// Helper function to read a bootloader header at a given offset
static auto read_bl_header(const std::vector<uint8_t>& data, uint32_t offset) -> std::optional<bl_results_t> {
    // Check bounds: bl_header is 0x10 bytes
    if (offset + sizeof(bl_header) > data.size()) {
        return std::nullopt;
    }
    
    const auto* bl_hdr = reinterpret_cast<const bl_header*>(data.data() + offset);
    
    // Validate magic
    uint16_t magic = bswap16(bl_hdr->magic);
    if (magic == 0) {
        return std::nullopt;
    }
    
    bl_results_t result;
    result.offset = offset;
    result.version = bswap16(bl_hdr->version);
    result.size = bswap32(bl_hdr->size);
    
    return result;
}

// Helper function to find the next bootloader in the chain
static uint32_t find_next_bl_offset(uint32_t current_offset, uint32_t current_size) {
    // Align to 16 bytes (0x10)
    return current_offset + ((current_size + 0xF) & ~0xF);
}

nand_results_t read(const std::vector<uint8_t>& data) {
    nand_results_t results{};
    
    // Check if data is large enough to contain the raw NAND header
    if (data.size() < sizeof(raw_nand_header_t)) {
        results.valid = false;
        return results;
    }
    
    // Read the raw NAND header from the data
    const raw_nand_header_t* header = reinterpret_cast<const raw_nand_header_t*>(data.data());
    
    // Validate magic
    uint16_t magic = bswap16(header->magic);
    if (magic == 0) {
        results.valid = false;
        return results;
    }
    
    // Set valid flag
    results.valid = true;
    
    // Parse NAND header metadata with proper endian swapping
    results.kv_offset = bswap32(header->kv_offset);
    results.kv_size = bswap32(header->kv_size);
    results.kv_version = bswap16(header->kv_version);
    results.smc_size = bswap32(header->smc_size);
    results.smc_offset = bswap32(header->smc_offset);
    results.smc_config_offset = bswap32(header->smc_config_offset);
    results.fs_offset = bswap32(header->fs_offset);
    results.payload_indicator = bswap32(header->payload_indicator);
    results.patch_slots = bswap16(header->patch_slots);
    
    // Parse CB (2BL) offset
    // entrypoint at 0x08 serves as the CB offset
    uint32_t cb_offset = bswap32(header->entrypoint);
    auto cb_result = read_bl_header(data, cb_offset);
    if (!cb_result) {
        results.valid = false;
        return results;
    }
    results.cb_or_a = *cb_result;
    
    // Walk the bootloader chain
    uint32_t current_offset = results.cb_or_a.offset;
    uint32_t current_size = results.cb_or_a.size;
    
    // Check if CB has flags & 0x0800 (dual 2BL)
    const auto* cb_hdr = reinterpret_cast<const bl_header*>(data.data() + current_offset);
    uint16_t cb_flags = bswap16(cb_hdr->flags);
    if ((cb_flags & 0x0800) == 0x0800) {
        uint32_t cb_b_offset = find_next_bl_offset(current_offset, current_size);
        auto cb_b_result = read_bl_header(data, cb_b_offset);
        if (cb_b_result) {
            results.cbb = *cb_b_result;
            current_offset = results.cbb->offset;
            current_size = results.cbb->size;
        }
    }
    
    // Parse CD (3BL)
    uint32_t next_offset = find_next_bl_offset(current_offset, current_size);
    auto cd_result = read_bl_header(data, next_offset);
    if (cd_result) {
        results.cd = *cd_result;
        current_offset = results.cd.offset;
        current_size = results.cd.size;
        
        // Parse CE (4BL)
        next_offset = find_next_bl_offset(current_offset, current_size);
        auto ce_result = read_bl_header(data, next_offset);
        if (ce_result) {
            results.ce = *ce_result;
            current_offset = results.ce->offset;
            current_size = results.ce->size;
            
            // Parse CF (5BL)
            next_offset = find_next_bl_offset(current_offset, current_size);
            auto cf_result = read_bl_header(data, next_offset);
            if (cf_result) {
                results.cf0 = *cf_result;
                current_offset = results.cf0->offset;
                current_size = results.cf0->size;
                
                // Parse CG (6BL)
                next_offset = find_next_bl_offset(current_offset, current_size);
                auto cg_result = read_bl_header(data, next_offset);
                if (cg_result) {
                    results.cg0 = *cg_result;
                }
            }
        }
    }
    
    // Also check CF from NAND header if we haven't found it yet
    uint32_t cf1_offset_swapped = bswap32(header->cf1_offset);
    if (!results.cf0 && cf1_offset_swapped != 0 && cf1_offset_swapped != 0xFFFFFFFF) {
        auto cf_result = read_bl_header(data, cf1_offset_swapped);
        if (cf_result) {
            results.cf0 = *cf_result;
        }
    }
    
    // Handle patchslots
    if (results.patch_slots > 1 && results.cf0) {
        uint32_t cf1_offset = results.cf0->offset + results.cf0->size;
        auto cf1_result = read_bl_header(data, cf1_offset);
        if (cf1_result) {
            results.cf1 = *cf1_result;
        } else if (cf1_offset_swapped != 0 && cf1_offset_swapped != 0xFFFFFFFF) {
            cf1_result = read_bl_header(data, cf1_offset_swapped);
            if (cf1_result) {
                results.cf1 = *cf1_result;
            }
        }
        
        if (results.cg0) {
            uint32_t cg1_offset = results.cg0->offset + results.cg0->size;
            auto cg1_result = read_bl_header(data, cg1_offset);
            if (cg1_result) {
                results.cg1 = *cg1_result;
            }
        }
    }
    
    return results;
}

flash_image_t FlashImage::parse(const std::vector<uint8_t>& data) {
    flash_image_t image{};
    
    nand_results_t results = read(data);
    if (!results.valid) {
        return image;
    }
    
    if (data.size() >= sizeof(raw_nand_header_t)) {
        const raw_nand_header_t* header = reinterpret_cast<const raw_nand_header_t*>(data.data());
        image.header = *header;
    }
    
    auto extract_bootloader = [&](uint32_t offset, uint32_t size) -> std::optional<std::vector<uint8_t>> {
        if (offset == 0 || offset >= data.size() || size == 0) {
            return std::nullopt;
        }
        
        if (offset + size > data.size()) {
            return std::nullopt;
        }
        
        return std::vector<uint8_t>(data.begin() + offset, data.begin() + offset + size);
    };
    
    if (results.cb_or_a.offset != 0 && results.cb_or_a.size != 0) {
        image.cb_or_A = extract_bootloader(results.cb_or_a.offset, results.cb_or_a.size);
    }
    
    if (results.cbb && results.cbb->offset != 0 && results.cbb->size != 0) {
        image.cb_B = extract_bootloader(results.cbb->offset, results.cbb->size);
    }
    
    if (results.cd.offset != 0 && results.cd.size != 0) {
        image.cd = extract_bootloader(results.cd.offset, results.cd.size);
    }
    
    if (results.ce && results.ce->offset != 0 && results.ce->size != 0) {
        image.ce = extract_bootloader(results.ce->offset, results.ce->size);
    }
    
    if (results.cf0 && results.cf0->offset != 0 && results.cf0->size != 0) {
        patchslot_t slot0;
        slot0.cf = extract_bootloader(results.cf0->offset, results.cf0->size);
        if (results.cg0 && results.cg0->offset != 0 && results.cg0->size != 0) {
            slot0.cg = extract_bootloader(results.cg0->offset, results.cg0->size);
        }
        image.patchslot_0 = slot0;
    }
    
    if (results.patch_slots > 1 && results.cf1 && results.cf1->offset != 0 && results.cf1->size != 0) {
        patchslot_t slot1;
        slot1.cf = extract_bootloader(results.cf1->offset, results.cf1->size);
        if (results.cg1 && results.cg1->offset != 0 && results.cg1->size != 0) {
            slot1.cg = extract_bootloader(results.cg1->offset, results.cg1->size);
        }
        image.patchslot_1 = slot1;
    }
    
    if (results.smc_offset != 0 && results.smc_size != 0 && results.smc_offset != 0xFFFFFFFF) {
        image.smc = extract_bootloader(results.smc_offset, results.smc_size);
    }
    
    if (results.kv_offset != 0 && results.kv_size != 0 && results.kv_offset != 0xFFFFFFFF) {
        image.keyvault = extract_bootloader(results.kv_offset, results.kv_size);
    }
    
    return image;
}

static bool WriteFile(const std::filesystem::path& path, std::span<const std::byte> data) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

static std::span<const std::byte> AsByteSpan(const std::vector<uint8_t>& data) {
    return {reinterpret_cast<const std::byte*>(data.data()), data.size()};
}

bool extract_all(const flash_image_t& flash, const std::filesystem::path& output_dir, const std::vector<uint8_t>& cpu_key_bytes, const std::vector<uint8_t>& bl_key_bytes) {
    std::filesystem::create_directories(output_dir);
    
    uint8_t cb_key[16] = {0};
    if (flash.cb_or_A) {
        auto cb = BootloaderCb::parse(*flash.cb_or_A);
        if (!bl_key_bytes.empty()) {
            cb.decrypt(bl_key_bytes.data());
        }
        std::vector<uint8_t> cb_serialized = cb.serialize();
        
        std::string filename = cb.is_decrypted() ? "CB.bin" : "CB_enc.bin";
        WriteFile(output_dir / filename, AsByteSpan(cb_serialized));
        Log::Info("Extracted 2BL: {} (v{})", filename, cb.header.header.version);
        
        if (cb.is_decrypted()) {
            std::memcpy(cb_key, cb.derived_key.data(), 16);
        }
    }
    
    uint8_t cb_b_key[16] = {0};
    if (flash.cb_B && flash.cb_or_A) {
        auto cb_b = BootloaderCb::parse(*flash.cb_B);
        if (cb_key[0] != 0 && !cpu_key_bytes.empty()) {
            auto cb_a = BootloaderCb::parse(*flash.cb_or_A);
            cb_b.decrypt_v2(cb_a.header, cb_key, cpu_key_bytes.data());
        }
        std::vector<uint8_t> cb_b_serialized = cb_b.serialize();
        
        std::string filename = cb_b.is_decrypted() ? "CB_B.bin" : "CB_B_enc.bin";
        WriteFile(output_dir / filename, AsByteSpan(cb_b_serialized));
        Log::Info("Extracted 2BL_B: {} (v{})", filename, cb_b.header.header.version);
        
        if (cb_b.is_decrypted()) {
            std::memcpy(cb_b_key, cb_b.derived_key.data(), 16);
        }
    }
    
    uint8_t cd_key[16] = {0};
    if (flash.cd) {
        auto cd = BootloaderSc::parse(*flash.cd);
        const uint8_t* active_cb_key = (cb_b_key[0] != 0) ? cb_b_key : cb_key;
        if (active_cb_key[0] != 0) {
            cd.decrypt(active_cb_key);
        }
        std::vector<uint8_t> cd_serialized = cd.serialize();
        
        std::string filename = cd.is_decrypted() ? "CD.bin" : "CD_enc.bin";
        WriteFile(output_dir / filename, AsByteSpan(cd_serialized));
        Log::Info("Extracted 3BL/SC: {} (v{})", filename, cd.header.header.header.version);
        
        if (cd.is_decrypted()) {
            ExCryptHmacSha(active_cb_key, 16, cd.header.header.key, 16, nullptr, 0, nullptr, 0, cd_key, 16);
        }
    }
    
    uint8_t ce_key[16] = {0};
    if (flash.ce) {
        auto ce = BootloaderCd::parse(*flash.ce);
        const uint8_t* active_cb_key = (cb_b_key[0] != 0) ? cb_b_key : cb_key;
        if (active_cb_key[0] != 0) {
            ce.decrypt(active_cb_key, cpu_key_bytes.empty() ? nullptr : cpu_key_bytes.data());
        }
        std::vector<uint8_t> ce_serialized = ce.serialize();
        
        std::string filename = ce.is_decrypted() ? "CE.bin" : "CE_enc.bin";
        WriteFile(output_dir / filename, AsByteSpan(ce_serialized));
        Log::Info("Extracted 4BL/SD: {} (v{})", filename, ce.header.header.version);
        
        if (ce.is_decrypted()) {
            ExCryptHmacSha(cpu_key_bytes.empty() ? active_cb_key : cpu_key_bytes.data(), 16, ce.header.rsa_pub_key, 16, nullptr, 0, nullptr, 0, ce_key, 16);
        }
    }
    
    uint8_t cf_key[16] = {0};
    if (flash.patchslot_0) {
        if (flash.patchslot_0->cf) {
            auto cf = BootloaderCe::parse(*flash.patchslot_0->cf);
            if (ce_key[0] != 0) {
                cf.decrypt(ce_key);
            }
            std::vector<uint8_t> cf_serialized = cf.serialize();
            
            std::string filename = cf.is_decrypted() ? "CF.bin" : "CF_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cf_serialized));
            Log::Info("Extracted 5BL/SE: {} (v{})", filename, cf.header.header.version);
            
            if (cf.is_decrypted()) {
                ExCryptHmacSha(ce_key, 16, cf.header.key, 16, nullptr, 0, nullptr, 0, cf_key, 16);
            }
        }
        
        if (flash.patchslot_0->cg) {
            auto cg = BootloaderCf::parse(*flash.patchslot_0->cg);
            if (!bl_key_bytes.empty()) {
                cg.decrypt(bl_key_bytes.data());
            }
            std::vector<uint8_t> cg_serialized = cg.serialize();
            
            std::string filename = cg.is_decrypted() ? "CG.bin" : "CG_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cg_serialized));
            Log::Info("Extracted 6BL/SF: {} (v{})", filename, cg.header.header.version);
        }
    }
    
    if (flash.patchslot_1) {
        if (flash.patchslot_1->cf) {
            auto cf = BootloaderCe::parse(*flash.patchslot_1->cf);
            if (ce_key[0] != 0) {
                cf.decrypt(ce_key);
            }
            std::vector<uint8_t> cf_serialized = cf.serialize();
            
            std::string filename = cf.is_decrypted() ? "CF_slot1.bin" : "CF_slot1_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cf_serialized));
            Log::Info("Extracted 5BL/SE Slot 1: {} (v{})", filename, cf.header.header.version);
        }
        
        if (flash.patchslot_1->cg) {
            auto cg = BootloaderCf::parse(*flash.patchslot_1->cg);
            if (!bl_key_bytes.empty()) {
                cg.decrypt(bl_key_bytes.data());
            }
            std::vector<uint8_t> cg_serialized = cg.serialize();
            
            std::string filename = cg.is_decrypted() ? "CG_slot1.bin" : "CG_slot1_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cg_serialized));
            Log::Info("Extracted 6BL/SF Slot 1: {} (v{})", filename, cg.header.header.version);
        }
    }
    
    if (flash.smc) {
        std::vector<uint8_t> smc_dec = smc_decrypt(*flash.smc);
        WriteFile(output_dir / "smc_dec.bin", AsByteSpan(smc_dec));
        WriteFile(output_dir / "smc_raw.bin", AsByteSpan(*flash.smc));
        Log::Info("Extracted SMC: smc_dec.bin / smc_raw.bin");
    }
    
    if (flash.keyvault) {
        WriteFile(output_dir / "kv_raw.bin", AsByteSpan(*flash.keyvault));
        Log::Info("Extracted Keyvault: kv_raw.bin");
        if (!cpu_key_bytes.empty()) {
            try {
                std::vector<uint8_t> kv_dec = keyvault_decrypt(cpu_key_bytes, *flash.keyvault);
                WriteFile(output_dir / "kv_dec.bin", AsByteSpan(kv_dec));
                Log::Info("Extracted decrypted Keyvault: kv_dec.bin");
            } catch (const std::exception& e) {
                Log::Warn("Failed to decrypt KV: {}", e.what());
            }
        }
    }
    
    return true;
}

bool build(const flash_image_t& flash, std::string output_path) {
    (void)flash;
    (void)output_path;
    return false;
}