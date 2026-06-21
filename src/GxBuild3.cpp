#include "Ascii.hpp"
#include "Log.hpp"

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

int main() {
    Log::Init();

    std::cout << Ascii::Logo;

    Log::Info("gxbuild3 starting... (Built on: {} {} with {})", __DATE__, __TIME__, compiler);

    return 0;
}