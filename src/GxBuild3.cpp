#include "Ascii.hpp"
#include "Log.hpp"

#include <iostream>

int main() {
    Log::Init();

    std::cout << Ascii::Logo;

    Log::Info("hello from GxBuild3.");
    return 0;
}