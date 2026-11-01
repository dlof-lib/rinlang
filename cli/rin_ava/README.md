# RIN_AVA — أداة بناء Rin بواجهة g++ (متعددة الخلفيات)

```
rin_ava prog.rin -o prog            # تنفيذي مستقل (الخلفية الافتراضية interp)
./prog arg1 arg2                    # يعمل في أي مكان، الوسائط تصل للبرنامج
rin_ava -fsyntax-only prog.rin
rin_ava -fanalyze prog.rin -o prog  # + المحلل الدلالي ومدقّق الأنواع
rin_ava --list-backends             # ما المتاح فعلاً على هذا الجهاز
rin_ava --help
```

rin_ava لا يضيف لغة ولا محرّكاً جديداً: هو **سائق** يوصل برنامج Rin إلى أي من أدوات Rin الأصلية
(المفسّر، آلة البايتكود، المترجم rinc، مفسّر Go). الفحص دائماً بـ `rin check` (المحلل الحقيقي)،
والأخطاء بصيغة `file:line:col: error: message`، والخروج 0 نجاح / 1 فشل.

## الخلفيات  (`-fbackend=NAME` أو `--backend=NAME`)

| الخلفية | اختصار | ماذا تفعل | تحتاج |
|---|---|---|---|
| `interp` (افتراضية) | `-finterp` `-fbundle` | نسخة من `rin` + البرنامج ← **المفسّر `rin_interpreter.cpp`**؛ تقبل كل برنامج صالح | `rin` فقط |
| `vm` | `-fvm` | **صورة VM (مكتبة `rin_vm`)**: البرنامج + كل ما يستورده محلولاً وقت البناء في صورة واحدة، يشغّلها المحرّك الكامل ← **كل اللغة**، وبناء فوري. `-fvm-bytecode`: المسار السريع الصارم (`rin bytecode` ← `BytecodeVM`، نواة اللغة فقط). `-fvm-auto`: بايتكود إن أمكن (بلا استيراد) وإلا الصورة | `rin` فقط |
| `native` | `-fnative` | **rinc**: Rin ← C ← cc (أسرع ناتج؛ المجموعة التي يترجمها rinc فقط) | `rinc` + مترجم C |
| `engine` | `-fengine` | يبني **المفسّر نفسه من مصدره** (`rin_interpreter.cpp` وأخواته) مع البرنامج مُضمَّناً؛ **لا يحتاج `rin` أبداً** | مترجم C++ + zlib + مصدر `app/src/main/cpp` |
| `go` | `-fgo` | `gorin-embedded` (مفسّر Go في `gorin/cmd/rin`) + البرنامج ← تنفيذي مستقل؛ يُفحَص بـ `gorin-embedded --check` | الملف الثنائي، أو `go` + مصدر `gorin/` (يُبنى تلقائياً أول مرة) |
| `auto` | `-fauto` | `native` إن قبله rinc، وإلا `interp`؛ وإن غاب `rin` يجرّب `go` | — |

- `-S` / `-c` بلا خلفية صريحة = `native` (السلوك القديم). مع `-fvm`: `-S` قائمة التعليمات، `-c` ملف `.rbc`. مع `-fengine`: `-S` يكتب ملف C++ المُغلِّف.
- `native` و`engine` و`go` و`auto` لا تتطلب `rin` للبناء نفسه (يُتخطّى فحص الصياغة بتحذير إن غاب).
- `engine`: الكائنات تُخزَّن مؤقتاً (`~/.cache/rin_ava`، أو `--engine-cache=`/`RIN_AVA_CACHE`) ومفتاحها بصمة المصدر، فأول بناء بطيء (دقائق) وما بعده ثوانٍ؛ `-jN` للتوازي.
- `vm` (الصورة) كامل الدلالات؛ وضع `-fvm-bytecode` يدعم نواة اللغة فقط وقد تختلف دلالاته؛ `go` نواة اللغة كذلك نواة اللغة (انظر `gorin/README.md`) ويعرّف المتغير العام `args` دائماً.
- `@import`: تُضمَّن في خلفية `vm` (الصورة). في بقية الخلفيات لا تُضمَّن وتُحلّ وقت التشغيل (يظهر تحذير).
- ملفات `.rvm`: `rin_ava -fvm -c prog.rin -o prog.rvm` أو `rin_vm pack prog.rin` ثم `rin image prog.rvm [args]`.

## البحث عن الأدوات
`--runtime=`/`RIN_AVA_RUNTIME` ← بجوار rin_ava ← `RIN_HOME/bin` ← PATH، وكذلك:
`--rinc=`/`RIN_AVA_RINC`، `--cc=`/`RIN_AVA_CC`، `RIN_AVA_CXX`، `--engine-src=`/`RIN_AVA_ENGINE_SRC`،
`--go-runtime=`/`RIN_AVA_GORIN`، `--gorin-src=`/`RIN_AVA_GORIN_SRC`.

## حمولة التنفيذي
نهاية الملف: `[name\0 data][u64 len][magic]` — `RINAVAP1` (مصدر ← المفسّر؛ متوافق مع ما بُني سابقاً)،
`RINAVAP2` (حقل `kind`: `S` مصدر، `B` بايتكود). `rin` و`gorin-embedded` العاديان لا يتأثران.

## البناء والاختبار
```
cd cli/linux && ./build.sh        # build/rin  build/rin_ava  build/rinc  build/gorin-embedded (إن وُجد Go)
cli/rin_ava/tests/run_tests.sh cli/linux/build/rin_ava cli/linux/build/rin [--engine]
```
