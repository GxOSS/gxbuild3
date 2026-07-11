#include "FlashImage.hpp"
#include "Ascii.hpp"
#include "Log.hpp"
#include "Options.hpp"
#include "Utils.hpp"
#include "bootloaders/2bl.hpp"
#include "bootloaders/Keyvault.hpp"
#include "patchers/BinaryParser.hpp"
#include "patchers/Patcher.hpp"
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

    Options::Init(options);

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
        const auto& opts = Options::Get();
        const std::filesystem::path data_dir =
            args.data_dir.value_or(std::filesystem::current_path());
        const std::optional<std::filesystem::path> common_dir = args.common_dir;
        std::filesystem::path fw_dir = args.fw_dir.value_or(data_dir);
        std::optional<Stfs::ExtractedFiles> stfs_files;

        auto normalize_file_key = [](std::string key) {
            std::replace(key.begin(), key.end(), '\\', '/');
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            return key;
        };

        auto entry_to_lookup_path = [](std::string_view entry_name) {
            std::string normalized{entry_name};
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return std::filesystem::path(normalized);
        };

        using resolved_file_t = std::pair<std::filesystem::path, std::vector<uint8_t>>;

        auto load_from_root = [&](const std::filesystem::path& root, std::string_view entry_name)
            -> std::optional<resolved_file_t> {
            const auto candidate = root / entry_to_lookup_path(entry_name);
            auto file_data = ReadFile(candidate);
            if (!file_data) {
                return std::nullopt;
            }
            return resolved_file_t{candidate, std::move(*file_data)};
        };

        auto load_from_common_dir = [&](std::string_view entry_name)
            -> std::optional<resolved_file_t> {
            if (!common_dir) {
                return std::nullopt;
            }
            return load_from_root(*common_dir, entry_name);
        };

        auto load_main_section_file = [&](std::string_view entry_name)
            -> std::optional<resolved_file_t> {
            if (auto loaded = load_from_root(fw_dir, entry_name)) {
                return loaded;
            }
            if (auto loaded = load_from_root(data_dir, entry_name)) {
                return loaded;
            }
            return load_from_common_dir(entry_name);
        };

        auto load_payload_file = [&](std::string_view filename) -> std::optional<resolved_file_t> {
            const auto& root = fw_dir;

            if (auto loaded = load_from_root(root, filename)) {
                return loaded;
            }
            if (auto loaded = load_from_root(root / "payloads", filename)) {
                return loaded;
            }
            return std::nullopt;
        };

        auto load_xell_file = [&]() -> std::optional<resolved_file_t> {
            static constexpr std::string_view kGlitchXellName = "xell-gggggg.bin";
            static constexpr std::string_view kJtagXellName = "xell-2f.bin";

            if (args.build_type && *args.build_type == BuildType::Jtag) {
                return load_payload_file(kJtagXellName);
            }

            return load_payload_file(kGlitchXellName);
        };

        const bool is_xell_build_type = args.build_type &&
                                        (*args.build_type == BuildType::Jtag ||
                                         *args.build_type == BuildType::Glitch ||
                                         *args.build_type == BuildType::Glitch2 ||
                                         *args.build_type == BuildType::Glitch3);

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



        {
            std::optional<std::filesystem::path> stfs_path;

            if (std::filesystem::exists(data_dir) && std::filesystem::is_directory(data_dir)) {
                for (const auto& dir_entry : std::filesystem::directory_iterator(data_dir)) {
                    if (!dir_entry.is_regular_file()) {
                        continue;
                    }

                    const auto candidate = dir_entry.path();
                    const std::string filename = candidate.filename().string();
                    std::string filename_lower = filename;
                    std::transform(filename_lower.begin(), filename_lower.end(),
                                   filename_lower.begin(), ::tolower);

                    if (!candidate.has_extension() && filename_lower.starts_with("su")) {
                        if (stfs_path) {
                            Log::Warn("Multiple STFS candidates found in '{}', using '{}'",
                                      data_dir.string(), stfs_path->filename().string());
                            break;
                        }
                        stfs_path = candidate;
                    }
                }
            }

            if (stfs_path) {
                auto stfs_data = ReadFile(*stfs_path);
                if (!stfs_data) {
                    Log::Error("Failed to read STFS package: {}", stfs_path->string());
                    return 1;
                }

                try {
                    const Stfs::StfsContainer container{AsByteSpan(*stfs_data)};
                    stfs_files = container.extractToMemory();
                    Log::Info("Loaded STFS package '{}' with {} extracted files",
                              stfs_path->filename().string(), stfs_files->size());
                } catch (const std::exception& ex) {
                    Log::Error("Failed to parse STFS package '{}': {}", stfs_path->string(),
                               ex.what());
                    return 1;
                }
            }
        }

        // build patch file name and grab if present
        std::filesystem::path patches_dir = data_dir / "bin";
        std::filesystem::path patch;
        const std::string console_name = [&]() -> std::string {
            if (!args.console) {
                return {};
            }

            switch (*args.console) {
                case ConsoleType::Xenon: return "xenon";
                case ConsoleType::Zephyr: return "zephyr";
                case ConsoleType::Falcon: return "falcon";
                case ConsoleType::Jasper: return "jasper";
                case ConsoleType::Jasper256: return "jasper256";
                case ConsoleType::Jasper512: return "jasper512";
                case ConsoleType::JasperBB: return "jasperbb";
                case ConsoleType::JasperBigFFS: return "jasperbigffs";
                case ConsoleType::Trinity: return "trinity";
                case ConsoleType::TrinityBB: return "trinitybb";
                case ConsoleType::TrinityBigFFS: return "trinitybigffs";
                case ConsoleType::Corona: return "corona";
                case ConsoleType::Corona4G: return "corona4g";
                case ConsoleType::Winchester: return "winchester";
                case ConsoleType::Winchester4G: return "winchester4g";
            }

            return {};
        }();
        std::string g1_model = console_name;
        if (console_name == "xenon" || console_name == "elpis" || console_name == "zephyr" ||
            console_name == "falcon" || console_name == "opus" || console_name == "jasper" ||
            console_name == "jasperbb" || console_name == "jasperbigffs" ||
            console_name == "tonasket") {
            g1_model = "phat";
        } else if (console_name == "trinity" || console_name == "trinitybb" ||
                   console_name == "trinitybigffs" || console_name == "corona" ||
                   console_name == "corona4g" || console_name == "winchester" ||
                   console_name == "winchester4g") {
            g1_model = "trinity";
        }

        const std::string patch_mobo =
            console_name + (args.bl_ext ? "_" + *args.bl_ext : std::string{});

        if (build_type_str == "retail" || build_type_str == "devkit") {
            // Retail/devkit builds do not use patch files.
        } else if (build_type_str == "glitch") {
            patch = patches_dir / ("patches_" + g1_model + ".bin");
        } else if (build_type_str == "glitch2") {
            patch = patches_dir / ("patches_g2" + patch_mobo + ".bin");
        } else if (build_type_str == "glitch2m" || build_type_str == "devgl") {
            patch = patches_dir / ("patches_g2m" + patch_mobo + ".bin");
        } else if (build_type_str == "glitch3") {
            patch = patches_dir / ("patches_g3" + patch_mobo + ".bin");
        } else if (build_type_str == "jtag") {
            patch = patches_dir / ("patches_" + patch_mobo + ".bin");
        } else {
            Log::Error("Unknown build type: {}", build_type_str);
            return 1;
        }

        std::optional<ParsedPatchSet> parsed_patchset;

        if (!patch.empty()) {
            auto patch_data = ReadFile(patch);
            if (!patch_data) {
                Log::Error("Failed to read patch file: {}", patch.string());
                return 1;
            }

            auto load_addon_file = [&](std::string_view addon_name)
                -> std::optional<resolved_file_t> {
                const auto try_load = [&](std::string_view candidate_name)
                    -> std::optional<resolved_file_t> {
                    if (auto loaded = load_from_root(data_dir, candidate_name)) {
                        return loaded;
                    }
                    if (auto loaded = load_from_root(data_dir / "payloads", candidate_name)) {
                        return loaded;
                    }
                    return std::nullopt;
                };

                if (auto loaded = try_load(addon_name)) {
                    return loaded;
                }

                const std::filesystem::path addon_path{std::string{addon_name}};
                if (!addon_path.has_extension()) {
                    const std::string addon_with_bin = std::string{addon_name} + ".bin";
                    return try_load(addon_with_bin);
                }

                return std::nullopt;
            };

            parsed_patchset.emplace();
            if (!args.build_type ||
                !BinaryParser::ParsePatchSet(patch.string(), *args.build_type, *parsed_patchset)) {
                Log::Error("Failed to parse patchset '{}'", patch.string());
                return 1;
            }

            auto raw_tail_it = std::find_if(
                parsed_patchset->sections.begin(), parsed_patchset->sections.end(),
                [](const ParsedPatchSection& section) {
                    return section.target == PatchSectionTarget::Khv ||
                           section.target == PatchSectionTarget::JtagSection4;
                });
            if (raw_tail_it == parsed_patchset->sections.end()) {
                Log::Error("Parsed patchset '{}' is missing a raw insert section", patch.string());
                return 1;
            }

            for (const auto& addon_name : args.addons) {
                auto addon_file = load_addon_file(addon_name);
                if (!addon_file) {
                    Log::Error("Failed to resolve addon patch '{}'", addon_name);
                    return 1;
                }

                raw_tail_it->raw_data.insert(raw_tail_it->raw_data.end(),
                                             addon_file->second.begin(), addon_file->second.end());
                Log::Info("Appended addon '{}' ({} bytes) to '{}'", addon_file->first.string(),
                          addon_file->second.size(), raw_tail_it->identifier);
            }

            Log::Info("Parsed patchset '{}' as {} with {} sections", patch.filename().string(),
                      parsed_patchset->kind == PatchSetKind::Jtag ? "JTAG" : "Glitch",
                      parsed_patchset->sections.size());
        }

        flash_image_t new_nand{};
        std::optional<flash_image_t> donor_nand;

        // open nand, grab keyvault
        if (args.source_nand) {
            std::filesystem::path source_nand_path =
                args.fw_dir.value_or(args.data_dir.value_or(std::filesystem::current_path()));
            source_nand_path /= *args.source_nand;
            auto source_nand_data = ReadFile(source_nand_path);
            if (!source_nand_data) {
                Log::Error("Failed to read source NAND file: {}", source_nand_path.string());
                return 1;
            }
            donor_nand = FlashImage::parse(*source_nand_data);
            if (!donor_nand->nand_results || !donor_nand->nand_results->valid) {
                Log::Error("Failed to parse source NAND file: {}", source_nand_path.string());
                return 1;
            }
            new_nand.keyvault = donor_nand->keyvault;
            new_nand.smc = donor_nand->smc;
        }

        if (!new_nand.smc) {
            if (auto smc_file = load_from_root(fw_dir, "smc.bin")) {
                new_nand.smc = std::move(smc_file->second);
                Log::Info("Loaded SMC from '{}'", smc_file->first.string());
            }
        }

        if (!new_nand.keyvault) {
            if (auto kv_file = load_from_root(fw_dir, "kv.bin")) {
                new_nand.keyvault = std::move(kv_file->second);
                Log::Info("Loaded keyvault from '{}'", kv_file->first.string());
            }
        }

        if (is_xell_build_type) {
            if (auto xell_file = load_xell_file()) {
                if (!new_nand.xellblock) {
                    new_nand.xellblock.emplace();
                }
                new_nand.xellblock->xell_main = std::move(xell_file->second);
                Log::Info("Loaded XeLL from '{}'", xell_file->first.string());
            }
        }

        if (args.build_type && *args.build_type == BuildType::Jtag) {
            auto ensure_payloads = [&new_nand]() -> payloads_t& {
                if (!new_nand.payloads) {
                    new_nand.payloads.emplace();
                }
                return *new_nand.payloads;
            };

            if (auto payload_file = load_payload_file("payload.bin")) {
                ensure_payloads().jtag_payload = std::move(payload_file->second);
                Log::Info("Loaded JTAG payload from '{}'", payload_file->first.string());
            }

            if (auto freeboot_file = load_payload_file("freeboot.bin")) {
                ensure_payloads().jtag_rebooter = std::move(freeboot_file->second);
                Log::Info("Loaded JTAG rebooter from '{}'", freeboot_file->first.string());
            }

            if (auto fuses_file = load_payload_file("fuses.bin")) {
                ensure_payloads().vfuses = std::move(fuses_file->second);
                Log::Info("Loaded JTAG fuses from '{}'", fuses_file->first.string());
            }
        }

        
        const Ini::Section* sec_sec = build_doc->get("security");
        const Ini::Section* flashfs_sec = build_doc->get("flashfs");
        const Ini::Section* payloads_sec = build_doc->get("payloads");
        (void)payloads_sec;

        auto stfs_file_to_u8 = [&stfs_files, &normalize_file_key](std::string_view name)
            -> std::optional<std::vector<uint8_t>> {
            if (!stfs_files) {
                return std::nullopt;
            }

            const auto it = stfs_files->find(normalize_file_key(std::string{name}));
            if (it == stfs_files->end()) {
                return std::nullopt;
            }

            std::vector<uint8_t> out;
            out.reserve(it->second.size());
            for (const auto byte : it->second) {
                out.push_back(std::to_integer<uint8_t>(byte));
            }
            return out;
        };

        auto ensure_flashfs_files = [&new_nand]() -> flashfs_files_t& {
            if (!new_nand.flashfs_files) {
                new_nand.flashfs_files.emplace();
            }
            return *new_nand.flashfs_files;
        };

        auto ensure_flashfs_payloads = [&new_nand]() -> flashfs_payload_map_t& {
            if (!new_nand.flashfs_payloads) {
                new_nand.flashfs_payloads.emplace();
            }
            return *new_nand.flashfs_payloads;
        };

        auto ensure_payloads = [&new_nand]() -> payloads_t& {
            if (!new_nand.payloads) {
                new_nand.payloads.emplace();
            }
            return *new_nand.payloads;
        };

        auto find_patch_section = [&](PatchSectionTarget target) -> const ParsedPatchSection* {
            if (!parsed_patchset) {
                return nullptr;
            }

            const auto it = std::find_if(
                parsed_patchset->sections.begin(), parsed_patchset->sections.end(),
                [target](const ParsedPatchSection& section) { return section.target == target; });
            return it != parsed_patchset->sections.end() ? &*it : nullptr;
        };

        auto has_any_key_bytes = [](const std::array<uint8_t, 16>& key) {
            return std::any_of(key.begin(), key.end(), [](uint8_t byte) { return byte != 0; });
        };

        auto apply_glitch_patch_section = [&](std::vector<uint8_t>& bytes, PatchSectionTarget target,
                                             std::string_view stage_name) -> bool {
            const auto* section = find_patch_section(target);
            if (!section) {
                return true;
            }
            if (section->encoding != PatchSectionEncoding::XePatch) {
                Log::Error("Patch section '{}' for {} is not an xePatch section",
                           section->identifier, stage_name);
                return false;
            }

            XePatchSection xe_section;
            xe_section.identifier = section->identifier;
            xe_section.entries = section->entries;

            if (!XePatch::ApplyPatchSection(bytes.data(), static_cast<uint32_t>(bytes.size()),
                                            xe_section)) {
                Log::Error("Failed to apply {} patch section '{}'", stage_name,
                           section->identifier);
                return false;
            }

            Log::Info("Applied {} patch section '{}'", stage_name, section->identifier);
            return true;
        };

        auto load_security_common_file = [&](std::string_view entry_name)
            -> std::optional<resolved_file_t> { return load_from_common_dir(entry_name); };

        auto load_flashfs_payload_file = [&](std::string_view entry_name)
            -> std::optional<resolved_file_t> {
            if (auto loaded = load_from_root(fw_dir, entry_name)) {
                return loaded;
            }
            if (auto loaded = load_from_root(data_dir, entry_name)) {
                return loaded;
            }
            if (auto stfs_file = stfs_file_to_u8(entry_name)) {
                return resolved_file_t{std::filesystem::path{"<stfs>"}, std::move(*stfs_file)};
            }
            return load_from_common_dir(entry_name);
        };

        // merge nand security into struct depending on options
        if (sec_sec) {
            for (const auto& entry : *sec_sec) {
                const std::string key_lower = normalize_file_key(entry.key);

                if (key_lower == "fcrt.bin") {
                    if (opts.nofcrt.value_or(false)) {
                        continue;
                    }
                    if (!opts.nosecurity.value_or(false) && donor_nand &&
                        donor_nand->flashfs_files && donor_nand->flashfs_files->fcrt) {
                        ensure_flashfs_files().fcrt = donor_nand->flashfs_files->fcrt;
                    } else if (!opts.nosusecurity.value_or(false)) {
                        if (auto stfs_file = stfs_file_to_u8(key_lower)) {
                            ensure_flashfs_files().fcrt = std::move(*stfs_file);
                        } else if (auto common_file = load_security_common_file(entry.key)) {
                            ensure_flashfs_files().fcrt = std::move(common_file->second);
                        }
                    } else if (auto common_file = load_security_common_file(entry.key)) {
                        ensure_flashfs_files().fcrt = std::move(common_file->second);
                    }
                } else if (key_lower == "crl.bin") {
                    if (!opts.nosecurity.value_or(false) && donor_nand &&
                        donor_nand->flashfs_files && donor_nand->flashfs_files->crl) {
                        ensure_flashfs_files().crl = donor_nand->flashfs_files->crl;
                    } else if (!opts.nosusecurity.value_or(false)) {
                        if (auto stfs_file = stfs_file_to_u8(key_lower)) {
                            ensure_flashfs_files().crl = std::move(*stfs_file);
                        } else if (auto common_file = load_security_common_file(entry.key)) {
                            ensure_flashfs_files().crl = std::move(common_file->second);
                        }
                    } else if (auto common_file = load_security_common_file(entry.key)) {
                        ensure_flashfs_files().crl = std::move(common_file->second);
                    }
                } else if (key_lower == "dae.bin") {
                    if (!opts.nosecurity.value_or(false) && donor_nand &&
                        donor_nand->flashfs_files && donor_nand->flashfs_files->dae) {
                        ensure_flashfs_files().dae = donor_nand->flashfs_files->dae;
                    } else if (!opts.nosusecurity.value_or(false)) {
                        if (auto stfs_file = stfs_file_to_u8(key_lower)) {
                            ensure_flashfs_files().dae = std::move(*stfs_file);
                        } else if (auto common_file = load_security_common_file(entry.key)) {
                            ensure_flashfs_files().dae = std::move(common_file->second);
                        }
                    } else if (auto common_file = load_security_common_file(entry.key)) {
                        ensure_flashfs_files().dae = std::move(common_file->second);
                    }
                } else if (key_lower == "extended.bin") {
                    if (!opts.nosecurity.value_or(false) && donor_nand &&
                        donor_nand->flashfs_files && donor_nand->flashfs_files->extended) {
                        ensure_flashfs_files().extended = donor_nand->flashfs_files->extended;
                    } else if (!opts.nosusecurity.value_or(false)) {
                        if (auto stfs_file = stfs_file_to_u8(key_lower)) {
                            ensure_flashfs_files().extended = std::move(*stfs_file);
                        } else if (auto common_file = load_security_common_file(entry.key)) {
                            ensure_flashfs_files().extended = std::move(common_file->second);
                        }
                    } else if (auto common_file = load_security_common_file(entry.key)) {
                        ensure_flashfs_files().extended = std::move(common_file->second);
                    }
                } else if (key_lower == "secdata.bin") {
                    if (!opts.nosecurity.value_or(false) && donor_nand &&
                        donor_nand->flashfs_files && donor_nand->flashfs_files->secdata) {
                        ensure_flashfs_files().secdata = donor_nand->flashfs_files->secdata;
                    } else if (!opts.nosusecurity.value_or(false)) {
                        if (auto stfs_file = stfs_file_to_u8(key_lower)) {
                            ensure_flashfs_files().secdata = std::move(*stfs_file);
                        } else if (auto common_file = load_security_common_file(entry.key)) {
                            ensure_flashfs_files().secdata = std::move(common_file->second);
                        }
                    } else if (auto common_file = load_security_common_file(entry.key)) {
                        ensure_flashfs_files().secdata = std::move(common_file->second);
                    }
                }
            }
        }

        if (flashfs_sec) {
            for (const auto& entry : *flashfs_sec) {
                const auto key_lower = normalize_file_key(entry.key);
                if (auto flashfs_file = load_flashfs_payload_file(entry.key)) {
                    ensure_flashfs_payloads()[key_lower] = std::move(flashfs_file->second);
                }
            }
        }


        std::array<uint8_t, 16> cb_key{};
        std::array<uint8_t, 16> cb_b_key{};
        std::optional<bl2_header> cb_a_header;

        // main bootloader section
        const Ini::Section* main_sec = build_doc->get(section_name);
        if (main_sec) {
            for (const auto& entry : *main_sec) {
                std::string key_lower = entry.key;
                std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);

                if (key_lower == "none")
                    continue;

                auto resolved_stage = load_main_section_file(entry.key);
                if (!resolved_stage) {
                    Log::Warn("Stage file '{}' not found in fw/data/common directories",
                              entry.key);
                    continue;
                }

                auto& [stage_path, stage_data] = *resolved_stage;

                try {
                    if (key_lower.starts_with("cba") || key_lower.starts_with("cb_") ||
                        key_lower.starts_with("sb_")) {
                        auto cb = BootloaderCb::parse(stage_data);
                        if (bl_key_bytes.size() == 16) {
                            cb.decrypt(bl_key_bytes.data());
                        }
                        auto cb_bytes = cb.serialize();
                        if (cb.is_decrypted()) {
                            cb_a_header = cb.header;
                            if (cb.derived_key) {
                                cb_key = *cb.derived_key;
                            }
                            if (!apply_glitch_patch_section(cb_bytes, PatchSectionTarget::Cb,
                                                            "CB")) {
                                return 1;
                            }
                            new_nand.cb_or_A = std::move(cb_bytes);
                            Log::Info("CB '{}' parsed successfully (v{})", entry.key,
                                      cb.header.header.version);
                        } else {
                            new_nand.cb_or_A = std::move(stage_data);
                            Log::Warn("CB '{}' parsed but is not decrypted", entry.key);
                        }
                    } else if (key_lower.starts_with("cbx")) {
                        auto cbx = BootloaderCb::parse(stage_data);
                        if (bl_key_bytes.size() == 16) {
                            cbx.decrypt(bl_key_bytes.data());
                        }
                        new_nand.cb_X = std::move(stage_data);
                        if (cbx.is_decrypted()) {
                            Log::Info("CBX '{}' parsed successfully (v{})", entry.key,
                                      cbx.header.header.version);
                        } else {
                            Log::Warn("CBX '{}' parsed but is not decrypted", entry.key);
                        }
                    } else if (key_lower.starts_with("cbb")) {
                        auto cbb = BootloaderCb::parse(stage_data);
                        if (cb_a_header && has_any_key_bytes(cb_key) && cpu_key_bytes.size() == 16) {
                            cbb.decrypt_v2(*cb_a_header, cb_key.data(), cpu_key_bytes.data());
                        }
                        auto cbb_bytes = cbb.serialize();
                        if (cbb.is_decrypted()) {
                            if (cbb.derived_key) {
                                cb_b_key = *cbb.derived_key;
                            }
                            if (!apply_glitch_patch_section(cbb_bytes, PatchSectionTarget::Cbb,
                                                            "CB_B")) {
                                return 1;
                            }
                            new_nand.cb_B = std::move(cbb_bytes);
                            Log::Info("CBB '{}' parsed successfully (v{})", entry.key,
                                      cbb.header.header.version);
                        } else {
                            new_nand.cb_B = std::move(stage_data);
                            Log::Warn("CBB '{}' parsed but is not decrypted", entry.key);
                        }
                    } else if (key_lower.starts_with("sc")) {
                        auto sc = BootloaderSc::parse(stage_data);
                        new_nand.sc = std::move(stage_data);
                        Log::Info("SC '{}' parsed successfully (v{})", entry.key,
                                  sc.header.header.version);
                    } else if (key_lower.starts_with("cd")) {
                        auto cd = BootloaderCd::parse(stage_data);
                        const uint8_t* active_cb_key =
                            has_any_key_bytes(cb_b_key)
                                ? cb_b_key.data()
                                : (has_any_key_bytes(cb_key) ? cb_key.data() : nullptr);
                        if (active_cb_key) {
                            cd.decrypt(active_cb_key,
                                       cpu_key_bytes.size() == 16 ? cpu_key_bytes.data() : nullptr);
                        }

                        auto cd_bytes = cd.serialize();
                        if (cd.is_decrypted()) {
                            if (!apply_glitch_patch_section(cd_bytes, PatchSectionTarget::Cd,
                                                            "CD")) {
                                return 1;
                            }
                            new_nand.cd = std::move(cd_bytes);
                            Log::Info("CD '{}' parsed successfully (v{})", entry.key,
                                      cd.header.header.version);
                        } else {
                            new_nand.cd = std::move(stage_data);
                            Log::Warn("CD '{}' parsed but is not decrypted", entry.key);
                        }
                    } else if (key_lower.starts_with("ce")) {
                        auto ce = BootloaderCe::parse(stage_data);
                        new_nand.ce = std::move(stage_data);
                        Log::Info("CE '{}' parsed successfully (v{})", entry.key,
                                  ce.header.header.version);
                    } else if (key_lower.starts_with("cf")) {
                        auto cf = BootloaderCf::parse(stage_data);
                        if (!new_nand.patchslot_0) {
                            new_nand.patchslot_0.emplace();
                        }
                        new_nand.patchslot_0->cf = std::move(stage_data);
                        Log::Info("CF '{}' parsed successfully (v{})", entry.key,
                                  cf.header.header.version);
                    } else if (key_lower.starts_with("cg")) {
                        auto cg = BootloaderCg::parse(stage_data);
                        if (!new_nand.patchslot_0) {
                            new_nand.patchslot_0.emplace();
                        }
                        new_nand.patchslot_0->cg = std::move(stage_data);
                        Log::Info("CG '{}' parsed successfully (v{})", entry.key,
                                  cg.header.header.version);
                    } else {
                        Log::Trace("Ignoring unhandled bootloader stage '{}'", entry.key);
                    }
                } catch (const std::exception& ex) {
                    Log::Error("Stage '{}' parse failed: {}", entry.key, ex.what());
                }
            }
        } else if (!section_name.empty()) {
            Log::Warn("Section '{}' not found in build INI", section_name);
        }

        if (parsed_patchset) {
            if (parsed_patchset->kind == PatchSetKind::Glitch) {
                if (const auto* khv_section = find_patch_section(PatchSectionTarget::Khv)) {
                    ensure_payloads().addon_patches = khv_section->raw_data;
                    Log::Info("Assigned merged glitch payloads from '{}' ({} bytes)",
                              khv_section->identifier, khv_section->raw_data.size());
                }
            } else if (parsed_patchset->kind == PatchSetKind::Jtag) {
                auto serialized_patchset = BinaryParser::SerializePatchSet(*parsed_patchset);
                ensure_payloads().addon_patches = std::move(serialized_patchset);
                Log::Info("Assigned merged JTAG patch payloads ({} bytes)",
                          ensure_payloads().addon_patches->size());
            }
        }
    }

    return 0;
}
