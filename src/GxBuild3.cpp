#include "Args.hpp"
#include "Ascii.hpp"
#include "Log.hpp"

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <iostream>
#include <sstream>
#include <string>

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

    build_sub->callback([&args]() { args.mode = "build"; });
    extract_sub->callback([&args]() { args.mode = "extract"; });
    extract_sub->add_flag("--all", args.extract_all, "Extract all files");

    app.require_subcommand(0, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
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

    if (!app.got_subcommand(build_sub) && !app.got_subcommand(extract_sub))
        Log::Warn("No subcommand specified, defaulting to 'build'.");

    return 0;
}