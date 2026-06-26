cmake_minimum_required(VERSION 3.25)
project(GxBuild3 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(extern/CLI11)
add_subdirectory(extern/GxCrypt)
add_subdirectory(extern/spdlog)
add_subdirectory(extern/stfs)

add_executable(gxbuild3
    src/GxBuild3.cpp
    src/Log.cpp
    src/utils/FileManager.cpp
    src/utils/FlashBlockDriver.cpp
    src/ini/IniParser.cpp
    src/patchers/BinaryParser.cpp
    src/patchers/Patcher.cpp
    src/patchers/Signature.cpp
    src/bootloaders/2bl.cpp
    src/bootloaders/Keyvault.cpp
    src/bootloaders/SMC.cpp
)

target_compile_features(gxbuild3 PRIVATE cxx_std_23)
set_target_properties(gxbuild3 PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(gxbuild3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gxbuild3 PRIVATE
    CLI11::CLI11
    GxCrypt
    spdlog::spdlog
    stfs
    stdc++fs
)

if(MSVC)
    target_compile_options(gxbuild3 PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(gxbuild3 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(GNUInstallDirs)

install(TARGETS gxbuild3
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)