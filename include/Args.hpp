#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

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
    std::optional<std::filesystem::path> data_dir;
    std::optional<std::filesystem::path> common_dir;
    std::optional<std::filesystem::path> fw_dir;
    std::optional<std::filesystem::path> sha_file;
    std::optional<std::filesystem::path> source_nand;
    std::optional<std::filesystem::path> ecc;
    std::optional<std::filesystem::path> xboxupd;
    std::optional<std::filesystem::path> output_dir;
    std::optional<std::filesystem::path> output;
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
};