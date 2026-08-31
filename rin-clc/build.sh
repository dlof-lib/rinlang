#!/usr/bin/env bash
# build.sh — يبني build/clc مباشرة عبر g++ (لا يتطلب CMake).
# إن كان CMake متوفراً ومفضَّلاً: cmake -B build && cmake --build build
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Isrc \
    src/sha256.cpp \
    src/clc_compress.cpp \
    src/clc_rin_opt.cpp \
    src/clc_security.cpp \
    src/clc_container.cpp \
    src/clc_zip_import.cpp \
    src/cli/main.cpp \
    src/cli/self_test.cpp \
    -lz -o build/clc
echo "Built: build/clc"
