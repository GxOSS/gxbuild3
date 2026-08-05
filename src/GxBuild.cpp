#include "GxBuild.hpp"
#include "FlashImage.hpp"
#include "ini/IniParser.hpp"
#include "Log.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace GxBuild {

namespace {

std::optional<std::vector<uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;
    std::streamsize size = file.tellg();
    if (size <= 0) return std::nullopt;
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return std::nullopt;
    return buffer;
}

} // namespace

std::expected<std::vector<uint8_t>, std::string>
BuildNandFromMetaIni(std::string_view ini_string,
                     const BuildOptions &options,
                     const std::function<void(const std::string &)> &log_callback) {
    auto parsed_doc = Ini::Parse(ini_string);
    if (!parsed_doc) {
        return std::unexpected(std::string("Failed to parse Genexis metabuild INI content: ") +
                               std::string(Ini::ParseErrorString(parsed_doc.error())));
    }

    const auto& doc = *parsed_doc;

    // 1. Resolve build type and console type
    std::string build_type_str = "retail";
    std::string console_str = "trinity";

    if (const auto* main_sec = doc.get("main")) {
        for (const auto& entry : *main_sec) {
            std::string k = entry.key;
            std::transform(k.begin(), k.end(), k.begin(), ::tolower);
            if (k == "buildtype") {
                build_type_str = entry.value;
            } else if (k == "consoletype") {
                console_str = entry.value;
            }
        }
    }

    BuildType build_type = BuildType::Retail;
    std::string bt_lower = build_type_str;
    std::transform(bt_lower.begin(), bt_lower.end(), bt_lower.begin(), ::tolower);
    if (bt_lower.find("glitch2") != std::string::npos || bt_lower == "gg" || bt_lower == "rgl") {
        build_type = BuildType::Glitch2;
    } else if (bt_lower.find("glitch") != std::string::npos) {
        build_type = BuildType::Glitch;
    } else if (bt_lower.find("jtag") != std::string::npos) {
        build_type = BuildType::Jtag;
    } else if (bt_lower.find("devkit") != std::string::npos || bt_lower == "xdkb") {
        build_type = BuildType::Devkit;
    }

    ConsoleType console_type = ConsoleType::Trinity;
    std::string ct_lower = console_str;
    std::transform(ct_lower.begin(), ct_lower.end(), ct_lower.begin(), ::tolower);
    if (ct_lower.find("xenon") != std::string::npos) console_type = ConsoleType::Xenon;
    else if (ct_lower.find("zephyr") != std::string::npos) console_type = ConsoleType::Zephyr;
    else if (ct_lower.find("falcon") != std::string::npos) console_type = ConsoleType::Falcon;
    else if (ct_lower.find("jasper") != std::string::npos) console_type = ConsoleType::Jasper;
    else if (ct_lower.find("trinity") != std::string::npos) console_type = ConsoleType::Trinity;
    else if (ct_lower.find("corona") != std::string::npos) console_type = ConsoleType::Corona;
    else if (ct_lower.find("winchester") != std::string::npos) console_type = ConsoleType::Winchester;

    flash_image_t flash{};

    // 2. Load source NAND image if provided (to pull Keyvault & SMC if not overridden)
    if (options.source_nand_bytes.has_value() && !options.source_nand_bytes->empty()) {
        try {
            flash = FlashImage::parse(*options.source_nand_bytes);
        } catch (...) {}
    } else if (options.source_nand_path.has_value() && !options.source_nand_path->empty()) {
        if (auto bytes = ReadFileBytes(*options.source_nand_path)) {
            try {
                flash = FlashImage::parse(std::move(*bytes));
            } catch (...) {}
        }
    }

    // 3. Apply custom Keyvault override
    if (options.custom_kv_path.has_value() && !options.custom_kv_path->empty()) {
        if (auto kv_data = ReadFileBytes(*options.custom_kv_path)) {
            flash.keyvault = std::move(*kv_data);
        }
    }

    // 4. Apply custom SMC override
    if (options.custom_smc_path.has_value() && !options.custom_smc_path->empty()) {
        if (auto smc_data = ReadFileBytes(*options.custom_smc_path)) {
            flash.smc = std::move(*smc_data);
        }
    }

    // 5. Apply custom SMC config override
    if (options.custom_smc_config_path.has_value() && !options.custom_smc_config_path->empty()) {
        if (auto cfg_data = ReadFileBytes(*options.custom_smc_config_path)) {
            flash.xconfig = std::move(*cfg_data);
        }
    }

    // 6. Build NAND image
    std::span<const uint8_t> cpu_key_span;
    if (options.cpu_key.has_value()) {
        cpu_key_span = *options.cpu_key;
    }

    if (log_callback) {
        log_callback("Building NAND image with gxbuild3_lib...\n");
    }

    auto result = build(flash, build_type, console_type, false, cpu_key_span);
    if (!result.has_value()) {
        return std::unexpected("FlashImage::build failed to assemble target NAND image.");
    }

    return std::move(*result);
}

namespace Patcher {

bool ApplyRawPatch(std::span<uint8_t> targetData, uint32_t offset,
                   std::span<const uint8_t> payload) {
    return Source::ApplyPatch(targetData.data(), static_cast<uint32_t>(targetData.size()), offset,
                              payload.data(), static_cast<uint32_t>(payload.size()));
}

bool ApplyXePatchWords(std::span<uint8_t> targetData, uint32_t address, uint32_t wordCount,
                       const uint32_t *patchWords) {
    return XePatch::ApplyPatch(targetData.data(), static_cast<uint32_t>(targetData.size()),
                               address, wordCount, patchWords);
}

bool ApplyXePatchEntry(std::span<uint8_t> targetData, const XePatchEntry &entry) {
    return XePatch::ApplyPatchEntry(targetData.data(), static_cast<uint32_t>(targetData.size()), entry);
}

bool ApplyXePatchSection(std::span<uint8_t> targetData, const XePatchSection &section) {
    return XePatch::ApplyPatchSection(targetData.data(), static_cast<uint32_t>(targetData.size()), section);
}

uint32_t ApplySignaturePatch(std::span<uint8_t> targetData,
                             const std::string &searchPatternStr,
                             const std::string &replacePatternStr) {
    return Signature::ApplyPatch(targetData.data(), static_cast<uint32_t>(targetData.size()),
                                  searchPatternStr, replacePatternStr);
}

bool ParsePatchFile(const std::string &filePath, std::vector<XePatchSection> &outSections) {
    return BinaryParser::ParsePatchFile(filePath, outSections);
}

bool ParsePatchSet(const std::string &filePath, BuildType buildType, ParsedPatchSet &outPatchSet) {
    return BinaryParser::ParsePatchSet(filePath, buildType, outPatchSet);
}

std::vector<uint8_t> SerializePatchSet(const ParsedPatchSet &patchSet) {
    return BinaryParser::SerializePatchSet(patchSet);
}

} // namespace Patcher

} // namespace GxBuild
