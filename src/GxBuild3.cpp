#include "Ascii.hpp"
#include "Log.hpp"

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <iostream>

#if defined(_MSC_VER)
constexpr const char* compiler = "MSVC";
#elif defined(__clang__)
constexpr const char* compiler = "Clang";
#elif defined(__GNUC__)
constexpr const char* compiler = "GCC";
#else
constexpr const char* compiler = "Unknown";
#endif

int main(int argc, char** argv) {
    Log::Init();

    std::cout << Ascii::Logo;

    Log::Info("gxbuild3 starting... (Built on: {} {} with {})", __DATE__, __TIME__, compiler);

    CLI::App app{"GxBuild3"};

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    return 0;
}