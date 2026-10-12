# CHANGES — rin_ava متعدد الخلفيات

rin_ava لم يعد مرتبطاً بـ `rinc` وحده؛ صار سائقاً موحّداً فوق مفسّرات ومترجمات Rin كلها.

## جديد
- **خلفيات**: `interp` (rin_interpreter.cpp عبر runtime)، `vm` (toolchain/BytecodeVM ببايتكود مُضمَّن)،
  `native` (rinc)، `engine` (بناء المحرّك من المصدر بذاكرة كائنات وتوازٍ)، `go` (مفسّر gorin)، `auto`.
- `-fanalyze` (rin analyze)، `--list-backends`، `-jN`، `--engine-cache`، `--go-runtime`، `--gorin-src`.
- حمولة **v2** (`RINAVAP2`) بحقل نوع (مصدر/بايتكود)؛ v1 يبقى مدعوماً.
- `rin` (cli/linux): ينفّذ الحمولتين (مفسّر أو VM).
- `toolchain`: `readBytecodeFromMemory`/`readBytecodeStream`؛ وإصلاح عرض الثوابت الرقمية في BytecodeVM
  (`print 5;` كانت تطبع `5.000000` مخالفةً للمفسّر).
- `gorin/cmd/rin`: كشف الحمولة المُلحَقة، `--check`، ووسائط البرنامج (`args`)؛ و`rin.Check` و`Interp.SetProgramArgs`.
- CMake: أهداف `rinc` و`gorin-embedded` (اختياري) بجوار `rin`/`rin_ava`.

## مكتبة rin_vm (صورة VM كاملة)
- `cli/rin_vm/include/rin_vm.h` (header-only): صورة RVM1 = مصدر + كل @import محلولة (+ بايتكود اختياري) + checksum.
- `rin_ava -fvm` صار يبني الصورة (كل اللغة، فوري)؛ `-fvm-bytecode` المسار القديم؛ `-fvm-auto` بايتكود ثم الصورة.
- `Interpreter::setVirtualFiles` في rin_interpreter (يُفحص قبل القرص في @import والاستيراد الانتقائي).
- `rin image prog.rvm`، حمولة `kind='M'`، أداة `rin_vm pack/info/unpack`.

- `rinvm` (`cli/rin_vm/src/rinvm_main.cpp` + هدف CMake): الآلة الافتراضية الكاملة في ملف واحد؛ `rin_ava -fvm` يفضّلها runtime.
- `cli/rin_vm/tools/convert_all.sh`: تحويل مجلد برامج إلى `.rvm` والتحقق من مطابقة المفسّر.

## ملفات
معدّلة: `app/src/main/cpp/rin_interpreter.{h,cpp}`، `cli/rin_ava/{src/rin_ava.cpp,README.md,tests/run_tests.sh}`، `cli/linux/{CMakeLists.txt,src/main.cpp,src/rin_ava_payload.h}`،
`app/src/main/cpp/toolchain/rin_toolchain.{h,cpp}`، `gorin/rin/interp.go`، `gorin/cmd/rin/main.go`.
جديدة: `cli/rin_vm/{include/rin_vm.h,src/rin_vm_tool.cpp,src/rinvm_main.cpp,tools/convert_all.sh,README.md}`، `gorin/cmd/rin/payload.go`، `CHANGES_RIN_AVA.md`.

## حدود معلومة
macOS/Windows لم تُغيَّر (rin_ava يعتمد POSIX/fork). `@import` تُضمَّن في vm فقط.
