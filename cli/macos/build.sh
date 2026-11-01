#!/usr/bin/env bash
# cli/macos/build.sh
# يبني rin (المفسِّر كسطر أوامر) على macOS عبر CMake + clang++.
#
# الاستخدام:
#   ./build.sh              بناء عادي (معمارية الجهاز الحالي فقط: arm64 أو x86_64)
#   ./build.sh --universal  بناء "universal2" يعمل على Apple Silicon وIntel معاً
set -euo pipefail
cd "$(dirname "$0")"

if ! xcode-select -p >/dev/null 2>&1; then
    echo "[rin] أدوات Xcode Command Line Tools غير مثبّتة. ثبّتها أولاً:"
    echo "      xcode-select --install"
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "[rin] CMake غير مثبّت. ثبّته عبر Homebrew:"
    echo "      brew install cmake"
    echo "      (لا يوجد Homebrew؟ راجع https://brew.sh)"
    exit 1
fi

EXTRA_ARGS=()
if [[ "${1:-}" == "--universal" ]]; then
    echo "[rin] بناء universal2 (arm64 + x86_64)..."
    EXTRA_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release "${EXTRA_ARGS[@]}"
cmake --build build -- -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo
echo "[rin] تم البناء بنجاح: build/rin"
file build/rin || true
echo "[rin] لتثبيته عالمياً (اختياري):  sudo cmake --install build"
