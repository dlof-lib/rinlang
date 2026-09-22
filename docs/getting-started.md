# getting-started.md — البداية

كل أمر في هذا الملف **جُرِّب فعليًا**: بنيتُ أداة `rin` الرسمية محليًا من مصدرها
الحقيقي (`cli/linux/`، بلا محاكاة) وشغَّلت كل أمر مذكور هنا بنفسي.

## البناء (على لينكس)
```bash
cd cli/linux
./build.sh          # يحتاج cmake + g++/clang++ + zlib1g-dev
# الناتج: cli/linux/build/rin
```
```
$ rin --version
Rin 1.0.0
rinc 0.5.0
Runtime 1.0.0
Edition 2026
Target x86_64-linux
```

## تشغيل كود مباشر
```bash
$ rin -c "print 1+1;"
2
```

## تشغيل ملف
```bash
$ cat hello.rin
print "Hello from Rin!";
let x = 5;
print x * 2;

$ rin run hello.rin
Hello from Rin!
10
```

## REPL تفاعلي / من stdin
```bash
$ rin                    # بلا وسائط -> REPL تفاعلي
$ echo 'print "via stdin";' | rin
via stdin
```

## مشروع جديد
```bash
$ rin new myproj --template console
تم إنشاء مشروع Rin: myproj/
  myproj/rin.toml
  myproj/src/main.rin
  myproj/tests/basic.rin
  myproj/README.md

التالي:
  cd myproj && rin build && ./myproj
```
`rin.toml` الناتج:
```toml
[package]
name = "myproj"
version = "0.1.0"
edition = "2026"

[dependencies]
```
`src/main.rin` الناتج:
```rin
// نقطة الدخول - myproj
print "Hello from Rin!";
```

## فحص نحوي بلا تشغيل
```bash
$ rin check hello.rin    # بلا أي إخراج = بلا أخطاء
```
(`--format=plain|short|json|lsp` لصيغ إخراج مختلفة، انظر [`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md)).

## `rin build` — تنفيذي أصلي عبر `rinc` — **مجموعة فرعية فقط من اللغة**
```bash
$ rin build hello.rin -o hello
$ ./hello
```
تنبيه مهم تحقّقتُ منه فعليًا: `rin build` (المترجم الأصلي `rinc`، وليس `rin run`
المفسِّر) **لا** يدعم كل اللغة بعد — الناتج الحرفي لـ`rin doctor` على نسخة هذا
المستودع:
```
ما هو مدعوم فعلياً في هذا التوزيع:
  ✓ المفسّر (rin run / REPL)
  ✓ الفحص النحوي (rin check)
  ✓ البناء الأصلي عبر rinc (rin build) — اللغة الإجرائية الأساسية + @container/@container.data
  ✗ container.pipe/api/import/table/doc/object/portal/block/sticker/aukt، Containers.Group، Volume — غير مدعومة بعد في rinc
  ✗ WASM / Android backends من خلال هذا CLI — غير مُدمَجة بعد
```
بكلمة أخرى: **`rin run` (المفسِّر) هو الطريقة الكاملة لتشغيل أي برنامج Rin** بكل
ميزاته (`enum`، `match`، `reckon`، `@container` بكل أنواعه، `@import`، ...) —
`rin build` مفيد فقط لإنتاج تنفيذي مستقل من الجزء الإجرائي الأساسي + `@container`
البسيطة، وسيرفض أي شيء أعقد بخطأ تحليل واضح بدل تجاهله بصمت (رأيتُ هذا فعليًا:
`rinc` يرفض حتى `enum` حاليًا).

## أدوات مساعدة أخرى
| الأمر | الغرض |
|---|---|
| `rin test [dir]` | تشغيل ملفات اختبار `.rin`. |
| `rin fmt <file> [--write]` | إعادة محاذاة المسافات البادئة. |
| `rin clean` | حذف مخرجات `./build`. |
| `rin doctor` | فحص بيئة التطوير الفعلية (بالضبط كما ظهر أعلاه). |

## الخطوة التالية
- [`syntax.md`](./syntax.md) → [`variables.md`](./variables.md) → [`control-flow.md`](./control-flow.md)
  → [`functions.md`](./functions.md) لأساسيات اللغة نفسها.
- [`language-reference.md`](./language-reference.md) لخريطة كل التوثيق.
