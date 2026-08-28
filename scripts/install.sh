#!/usr/bin/env bash
# ============================================================================
# scripts/install.sh — مثبّت Rin (Linux / macOS)
# ============================================================================
# يبني rin و rinc فعلياً من مصدر هذا المستودع (لا ملفات ثنائية مُسبقة الصنع
# مضمّنة هنا) ثم يثبتهما تحت RIN_HOME، ويضيف RIN_HOME/bin إلى PATH.
#
# الاستخدام:
#   ./scripts/install.sh                 يثبّت إلى ~/.rin
#   RIN_HOME=/opt/rin ./scripts/install.sh   يثبّت إلى مسار مخصّص
#
# ملاحظة صادقة: main.cpp الموحّد (new/build/run/test/fmt/clean/doctor) مبني
# ومُختبر فعلياً على Linux فقط حتى الآن (cli/linux). على macOS يُبنى بنفس
# المصدر لأنه C++17 قياسي بدون اعتمادات خاصة بلينكس عدا readlink("/proc/self/exe")
# لإيجاد rinc بجانب rin؛ هذا السطر الوحيد غير المتوافق مع macOS، فيتم تعويضه
# هنا بضبط RIN_HOME في متغيرات البيئة بدل الاعتماد على مسار التنفيذي.
# ============================================================================
set -euo pipefail

RIN_HOME="${RIN_HOME:-$HOME/.rin}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="$REPO_DIR/app/src/main/cpp"

echo "[rin-install] مستودع المصدر : $REPO_DIR"
echo "[rin-install] وجهة التثبيت  : $RIN_HOME"
echo

# ---------------------------------------------------------------------------
# 1) التحقق من مترجم C++
# ---------------------------------------------------------------------------
CXX=""
if command -v g++ >/dev/null 2>&1; then CXX="g++"
elif command -v clang++ >/dev/null 2>&1; then CXX="clang++"
else
    echo "[rin-install] خطأ: لا يوجد g++ ولا clang++ على النظام." >&2
    echo "  Debian/Ubuntu: sudo apt install g++" >&2
    echo "  Fedora:        sudo dnf install gcc-c++" >&2
    echo "  macOS:         xcode-select --install" >&2
    exit 1
fi
echo "[rin-install] مترجم C++: $CXX ($($CXX --version | head -1))"

CC=""
for c in cc gcc clang; do
    if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
done
if [ -z "$CC" ]; then
    echo "[rin-install] تحذير: لا يوجد مترجم C (cc/gcc/clang) على PATH — rinc يحتاجه لإنتاج تنفيذيات." >&2
fi

# ---------------------------------------------------------------------------
# 2) البناء
# ---------------------------------------------------------------------------
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "[rin-install] بناء rin ..."
"$CXX" -std=c++17 -O2 -I"$CORE" -o "$BUILD_DIR/rin" \
    "$REPO_DIR/cli/linux/src/main.cpp" \
    "$CORE/rin_lexer.cpp" "$CORE/rin_parser.cpp" "$CORE/rin_interpreter.cpp" "$CORE/rin_http.cpp" \
    "$CORE/diagnostics/diagnostic.cpp" "$CORE/diagnostics/source_manager.cpp" \
    "$CORE/diagnostics/diagnostic_engine.cpp" "$CORE/diagnostics/diagnostic_renderer.cpp"

echo "[rin-install] بناء rinc ..."
"$CXX" -std=c++17 -O2 -o "$BUILD_DIR/rinc" "$REPO_DIR/compiler/rinc.cpp"

# ---------------------------------------------------------------------------
# 3) التثبيت
# ---------------------------------------------------------------------------
mkdir -p "$RIN_HOME/bin" "$RIN_HOME/std"
install -m 755 "$BUILD_DIR/rin" "$RIN_HOME/bin/rin"
install -m 755 "$BUILD_DIR/rinc" "$RIN_HOME/bin/rinc"

# نسخ ما هو موجود فعلياً من مكتبة قياسية/أمثلة (بلا ادّعاء أي شيء غير موجود)
if [ -d "$REPO_DIR/examples" ]; then
    cp -r "$REPO_DIR/examples" "$RIN_HOME/std/examples" 2>/dev/null || true
fi

echo "[rin-install] ثُبِّت في: $RIN_HOME/bin/{rin,rinc}"

# ---------------------------------------------------------------------------
# 4) إضافة PATH و RIN_HOME إلى ملف بدء التشغيل المناسب
# ---------------------------------------------------------------------------
SHELL_RC=""
case "${SHELL:-}" in
    */zsh) SHELL_RC="$HOME/.zshrc" ;;
    */bash) SHELL_RC="$HOME/.bashrc" ;;
    *) SHELL_RC="$HOME/.profile" ;;
esac

MARKER="# >>> rin-lang installer >>>"
END_MARKER="# <<< rin-lang installer <<<"
if [ -f "$SHELL_RC" ] && grep -qF "$MARKER" "$SHELL_RC" 2>/dev/null; then
    echo "[rin-install] $SHELL_RC محدَّث مسبقاً — تخطّي"
else
    {
        echo ""
        echo "$MARKER"
        echo "export RIN_HOME=\"$RIN_HOME\""
        echo "export PATH=\"\$RIN_HOME/bin:\$PATH\""
        echo "$END_MARKER"
    } >> "$SHELL_RC"
    echo "[rin-install] أُضيف RIN_HOME/PATH إلى $SHELL_RC"
fi

export RIN_HOME="$RIN_HOME"
export PATH="$RIN_HOME/bin:$PATH"

# ---------------------------------------------------------------------------
# 5) التحقق من التثبيت (فعلياً، وليس افتراضاً)
# ---------------------------------------------------------------------------
echo
echo "[rin-install] التحقق:"
"$RIN_HOME/bin/rin" --version || { echo "[rin-install] فشل rin --version" >&2; exit 1; }
echo
echo "تم التثبيت بنجاح."
echo "افتح طرفية جديدة (أو نفّذ: source $SHELL_RC) ثم شغّل:"
echo "  rin doctor"
echo "  rin new hello && cd hello && rin build && ./build/hello"
