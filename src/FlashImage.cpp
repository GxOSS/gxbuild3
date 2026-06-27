#include "FlashImage.hpp"
#include "bootloaders/Common.hpp"

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
// Helper function to read a bootloader header at a given offset
static auto read_bl_header(const std::vector<uint8_t>& data, uint32_t offset) -> std::optional<bl_results_t> {
    // Check bounds: bl_header is 0x10 bytes
    if (offset + sizeof(bl_header) > data.size()) {
        return std::nullopt;
    }
    
    const auto* bl_hdr = reinterpret_cast<const bl_header*>(data.data() + offset);
    
    // Validate magic - bootloader headers typically have ASCII magic
    // e.g., "2BL0" for 2BL, "3BL0" for 3BL, etc.
    if (bl_hdr->magic == 0) {
        return std::nullopt;
    }
    
    bl_results_t result;
    result.magic = bl_hdr->magic;
    result.offset = offset;
    result.version = bl_hdr->version;
    result.size = bl_hdr->size;
    
    return result;
}

// Helper function to find the next bootloader in the chain
// For CB (2BL), the next bootloader info is in the bl2_header
static uint32_t find_next_bl_offset(uint32_t current_offset, uint32_t current_size) {
    // For now, assume sequential layout for CB -> CD -> CE
    // The next bootloader is right after the current one
    return current_offset + current_size;
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
    
    // Validate magic - should be non-zero (bootloader magic is typically ASCII like "2BL0", "3BL0", etc.)
    if (header->magic == 0) {
        results.valid = false;
        return results;
    }
    
    // Set valid flag
    results.valid = true;
    
    // Parse NAND header metadata
    results.kv_offset = header->kv_offset;
    results.kv_size = header->kv_size;
    results.kv_version = header->kv_version;
    results.smc_size = header->smc_size;
    results.smc_offset = header->smc_offset;
    results.smc_config_offset = header->smc_config_offset;
    results.fs_offset = header->fs_offset;
    results.payload_indicator = header->payload_indicator;
    results.patch_slots = header->patch_slots;
    
    // Parse CB (2BL) offset from header
    // First CB is always CB_or_A
    auto cb_result = read_bl_header(data, header->entrypoint);
    if (!cb_result) {
        results.valid = false;
        return results;
    }
    results.cb_or_a = *cb_result;

    uint32_t current_offset = results.cb_or_a.offset;
    uint32_t current_size = results.cb_or_a.size;
    
    uint32_t next_offset = find_next_bl_offset(current_offset, current_size);
    auto next_result = read_bl_header(data, next_offset);

    if (next_result) {
        bl_type bootloader_type = get_bl_type(next_result.magic);
        next_offset = find_next_bl_offset(next_result.offset, next_result.size);
        if (bootloader_type == CB) {
            // more then 1 cb
            auto next_next_result = read_bl_header(data, next_offset);
            bl_type bootloader_type2 = get_bl_type(next_next_result.magic);
            if (bootloader_type2 == CB) {
                // glitch3
                results.cbx = next_result;
                results.cbb = next_next_result;
                next_offset = find_next_bl_offset(results.cbb.offset, results.cbb.size);
            } else if (bootloader_type2 == CD) {
                // split-cb
                results.cbb = next_result;
                results.cd = next_next_result;
                next_offset = find_next_bl_offset(results.cd.offset, results.cd.size);
            } else {
                throw std::runtime_error("Unknown bootloader chain: " + std::to_string(bootloader_type2));
            }
        } else if (bootloader_type == CD) {
            results.cd = next_result;
            // single-cb
        } else {
            throw std::runtime_error("Unknown bootloader chain: " + std::to_string(bootloader_type));
        }
    }

    // Walk the bootloader chain: CB -> CD -> CE
    // Start with CB and follow the chain
    auto ce_result = read_bl_header(data, next_offset);
    if (ce_result) {
        results.ce = *ce_result;
        current_offset = results.ce.offset;
        current_size = results.ce.size;
        next_offset = find_next_bl_offset(current_offset, current_size);
    }
    

    // patchslot 0
    if (header->cf1_offset != 0 && header->cf1_offset != 0xFFFFFFFF) {
        auto cf_result = read_bl_header(data, header->cf1_offset);
        if (cf_result) {
            results.cf0 = *cf_result;
            next_offset = find_next_bl_offset(results.cf0.offset, results.cf0.size);
            auto cg0_result = read_bl_header(data, next_offset);
            if (cg0_result) {
                results.cg0 = *cg0_result;
            }
        }
    }
    
    // patchslot 1
    if (header->patch_slots > 1 && results.cf0) {
        // Second CF is directly after the first CG
        uint32_t cf1_offset = results.cg0.offset + results.cg0.size;
        auto cf1_result = read_bl_header(data, cf1_offset);
        if (cf1_result) {
            results.cf1 = *cf1_result;
            next_offset = find_next_bl_offset(results.cf1.offset, results.cf1.size);
            auto cg1_result = read_bl_header(data, next_offset);
            if (cg1_result) {
                results.cg1 = *cg1_result;
            }
        }
    }
    
    return results;
}

flash_image_t FlashImage::parse(const std::vector<uint8_t>& data) {
    flash_image_t image{};
    
    // Call read to parse NAND header and bootloader chain
    nand_results_t results = read(data);
    
    if (!results.valid) {
        return image; // Return empty/invalid image
    }
    
    // Copy the raw NAND header into the image
    if (data.size() >= sizeof(raw_nand_header_t)) {
        const raw_nand_header_t* header = reinterpret_cast<const raw_nand_header_t*>(data.data());
        image.header = *header;
    }
    
    // Helper lambda to extract bootloader data from the NAND image
    auto extract_bootloader = [&](uint32_t offset, uint32_t size) -> std::optional<std::vector<uint8_t>> {
        if (offset == 0 || offset >= data.size() || size == 0) {
            return std::nullopt;
        }
        
        // Ensure we don't read past the end of the data
        if (offset + size > data.size()) {
            return std::nullopt;
        }
        
        // Extract the bootloader data (including its header)
        std::vector<uint8_t> bl_data(data.begin() + offset, data.begin() + offset + size);
        return bl_data;
    };
    
    // Extract CB (2BL) data
    if (results.cb_or_a.offset != 0 && results.cb_or_a.size != 0) {
        image.cb_or_A = extract_bootloader(results.cb_or_a.offset, results.cb_or_a.size);
    }
    
    // Extract CD (3BL) data
    if (results.cd.offset != 0 && results.cd.size != 0) {
        image.cd = extract_bootloader(results.cd.offset, results.cd.size);
    }
    
    // Extract CE (4BL) data
    if (results.ce && results.ce->offset != 0 && results.ce->size != 0) {
        image.ce = extract_bootloader(results.ce->offset, results.ce->size);
    }
    
    // Extract CF (5BL) and CG (6BL) data into patchslots
    // According to the user: first CF/CG pair goes into patchslot_0
    // If header.patch_slots > 1, there's another CF/CG pair directly after the first, to go into patchslot_1
    
    // Patchslot 0: first CF/CG pair (cf0/cg0)
    if (results.cf0 && results.cf0->offset != 0 && results.cf0->size != 0) {
        patchslot_t slot0;
        slot0.cf = extract_bootloader(results.cf0->offset, results.cf0->size);
        
        // Extract CG0 if available
        if (results.cg0 && results.cg0->offset != 0 && results.cg0->size != 0) {
            slot0.cg = extract_bootloader(results.cg0->offset, results.cg0->size);
        }
        
        image.patchslot_0 = slot0;
    }
    
    // Patchslot 1: second CF/CG pair (cf1/cg1) - only if patch_slots > 1
    if (results.patch_slots > 1 && results.cf1 && results.cf1->offset != 0 && results.cf1->size != 0) {
        patchslot_t slot1;
        slot1.cf = extract_bootloader(results.cf1->offset, results.cf1->size);
        
        // Extract CG1 if available
        if (results.cg1 && results.cg1->offset != 0 && results.cg1->size != 0) {
            slot1.cg = extract_bootloader(results.cg1->offset, results.cg1->size);
        }
        
        image.patchslot_1 = slot1;
    }
    
    // Extract SMC data
    if (results.smc_offset != 0 && results.smc_size != 0 && results.smc_offset != 0xFFFFFFFF) {
        image.smc = extract_bootloader(results.smc_offset, results.smc_size);
    }
    
    // Extract Keyvault data
    if (results.kv_offset != 0 && results.kv_size != 0 && results.kv_offset != 0xFFFFFFFF) {
        image.keyvault = extract_bootloader(results.kv_offset, results.kv_size);
    }
    
    return image;
}