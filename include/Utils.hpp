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

// ============================================================================
// ASCII Art and Presentation
// ============================================================================

namespace Ascii {
    constexpr std::string_view Logo = R"(
                        .o8                    o8o  oooo        .o8    .oooo.
                       "888                    `"'  `888       "888  .dP""Y88b
 .oooooooo oooo    ooo  888oooo.  oooo  oooo  oooo   888   .oooo888        ]8P'
888' `88b   `88b..8P'   d88' `88b `888  `888  `888   888  d88' `888      <88b.
888   888     Y888'     888   888  888   888   888   888  888   888       `88b.
`88bod8P'   .o8"'88b    888   888  888   888   888   888  888   888  o.   .88P
`8oooooo.  o88'   888o  `Y8bod8P'  `V88V"V8P' o888o o888o `Y8bod88P" `8bd88P'
d"     YD
"Y88888P'
)";
} // namespace Ascii

// ============================================================================
// Endian Operations
// ============================================================================

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

// ============================================================================
// CLI Arguments and Types
// ============================================================================

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

// ============================================================================
// File I/O Utilities
// ============================================================================

namespace gxbuild3::utils {

/// @brief Converts a hex string to bytes
/// @param hex_string Hex string (e.g., "AABBCCDD")
/// @return Vector of bytes, empty on error
std::vector<uint8_t> hex_string_to_bytes(const std::string& hex_string);

/// @brief Reads entire file contents into a vector
/// @param path File path
/// @return File data as vector of bytes, or nullopt on error
std::optional<std::vector<uint8_t>> read_file(const fs::path& path);

/// @brief Reads file with maximum length limit
/// @param path File path
/// @param max_length Maximum bytes to read
/// @return File data (up to max_length bytes), or nullopt on error
std::optional<std::vector<uint8_t>> read_file(const fs::path& path, size_t max_length);

/// @brief Writes data to a file
/// @param path File path
/// @param data Data to write
/// @return true on success, false on error
bool write_file(const fs::path& path, const std::vector<uint8_t>& data);

/// @brief Writes data to a file
/// @param path File path
/// @param data Pointer to data
/// @param length Number of bytes to write
/// @return true on success, false on error
bool write_file(const fs::path& path, const uint8_t* data, size_t length);

/// @brief Checks if a directory exists
/// @param path Directory path
/// @return true if directory exists, false otherwise
bool directory_exists(const fs::path& path);

/// @brief Creates a directory (and parent directories if needed)
/// @param path Directory path
/// @return true on success, false on error
bool create_directory(const fs::path& path);

/// @brief Lists all files in a directory
/// @param path Directory path
/// @return Vector of file paths, or nullopt on error
std::optional<std::vector<fs::path>> list_files(const fs::path& path);

/// @brief Gets all files in a directory recursively
/// @param path Root directory path
/// @return Vector of file paths, or nullopt on error
std::optional<std::vector<fs::path>> list_files_recursive(const fs::path& path);

} // namespace gxbuild3::utils
