#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#include <cstdlib>
#include <intrin.h>
#endif

namespace fs = std::filesystem;

[[nodiscard]] inline uint32_t swap32(uint32_t x) noexcept {
    return (x & 0xFF000000U) >> 24 | (x & 0x00FF0000U) >> 8 | (x & 0x0000FF00U) << 8 |
           (x & 0x000000FFU) << 24;
}

#if defined(_MSC_VER)
[[nodiscard]] inline uint16_t bswap16(uint16_t x) noexcept {
    return _byteswap_ushort(x);
}
[[nodiscard]] inline uint32_t bswap32(uint32_t x) noexcept {
    return _byteswap_ulong(x);
}
[[nodiscard]] inline uint64_t bswap64(uint64_t x) noexcept {
    return _byteswap_uint64(x);
}
#else
[[nodiscard]] inline uint16_t bswap16(uint16_t x) noexcept {
    return __builtin_bswap16(x);
}
[[nodiscard]] inline uint32_t bswap32(uint32_t x) noexcept {
    return __builtin_bswap32(x);
}
[[nodiscard]] inline uint64_t bswap64(uint64_t x) noexcept {
    return __builtin_bswap64(x);
}
#endif

enum class BuildType {
    Retail,
    Jtag,
    Glitch,
    Glitch2,
    Glitch2m,
    Glitch3,
    Devkit,
};

enum class ConsoleType {
    Xenon,
    Zephyr,
    Falcon,
    Jasper,
    Jasper256,
    Jasper512,
    JasperBB,
    JasperBigFFS,
    Trinity,
    TrinityBB,
    TrinityBigFFS,
    Corona,
    Corona4G,
    Winchester,
    Winchester4G,
};

inline const std::map<std::string, BuildType> kBuildTypeMap = {
    {"retail", BuildType::Retail},     {"jtag", BuildType::Jtag},
    {"glitch", BuildType::Glitch},     {"glitch2", BuildType::Glitch2},
    {"glitch2m", BuildType::Glitch2m}, {"glitch3", BuildType::Glitch3},
    {"devkit", BuildType::Devkit},
};

inline const std::map<std::string, ConsoleType> kConsoleTypeMap = {
    {"xenon", ConsoleType::Xenon},
    {"zephyr", ConsoleType::Zephyr},
    {"falcon", ConsoleType::Falcon},
    {"jasper", ConsoleType::Jasper},
    {"jasper256", ConsoleType::Jasper256},
    {"jasper512", ConsoleType::Jasper512},
    {"jasperbb", ConsoleType::JasperBB},
    {"jasperbigffs", ConsoleType::JasperBigFFS},
    {"trinity", ConsoleType::Trinity},
    {"trinitybb", ConsoleType::TrinityBB},
    {"trinitybigffs", ConsoleType::TrinityBigFFS},
    {"corona", ConsoleType::Corona},
    {"corona4g", ConsoleType::Corona4G},
    {"winchester", ConsoleType::Winchester},
    {"winchester4g", ConsoleType::Winchester4G},
};

struct GxArgs {
    std::string mode{"build"};
    std::optional<BuildType> build_type;
    std::optional<ConsoleType> console;
    std::optional<std::string> cpu_key;
    std::optional<std::string> bl_key;
    std::optional<fs::path> data_dir;
    std::optional<fs::path> common_dir;
    std::optional<fs::path> fw_dir;
    std::optional<fs::path> sha_file;
    std::optional<fs::path> source_nand;
    std::optional<fs::path> stfs_package;
    std::optional<fs::path> ecc;
    std::optional<fs::path> xboxupd;
    std::optional<fs::path> output_dir;
    std::optional<fs::path> output;
    std::optional<std::string> ini_ext;
    std::optional<std::string> bl_ext;
    std::optional<std::string> preset;
    std::optional<std::string> cmd;
    std::optional<std::string> format;
    std::vector<std::pair<std::string, std::string>> options;
    std::vector<std::string> addons;
    std::vector<std::string> raw_patches;
    bool xsb{false};
    bool full_image{false};
    bool bigblock{false};
    bool extract_all{false};
    bool stfs_split_xboxupd{false};
    bool license{false};
};

namespace Utils {

    std::vector<uint8_t> hex_string_to_bytes(const std::string& hex_string);

    std::optional<std::vector<uint8_t>> read_file(const fs::path& path);

    std::optional<std::vector<uint8_t>> read_file(const fs::path& path, size_t max_length);

    bool write_file(const fs::path& path, const std::vector<uint8_t>& data);

    bool write_file(const fs::path& path, const uint8_t* data, size_t length);

    bool directory_exists(const fs::path& path);

    bool create_directory(const fs::path& path);

    std::optional<std::vector<fs::path>> list_files(const fs::path& path);

    std::optional<std::vector<fs::path>> list_files_recursive(const fs::path& path);

} // namespace Utils
