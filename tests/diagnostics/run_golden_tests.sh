#!/usr/bin/env bash
# tests/diagnostics/run_golden_tests.sh
# ============================================================================
# ينفّذ كل اختبارات tests/diagnostics/golden/*.rin ويقارن الناتج الفعلي بملف
# .expected المطابق له حرفياً. راجع docs/ERROR_SYSTEM.md (القسم 31: Golden Tests).
#
# نوعان من الاختبارات (يُعرَف النوع تلقائياً من اسم الملف، انظر التصنيف أدناه):
#   - اختبارات "parse-time" (أخطاء Lexer/Parser): تُشغَّل عبر
#       rin_check <file>.rin --format=plain
#     (أداة سطر أوامر مستقلة تفحص الملف فقط دون تنفيذه — tools/rin_check.cpp،
#     أو الهدف المكافئ rincheck في app/src/main/cpp/CMakeLists.txt)
#   - اختبارات "runtime" (أخطاء Interpreter مثل متغير غير معرَّف، فهرس خارج
#     الحدود، عدد وسائط خاطئ...): تُشغَّل عبر الـ CLI العادي (rin <file>.rin)،
#     لأن هذه الأخطاء لا تظهر إلا أثناء التنفيذ الفعلي، لا في مرحلة lex+parse.
#
# الاستخدام:
#   ./run_golden_tests.sh /path/to/rin_check /path/to/rin
#
# إن لم تُمرَّر المسارات، يُفترض وجود الأداتين على PATH باسمي "rin_check" و"rin".
# كود الخروج: 0 إن نجحت كل الاختبارات، 1 إن فشل واحد أو أكثر.
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOLDEN_DIR="$SCRIPT_DIR/golden"

RIN_CHECK="${1:-rin_check}"
RIN_CLI="${2:-rin}"

# اختبارات "runtime": أخطاء لا تظهر إلا أثناء تنفيذ الكود فعلياً (Interpreter)،
# لذا تُشغَّل عبر الـ CLI العادي بدل rin_check (الذي لا يُنفِّذ الملف إطلاقاً).
RUNTIME_TESTS=(
    "undefined_variable"
    "type_mismatch_index"
    "wrong_arg_count"
    "unknown_function"
)

is_runtime_test() {
    local name="$1"
    for t in "${RUNTIME_TESTS[@]}"; do
        if [[ "$t" == "$name" ]]; then return 0; fi
    done
    return 1
}

pass=0
fail=0

cd "$GOLDEN_DIR" || exit 1

for rin_file in *.rin; do
    name="$(basename "$rin_file" .rin)"
    expected_file="$name.expected"
    if [[ ! -f "$expected_file" ]]; then
        echo "SKIP  $name (no .expected file)"
        continue
    fi

    if is_runtime_test "$name"; then
        actual="$("$RIN_CLI" "$rin_file" 2>&1)"
    else
        actual="$("$RIN_CHECK" "$rin_file" --format=plain 2>&1)"
    fi
    expected="$(cat "$expected_file")"

    if [[ "$actual" == "$expected" ]]; then
        echo "PASS  $name"
        pass=$((pass + 1))
    else
        echo "FAIL  $name"
        echo "  --- expected ---"
        echo "$expected" | sed 's/^/  /'
        echo "  --- actual ---"
        echo "$actual" | sed 's/^/  /'
        fail=$((fail + 1))
    fi
done

echo ""
echo "$pass passed, $fail failed"
[[ $fail -eq 0 ]]
