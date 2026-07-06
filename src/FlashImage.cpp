#include "FlashImage.hpp"
#include "Utils.hpp"
#include "Log.hpp"
#include "bootloaders/Common.hpp"
#include "bootloaders/Keyvault.hpp"
#include "bootloaders/SMC.hpp"
#include "utils/FlashBlockDriver.hpp"
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>

using namespace gxbuild3::bootloaders;

bl_type get_bl_type(uint16_t value) {
    switch (value) {
        case 0x342: return CB;
        case 0x343: return SC;
        case 0x344: return CD;
        case 0x345: return CE;
        case 0x346: return CF;
        case 0x347: return CG;
        case 0xD4D: return XKE;
        case 0xE4E: return KV;
        default:
            throw std::runtime_error("Unknown bootloader type: " + std::to_string(value));
    }
}
// Helper function to read a bootloader header at a given offset using FlashBlockDriver
static auto read_bl_header(const gxbuild3::utils::FlashBlockDriver& driver, uint32_t offset) -> std::optional<bl_results_t> {
    // Check bounds: bl_header is 0x10 bytes
    if (offset + sizeof(bl_header) > driver.image_length_real()) {
        Log::Trace("read_bl_header: offset {:x} + {:x} exceeds image size {:x}",
                   offset, sizeof(bl_header), driver.image_length_real());
        return std::nullopt;
    }
    
    auto header_data = driver.read(offset, sizeof(bl_header));
    if (!header_data) {
        Log::Trace("read_bl_header: failed to read at offset {:x}", offset);
        return std::nullopt;
    }
    const auto* bl_hdr = reinterpret_cast<const bl_header*>(header_data->data());
    
    uint16_t magic = bswap16(bl_hdr->magic);
    if (magic == 0) {
        return std::nullopt;
    }
    
    bl_results_t result;
    result.magic = magic;
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

nand_results_t read(const gxbuild3::utils::FlashBlockDriver& driver) {
    nand_results_t results{};
    const size_t image_size = driver.image_length_real();
    
    // Check if data is large enough to contain the raw NAND header
    if (image_size < sizeof(raw_nand_header_t)) {
        results.valid = false;
        return results;
    }
    
    // Read the raw NAND header from the data using driver
    auto header_data = driver.read(0, sizeof(raw_nand_header_t));
    if (!header_data) {
        results.valid = false;
        return results;
    }
    const raw_nand_header_t* header = reinterpret_cast<const raw_nand_header_t*>(header_data->data());
    
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
    auto cb_result = read_bl_header(driver, cb_offset);
    if (!cb_result) {
        results.valid = false;
        return results;
    }
    results.cb_or_a = *cb_result;
    
    // Walk the bootloader chain
    uint32_t current_offset = results.cb_or_a.offset;
    uint32_t current_size = results.cb_or_a.size;
    
    // Check if CB has flags & 0x0800 (dual 2BL)
    auto cb_hdr_data = driver.read(current_offset, sizeof(bl_header));
    if (!cb_hdr_data) {
        return results;
    }
    const auto* cb_hdr = reinterpret_cast<const bl_header*>(cb_hdr_data->data());
    uint16_t cb_flags = bswap16(cb_hdr->flags);
    if ((cb_flags & 0x0800) == 0x0800) {
        uint32_t next_cb_offset = find_next_bl_offset(current_offset, current_size);
        auto next_cb_result = read_bl_header(driver, next_cb_offset);
        if (next_cb_result) {
            uint32_t possible_other_cb_offset = find_next_bl_offset(next_cb_offset, next_cb_result->size);
            auto possible_other_cb_result = read_bl_header(driver, possible_other_cb_offset);
            if (possible_other_cb_result && get_bl_type(possible_other_cb_result->magic) == CB) {
                results.cbx = *next_cb_result;
                results.cbb = *possible_other_cb_result;
                current_offset = results.cbb->offset;
                current_size = results.cbb->size;
            } else {
                results.cbb = *next_cb_result;
                current_offset = results.cbb->offset;
                current_size = results.cbb->size;
            }
        }
    }
    
    // Parse CD (3BL)
    uint32_t next_offset = find_next_bl_offset(current_offset, current_size);
    auto cd_result = read_bl_header(driver, next_offset);
    if (cd_result) {
        results.cd = *cd_result;
        current_offset = results.cd.offset;
        current_size = results.cd.size;
        
        // Parse CE (4BL)
        next_offset = find_next_bl_offset(current_offset, current_size);
        auto ce_result = read_bl_header(driver, next_offset);
        if (ce_result) {
            results.ce = *ce_result;
            current_offset = results.ce->offset;
            current_size = results.ce->size;
            
            // Parse CF (5BL)
            next_offset = find_next_bl_offset(current_offset, current_size);
            auto cf_result = read_bl_header(driver, next_offset);
            if (cf_result) {
                results.cf0 = *cf_result;
                current_offset = results.cf0->offset;
                current_size = results.cf0->size;
                
                // Parse CG (6BL)
                next_offset = find_next_bl_offset(current_offset, current_size);
                auto cg_result = read_bl_header(driver, next_offset);
                if (cg_result) {
                    results.cg0 = *cg_result;
                }
            }
        }
    }

    // Also check CF from NAND header if we haven't found it yet
    uint32_t cf1_offset_swapped = bswap32(header->cf1_offset);
    if (!results.cf0 && cf1_offset_swapped != 0 && cf1_offset_swapped != 0xFFFFFFFF) {
        auto cf_result = read_bl_header(driver, cf1_offset_swapped);
        if (cf_result) {
            results.cf0 = *cf_result;
            next_offset = find_next_bl_offset(results.cf0->offset, results.cf0->size);
            auto cg0_result = read_bl_header(driver, next_offset);
            if (cg0_result) {
                results.cg0 = *cg0_result;
            }
        }
    }
    
    // Handle patchslots
    if (results.patch_slots > 1 && results.cf0) {
        uint32_t cf1_offset = results.cf0->offset + results.cf0->size;
        auto cf1_result = read_bl_header(driver, cf1_offset);
        if (cf1_result) {
            results.cf1 = *cf1_result;
        } else if (cf1_offset_swapped != 0 && cf1_offset_swapped != 0xFFFFFFFF) {
            cf1_result = read_bl_header(driver, cf1_offset_swapped);
            if (cf1_result) {
                results.cf1 = *cf1_result;
            }
        }
        
        if (results.cg0) {
            uint32_t cg1_offset = results.cg0->offset + results.cg0->size;
            auto cg1_result = read_bl_header(driver, cg1_offset);
            if (cg1_result) {
                results.cg1 = *cg1_result;
            }
        }
    }
    
    return results;
}

// Backward compatibility wrapper
nand_results_t read(const std::vector<uint8_t>& data) {
    gxbuild3::utils::FlashBlockDriver driver{std::vector<uint8_t>(data)};
    if (!driver.open_continue(driver.image_data().size(), 528)) {
        nand_results_t out{};
        out.valid = false;
        return out;
    }
    return read(static_cast<const gxbuild3::utils::FlashBlockDriver&>(driver));
}

FlashImage FlashImage::parse(std::vector<uint8_t> data) {
    FlashImage image{};

    image.driver = gxbuild3::utils::FlashBlockDriver(std::move(data));
    if (!image.driver.open_continue(image.driver.image_data().size(), 528)) {
        Log::Error("FlashImage::parse: Failed to initialize FlashBlockDriver");
        return image;
    }

    auto header_data = image.driver.read(0, sizeof(raw_nand_header_t));
    if (header_data) {
        std::memcpy(&image.header, header_data->data(), sizeof(raw_nand_header_t));
    }

    return image;
}

flash_image_t parse(const std::vector<uint8_t>& data) {
    return FlashImage::parse(std::vector<uint8_t>(data));
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

    const auto& driver = flash.driver;
    nand_results_t results = read(driver);
    if (!results.valid) {
        Log::Error("Extraction failed: invalid NAND");
        return false;
    }

    auto extract_region = [&](uint32_t offset, uint32_t size) -> std::optional<std::vector<uint8_t>> {
        if (offset == 0 || offset == 0xFFFFFFFF || size == 0)
            return std::nullopt;
        return driver.read(offset, size);
    };

    uint8_t cb_key[16] = {};
    uint8_t cb_b_key[16] = {};
    uint8_t ce_key[16] = {};

    auto cb_bytes = extract_region(results.cb_or_a.offset, results.cb_or_a.size);
    if (cb_bytes) {
        auto cb = BootloaderCb::parse(*cb_bytes);
        if (!bl_key_bytes.empty())
            cb.decrypt(bl_key_bytes.data());

        const auto cb_serialized = cb.serialize();
        const std::string filename = cb.is_decrypted() ? "CB.bin" : "CB_enc.bin";
        WriteFile(output_dir / filename, AsByteSpan(cb_serialized));
        Log::Info("Extracted 2BL: {} (v{})", filename, cb.header.header.version);

        if (cb.is_decrypted() && cb.derived_key)
            std::memcpy(cb_key, cb.derived_key->data(), 16);
    }

    if (results.cbx) {
        auto cbx_bytes = extract_region(results.cbx->offset, results.cbx->size);
        if (cbx_bytes) {
            const std::string filename = "CB_X.bin";
            WriteFile(output_dir / filename, AsByteSpan(*cbx_bytes));
            Log::Info("Extracted 2BL_X: {}", filename);
        }
    }

    if (results.cbb && cb_bytes) {
        auto cbb_bytes = extract_region(results.cbb->offset, results.cbb->size);
        if (cbb_bytes) {
            auto cb_b = BootloaderCb::parse(*cbb_bytes);
            if (cb_key[0] != 0 && !cpu_key_bytes.empty()) {
                auto cb_a = BootloaderCb::parse(*cb_bytes);
                cb_b.decrypt_v2(cb_a.header, cb_key, cpu_key_bytes.data());
            }

            const auto cb_b_serialized = cb_b.serialize();
            const std::string filename = cb_b.is_decrypted() ? "CB_B.bin" : "CB_B_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cb_b_serialized));
            Log::Info("Extracted 2BL_B: {} (v{})", filename, cb_b.header.header.version);

            if (cb_b.is_decrypted() && cb_b.derived_key)
                std::memcpy(cb_b_key, cb_b.derived_key->data(), 16);
        }
    }

    auto cd_bytes = extract_region(results.cd.offset, results.cd.size);
    uint8_t cd_key[16] = {};
    if (cd_bytes) {
        auto cd = BootloaderSc::parse(*cd_bytes);
        const uint8_t* active_cb_key = (cb_b_key[0] != 0) ? cb_b_key : cb_key;
        if (active_cb_key[0] != 0)
            cd.decrypt(active_cb_key);

        const auto cd_serialized = cd.serialize();
        const std::string filename = cd.is_decrypted() ? "CD.bin" : "CD_enc.bin";
        WriteFile(output_dir / filename, AsByteSpan(cd_serialized));
        Log::Info("Extracted 3BL/SC: {} (v{})", filename, cd.header.header.header.version);

        if (cd.is_decrypted())
            ExCryptHmacSha(active_cb_key, 16, cd.header.header.key, 16, nullptr, 0, nullptr, 0,
                           cd_key, 16);
    }

    if (results.ce) {
        auto ce_bytes = extract_region(results.ce->offset, results.ce->size);
        if (ce_bytes) {
            auto ce = BootloaderCd::parse(*ce_bytes);
            const uint8_t* active_cb_key = (cb_b_key[0] != 0) ? cb_b_key : cb_key;
            if (active_cb_key[0] != 0)
                ce.decrypt(active_cb_key, cpu_key_bytes.empty() ? nullptr : cpu_key_bytes.data());

            const auto ce_serialized = ce.serialize();
            const std::string filename = ce.is_decrypted() ? "CE.bin" : "CE_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(ce_serialized));
            Log::Info("Extracted 4BL/SD: {} (v{})", filename, ce.header.header.version);

            if (ce.is_decrypted())
                ExCryptHmacSha(cpu_key_bytes.empty() ? active_cb_key : cpu_key_bytes.data(), 16,
                               ce.header.rsa_pub_key, 16, nullptr, 0, nullptr, 0, ce_key, 16);
        }
    }

    if (results.cf0) {
        auto cf_bytes = extract_region(results.cf0->offset, results.cf0->size);
        if (cf_bytes) {
            auto cf = BootloaderCe::parse(*cf_bytes);
            if (ce_key[0] != 0)
                cf.decrypt(ce_key);

            const auto cf_serialized = cf.serialize();
            const std::string filename = cf.is_decrypted() ? "CF.bin" : "CF_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cf_serialized));
            Log::Info("Extracted 5BL/SE: {} (v{})", filename, cf.header.header.version);
        }
    }

    if (results.cg0) {
        auto cg_bytes = extract_region(results.cg0->offset, results.cg0->size);
        if (cg_bytes) {
            auto cg = BootloaderCf::parse(*cg_bytes);
            if (!bl_key_bytes.empty())
                cg.decrypt(bl_key_bytes.data());

            const auto cg_serialized = cg.serialize();
            const std::string filename = cg.is_decrypted() ? "CG.bin" : "CG_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cg_serialized));
            Log::Info("Extracted 6BL/SF: {} (v{})", filename, cg.header.header.version);
        }
    }

    if (results.cf1) {
        auto cf_bytes = extract_region(results.cf1->offset, results.cf1->size);
        if (cf_bytes) {
            auto cf = BootloaderCe::parse(*cf_bytes);
            if (ce_key[0] != 0)
                cf.decrypt(ce_key);

            const auto cf_serialized = cf.serialize();
            const std::string filename = cf.is_decrypted() ? "CF_slot1.bin" : "CF_slot1_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cf_serialized));
            Log::Info("Extracted 5BL/SE Slot 1: {} (v{})", filename, cf.header.header.version);
        }
    }

    if (results.cg1) {
        auto cg_bytes = extract_region(results.cg1->offset, results.cg1->size);
        if (cg_bytes) {
            auto cg = BootloaderCf::parse(*cg_bytes);
            if (!bl_key_bytes.empty())
                cg.decrypt(bl_key_bytes.data());

            const auto cg_serialized = cg.serialize();
            const std::string filename = cg.is_decrypted() ? "CG_slot1.bin" : "CG_slot1_enc.bin";
            WriteFile(output_dir / filename, AsByteSpan(cg_serialized));
            Log::Info("Extracted 6BL/SF Slot 1: {} (v{})", filename, cg.header.header.version);
        }
    }

    auto smc_bytes = extract_region(results.smc_offset, results.smc_size);
    if (smc_bytes) {
        const auto smc_dec = smc_decrypt(*smc_bytes);
        WriteFile(output_dir / "smc_dec.bin", AsByteSpan(smc_dec));
        WriteFile(output_dir / "smc_raw.bin", AsByteSpan(*smc_bytes));
        Log::Info("Extracted SMC: smc_dec.bin / smc_raw.bin");
    }

    auto kv_bytes = extract_region(results.kv_offset, results.kv_size);
    if (kv_bytes) {
        WriteFile(output_dir / "kv_raw.bin", AsByteSpan(*kv_bytes));
        Log::Info("Extracted Keyvault: kv_raw.bin");
        if (!cpu_key_bytes.empty()) {
            try {
                const auto kv_dec = keyvault_decrypt(cpu_key_bytes, *kv_bytes);
                WriteFile(output_dir / "kv_dec.bin", AsByteSpan(kv_dec));
                Log::Info("Extracted decrypted Keyvault: kv_dec.bin");
            } catch (const std::exception& e) {
                Log::Warn("Failed to decrypt KV: {}", e.what());
            }
        }
    }

    return true;
}

std::vector<uint8_t> build(const flash_image_t& image) {
    return image.driver.image_data();
}
