#!/bin/bash
cmake -B build -S . -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j${nproc}
cp build/gxbuild3 /home/e3xp0/Projects/gxBuild-support-files
