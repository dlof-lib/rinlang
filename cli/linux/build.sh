#!/usr/bin/env bash
# cli/linux/build.sh
# يبني rin (المفسِّر كسطر أوامر) على لينكس عبر CMake + g++/clang++.
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v cmake >/dev/null 2>&1; then
    echo "[rin] CMake غير مثبّت. على Debian/Ubuntu:  sudo apt install cmake g++"
    echo "      على Fedora:                          sudo dnf install cmake gcc-c++"
    echo "      على Arch:                             sudo pacman -S cmake gcc"
    exit 1
fi

if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    echo "[rin] لا يوجد مترجم C++ (g++ أو clang++). ثبّت أحدهما أولاً."
    exit 1
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j"$(nproc 2>/dev/null || echo 4)"

echo
echo "[rin] تم البناء بنجاح: build/rin"
echo "[rin] لتثبيته عالمياً (اختياري):  sudo cmake --install build"
