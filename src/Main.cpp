#include "FlashImage.hpp"
#include "Ascii.hpp"
#include "Log.hpp"
#include "Utils.hpp"
#include "bootloaders/2bl.hpp"
#include "bootloaders/Keyvault.hpp"
#include "ini/IniParser.hpp"
#include "stfs/StfsContainer.hpp"

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#if defined(_MSC_VER)
constexpr const char* compiler = "MSVC";
#elif defined(__clang__)
constexpr const char* compiler = "Clang";
#elif defined(__GNUC__)
constexpr const char* compiler = "GCC";
#else
constexpr const char* compiler = "Unknown";
#endif

static std::optional<BuildType> ParseBuildType(const std::string& s) {
    auto it = kBuildTypeMap.find(s);
    if (it == kBuildTypeMap.end())
        return std::nullopt;
    return it->second;
}

static std::optional<ConsoleType> ParseConsoleType(const std::string& s) {
    auto it = kConsoleTypeMap.find(s);
    if (it == kConsoleTypeMap.end())
        return std::nullopt;
    return it->second;
}

static std::optional<std::vector<uint8_t>> HexToBytes(const std::string& hex) {
    if (hex.size() % 2 != 0)
        return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto hi = hex[i];
        auto lo = hex[i + 1];
        auto from_hex = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        int h = from_hex(hi);
        int l = from_hex(lo);
        if (h < 0 || l < 0)
            return std::nullopt;
        out.push_back(static_cast<uint8_t>((h << 4) | l));
    }
    return out;
}

static std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static std::span<const std::byte> AsByteSpan(const std::vector<uint8_t>& data) {
    return {reinterpret_cast<const std::byte*>(data.data()), data.size()};
}

static bool WriteFile(const std::filesystem::path& path, std::span<const std::byte> data) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

int main(int argc, char** argv) {
    Log::Init();

    GxArgs args;

    CLI::App app{"GxBuild3"};
    app.set_help_flag("-?,--help", "Print this help message and exit");

    std::string build_type_str;
    std::string console_str;

    app.add_option("-t,--type", build_type_str)
        ->description("Build type (retail, jtag, glitch, glitch2, glitch2m, glitch3, devkit)");
    app.add_option("-c,--console", console_str)->description("Console revision");

    app.add_option("-p,--cpukey", args.cpu_key)->description("CPU key (hex)");
    app.add_option("-b,--1blkey", args.bl_key)->description("1BL key (hex)");
    app.add_option("-d,--build", args.data_dir)->description("Build data directory");
    app.add_option("-m,--common", args.common_dir)->description("Common files directory");
    app.add_option("-f,--data", args.fw_dir)->description("Firmware directory");
    app.add_option("-s,--sha", args.sha_file)->description("SHA file path");
    app.add_option("-a,--addon", args.addons)->description("Addon packages");
    app.add_option("-i,--fwext", args.ini_ext)->description("Firmware extension override");
    app.add_option("-r,--iniext", args.bl_ext)->description("INI extension override");
    app.add_option("-8,--raw", args.raw_patches)->description("Raw patch entries");
    app.add_option("-l,--image", args.source_nand)->description("Source NAND image");
    app.add_option("--ecc", args.ecc)->description("ECC file");
    app.add_option("-u,--update", args.xboxupd)->description("xboxupd.bin path");
    app.add_option("-e,--preset", args.preset)->description("Preset name");
    app.add_option("-n,--cmd", args.cmd)->description("Command override");
    app.add_option("--format", args.format)->description("Output format");
    app.add_option("-g,--output-dir", args.output_dir)->description("Output directory");
    app.add_option("output", args.output)->description("Output file");

    app.add_flag("-x,--xsb", args.xsb)->description("Enable XSB mode");
    app.add_flag("--fullimage", args.full_image)->description("Build full image");
    app.add_flag("--bigblock", args.bigblock)->description("Use big block layout");
    app.add_flag("--license", args.license)->description("Show license information");

    app.add_option("-o,--options", "Semicolon-separated key=value option overrides")
        ->each([&args](const std::string& val) {
            std::stringstream ss(val);
            std::string part;
            while (std::getline(ss, part, ';')) {
                if (part.empty())
                    continue;
                if (auto pos = part.find('='); pos != std::string::npos)
                    args.options.emplace_back(part.substr(0, pos), part.substr(pos + 1));
                else
                    args.options.emplace_back(part, "true");
            }
        });

    auto* build_sub = app.add_subcommand("build", "Build a NAND image (default)");
    auto* extract_sub = app.add_subcommand("extract", "Extract files from a NAND image");
    auto* stfs_sub = app.add_subcommand("stfs", "Extract files from a PIRS/STFS package");

    build_sub->callback([&args]() { args.mode = "build"; });
    extract_sub->callback([&args]() { args.mode = "extract"; });
    extract_sub->add_flag("--all", args.extract_all, "Extract all files");
    stfs_sub->callback([&args]() { args.mode = "stfs"; });
    stfs_sub->add_option("package", args.stfs_package, "PIRS/STFS package")->required();
    stfs_sub->add_option("-g,--output-dir", args.output_dir, "Output directory");
    stfs_sub->add_flag("--xboxupd", args.stfs_split_xboxupd,
                       "Extract xboxupd.bin and split it into cf.bin/cg.bin");

    app.require_subcommand(0, 1);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (args.license) {
        std::cout << R"(Copyright (c) 2026, GxOSS

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.)"
                  << std::endl;
        return 0;
    }

    std::cout << Ascii::Logo;
    Log::Info("gxbuild3 starting... (Built on: {} {} with {})", __DATE__, __TIME__, compiler);

    if (!build_type_str.empty()) {
        auto bt = ParseBuildType(build_type_str);
        if (!bt) {
            Log::Error("Unknown build type: '{}'", build_type_str);
            return 1;
        }
        args.build_type = bt;
    }

    if (!console_str.empty()) {
        auto ct = ParseConsoleType(console_str);
        if (!ct) {
            Log::Error("Unknown console type: '{}'", console_str);
            return 1;
        }
        args.console = ct;
    }

    if (!app.got_subcommand(build_sub) && !app.got_subcommand(extract_sub) &&
        !app.got_subcommand(stfs_sub))
        Log::Warn("No subcommand specified, defaulting to 'build'.");

    if (args.mode == "stfs") {
        const auto package_path = *args.stfs_package;
        const auto output_dir = args.output_dir.value_or(std::filesystem::current_path());

        auto package_data = ReadFile(package_path);
        if (!package_data) {
            Log::Error("Failed to read STFS package: {}", package_path.string());
            return 1;
        }

        try {
            std::filesystem::create_directories(output_dir);

            if (args.stfs_split_xboxupd) {
                const auto parts = Stfs::extractXboxupdRaw(AsByteSpan(*package_data));
                if (!WriteFile(output_dir / "cf.bin", parts.cf_raw) ||
                    !WriteFile(output_dir / "cg.bin", parts.cg_raw)) {
                    Log::Error("Failed to write xboxupd split output to '{}'", output_dir.string());
                    return 1;
                }

                Log::Info("Extracted xboxupd CF/CG to '{}'", output_dir.string());
            } else {
                const Stfs::StfsContainer container{AsByteSpan(*package_data)};
                container.extractAll(output_dir);
                Log::Info("Extracted STFS package to '{}'", output_dir.string());
            }
        } catch (const std::exception& ex) {
            Log::Error("STFS extraction failed: {}", ex.what());
            return 1;
        }

        return 0;
    }

    std::vector<uint8_t> cpu_key_bytes;
    if (args.cpu_key) {
        auto parsed = HexToBytes(*args.cpu_key);
        if (!parsed || parsed->size() != 16) {
            Log::Error("Invalid CPU key: must be a 32-character hex string");
            return 1;
        }
        cpu_key_bytes = std::move(*parsed);
        if (!cpukey_valid(cpu_key_bytes)) {
            Log::Error("CPU key failed ECC/hamming validation");
            return 1;
        }
        Log::Info("CPU key accepted");
    }

    std::vector<uint8_t> bl_key_bytes;
    if (args.bl_key) {
        auto parsed = HexToBytes(*args.bl_key);
        if (!parsed || parsed->size() != 16) {
            Log::Error("Invalid 1BL key: must be a 32-character hex string");
            return 1;
        }
        bl_key_bytes = std::move(*parsed);
        Log::Info("1BL key accepted");
    }

    OptionsArgs options{};
    {
        std::filesystem::path opts_path =
            args.data_dir ? (*args.data_dir / "options.ini") : std::filesystem::path("options.ini");

        auto opts_res = Ini::ParseOptionsIniFile(opts_path);
        if (!opts_res) {
            std::visit(
                [](auto&& err) {
                    using T = std::decay_t<decltype(err)>;
                    if constexpr (std::is_same_v<T, Ini::ParseError>) {
                        if (err != Ini::ParseError::FileNotFound)
                            Log::Warn("options.ini: {}", Ini::ParseErrorString(err));
                    } else {
                        Log::Error("options.ini: {}", Ini::OptionsErrorString(err));
                    }
                },
                opts_res.error());
        } else {
            options = std::move(*opts_res);
            Log::Info("Loaded options.ini");
        }
    }

    for (const auto& [key, value] : args.options) {
        Ini::ApplyOption(options, key, value);
    }

    if (cpu_key_bytes.empty() && options.cpukey) {
        auto parsed = HexToBytes(*options.cpukey);
        if (parsed && parsed->size() == 16 && cpukey_valid(*parsed)) {
            cpu_key_bytes = std::move(*parsed);
            Log::Info("CPU key loaded from merged options");
        }
    }
    if (bl_key_bytes.empty() && options.key_1bl) {
        auto parsed = HexToBytes(*options.key_1bl);
        if (parsed && parsed->size() == 16) {
            bl_key_bytes = std::move(*parsed);
            Log::Info("1BL key loaded from merged options");
        }
    }

    if (args.mode == "extract") {
        if (!args.source_nand) {
            Log::Error("Extraction requires a source NAND image (-l,--image)");
            return 1;
        }

        auto nand_data = ReadFile(*args.source_nand);
        if (!nand_data) {
            Log::Error("Failed to read source NAND image: {}", args.source_nand->string());
            return 1;
        }

        Log::Info("Parsing NAND image...");
        flash_image_t flash;
        try {
            flash = FlashImage::parse(std::move(*nand_data));
        } catch (const std::exception& ex) {
            Log::Error("Failed to parse NAND: {}", ex.what());
            return 1;
        }

        const auto output_dir = args.output_dir.value_or(std::filesystem::current_path());

        Log::Info("Extracting components to '{}'...", output_dir.string());
        if (extract_all(flash, output_dir, cpu_key_bytes, bl_key_bytes)) {
            Log::Info("Extraction completed successfully.");
            return 0;
        } else {
            Log::Error("Extraction failed.");
            return 1;
        }
    }

    std::optional<Ini::Document> build_doc;
    {
        std::filesystem::path ini_dir = args.data_dir.value_or(std::filesystem::current_path());
        std::filesystem::path ini_path = ini_dir / (build_type_str + ".ini");

        auto res = Ini::ParseFile(ini_path);
        if (!res) {
            Log::Error("Failed to load build INI '{}': {}", ini_path.string(),
                       Ini::ParseErrorString(res.error()));
            return 1;
        }
        
        build_doc = std::move(*res);
        Log::Info("Loaded build INI: {}", ini_path.string());
    }

    if (!bl_key_bytes.empty() && build_doc) {
        std::filesystem::path fw_dir =
            args.fw_dir.value_or(args.data_dir.value_or(std::filesystem::current_path()));

        std::string section_name;
        if (args.console) {
            static const std::map<ConsoleType, std::string> kConsoleSectionSuffix = {
                {ConsoleType::Xenon, "xenon"},
                {ConsoleType::Zephyr, "zephyr"},
                {ConsoleType::Falcon, "falcon"},
                {ConsoleType::Jasper, "jasper"},
                {ConsoleType::JasperBB, "jasperbb"},
                {ConsoleType::JasperBigFFS, "jasperbigffs"},
                {ConsoleType::Trinity, "trinity"},
                {ConsoleType::TrinityBB, "trinitybb"},
                {ConsoleType::TrinityBigFFS, "trinitybigffs"},
                {ConsoleType::Corona, "corona"},
                {ConsoleType::Corona4G, "corona4g"},
                {ConsoleType::Winchester, "winchester"},
                {ConsoleType::Winchester4G, "winchester4g"},
            };
            auto it = kConsoleSectionSuffix.find(*args.console);
            if (it != kConsoleSectionSuffix.end())
                section_name = it->second + "bl";
        }


        std::filesystem::path patches_dir =  args.data_dir / "bin";
        std::filesystem::path patch;
        std::string g1_model = switch (args.console->string()) {
            case "xenon" || "elpis" || "zephyr" || "falcon" || "opus" || "jasper" || "jasperbb" || "jasperbigffs" || "tonasket":
                return "phat";
            case "trinity" || "trinitybb" || "trinitybigffs" || "corona" || "corona4g" || "winchester" || "winchester4g":
                return "trinity";
            default:
                return args.console->string();
        };

        std::string patch_mobo = args.console->string() + ("_" + args.bl_ext->string()).value_or("");

        switch (build_type_str) {
            case "retail" || "devkit":
                break;
            case "glitch":
                patch = patches_dir / "patches_" + g1_model + ".bin";
                break;
            case "glitch2":
                patch = patches_dir / "patches_g2" + patch_mobo + ".bin";
                break;
            case "glitch2m" || "devgl":
                patch = patches_dir / "patches_g2m" + patch_mobo + ".bin";
                break;
            case "glitch3":
                patch = patches_dir / "patches_g3" + patch_mobo + ".bin";
                break;
            case "jtag":
                patch = patches_dir / "patches_" + patch_mobo + ".bin";
                break;
            default:
                Log::Error("Unknown build type: {}", build_type_str);
                return 1;
                break;
        }

        if (!patch.empty()) {
            auto patch_data = ReadFile(patch);
            if (!patch_data) {
                Log::Error("Failed to read patch file: {}", patch.string());
                return 1;
            }
        }

        flash_image_t new_nand{};

        if (!args.source_nand.empty()) {
            std::filesystem::path source_nand_path = args.fw_dir.value_or(args.data_dir.value_or(std::filesystem::current_path()));
            source_nand_path /= args.source_nand;
            auto source_nand_data = ReadFile(source_nand_path);
            if (!source_nand_data) {
                Log::Error("Failed to read source NAND file: {}", source_nand_path.string());
                return 1;
            }
            flash_image_t donor_nand = FlashImage::parse(*source_nand_data);
            if (!donor_nand.valid) {
                Log::Error("Failed to parse source NAND file: {}", source_nand_path.string());
                return 1;
            }
            new_nand.keyvault = donor_nand.keyvault;
            new_nand.smc_config = donor_nand.smc_config;
            new_nand.flashfs_files = donor_nand.flashfs_files;
            
        }

        const Ini::Section* main_sec = build_doc->get(section_name);
        const Ini::Section* sec_sec = build_doc->get("security");
        const Ini::Section* flashfs_sec = build_doc->get("flashfs");
        const Ini::Section* payloads_sec = build_doc->get("payloads");

        if (sec_sec) {
        }


        if (main_sec) {
            for (const auto& entry : *main_sec) {
                std::string key_lower = entry.key;
                std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);

                bool is_cb = key_lower.starts_with("cb") || key_lower.starts_with("sb");
                if (!is_cb)
                    continue;

                std::filesystem::path cb_path = fw_dir / entry.key;
                auto cb_data = ReadFile(cb_path);
                if (!cb_data) {
                    Log::Warn("CB file not found: {}", cb_path.string());
                    continue;
                }

                try {
                    auto cb = BootloaderCb::parse(*cb_data);
                    cb.decrypt(bl_key_bytes.data());
                    if (cb.is_decrypted()) {
                        Log::Info("CB '{}' decrypted successfully (v{})", entry.key,
                                  cb.header.header.version);
                    } else {
                        Log::Warn("CB '{}' decrypt completed but verification failed", entry.key);
                    }
                } catch (const std::exception& ex) {
                    Log::Error("CB '{}' parse/decrypt failed: {}", entry.key, ex.what());
                }
                break;
            }
        } else if (!section_name.empty()) {
            Log::Warn("Section '{}' not found in build INI", section_name);
        }
    }

    if (args.)

    return 0;
    }
}
