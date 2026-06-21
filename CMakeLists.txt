cmake_minimum_required(VERSION 3.25)
project(GxBuild3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(extern/GxCrypt)
add_subdirectory(extern/spdlog)
add_subdirectory(extern/stfs)

add_executable(gxbuild3 
    src/GxBuild3.cpp
)

target_include_directories(gxbuild3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(gxbuild3 PRIVATE
    GxCrypt
    spdlog::spdlog
    stfs
)

if(MSVC)
    target_compile_options(gxbuild3 PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(gxbuild3 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()