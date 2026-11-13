#!/usr/bin/env bash
# تشغيل اختبارات link/import: tests/modules/run.sh [path/to/rin]
HERE="$(cd "$(dirname "$0")" && pwd)"; RIN="$(realpath "${1:-$HERE/../../cli/linux/build/rin}")"
cd "$(dirname "$0")" || exit 1
fail=0
for t in link_live link_self link_modes link_import_alias import_rename reexport reexport_private reexport_all reexport_all_direct link_filters link_file slab_main slab_nested slab_guard json_import json_into json_extra; do
  got="$("$RIN" "$t.rin" 2>&1 | grep -v '^📥\|^📦\|^✅\|^🧬\|^🔗\|^↺\|^🔓')"
  if [ "$got" == "$(cat "$t.expected")" ]; then echo "PASS $t"; else echo "FAIL $t"; diff <(echo "$got") "$t.expected"; fail=1; fi
done
# حالات الخطأ: يجب أن يُنتج كل ملف في errors/ رمز الخطأ المتوقع (E....) بلا انهيار
for f in errors/*.rin; do
  t="${f%.rin}"
  got="$("$RIN" "$f" 2>&1 | grep -m1 '^error\[' | sed 's/error\[\(E[0-9]*\)\].*/\1/')"
  if [ "$got" == "$(cat "$t.expected")" ]; then echo "PASS $t"; else echo "FAIL $t (got '$got')"; fail=1; fi
done
exit $fail
