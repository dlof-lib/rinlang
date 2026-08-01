# rin — Rin Language CLI (Linux)

مُفسِّر Rin كسطر أوامر مستقل تماماً عن تطبيق أندرويد/JNI/NDK. يستخدم *نفس*
ملفات المحرّك الحقيقية (`app/src/main/cpp/rin_lexer.cpp`, `rin_parser.cpp`,
`rin_interpreter.cpp`) — لا نسخة مختلفة ولا محاكاة. لا حاجة لأي تعديل توافقية
هنا (بخلاف ويندوز) لأن هذه الملفات مكتوبة أصلاً بواجهات POSIX القياسية التي
يدعمها لينكس مباشرة.

## المتطلبات
```bash
# Debian / Ubuntu
sudo apt install cmake g++

# Fedora
sudo dnf install cmake gcc-c++

# Arch
sudo pacman -S cmake gcc
```

## البناء
```bash
cd cli/linux
./build.sh
```
أو يدوياً:
```bash
cd cli/linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
الناتج: `build/rin`

## التثبيت العالمي (اختياري)
```bash
sudo cmake --install build
# الآن rin متاح من أي مكان في PATH (عادة /usr/local/bin/rin)
```
أو يدوياً بدون صلاحيات root:
```bash
mkdir -p ~/.local/bin
cp build/rin ~/.local/bin/
# تأكد أن ~/.local/bin موجود في PATH (عادة كذلك افتراضياً على أغلب التوزيعات)
```

## الاستخدام
```bash
rin program.rin              # تشغيل ملف
rin -c "print 1+1;"          # تشغيل كود مُمرَّر مباشرة
rin                          # وضع تفاعلي (REPL) داخل طرفية تفاعلية
rin < program.rin            # قراءة الكود من stdin
rin --version                # رقم الإصدار
rin --help                   # المساعدة
```

مثال REPL:
```
$ rin
Rin v0.1.0 - وضع تفاعلي. اكتب سطر Rin ثم Enter لتنفيذه (exit للخروج).
rin[1]> let x = 5;
rin[2]> print x * 2;
10
rin[3]> exit
```

## ما هو خارج نطاق هذا الـCLI
هذا يبني `rin` (**المفسِّر** فقط). لا علاقة له بـ:
- `compiler/rinc.cpp` — مترجم (transpiler) منفصل يولّد C ثم ملفاً تنفيذياً أصلياً
  (`g++ -O2 -o rinc compiler/rinc.cpp`)، ولم يُدرَج بعد في هذا المجلد.
- `bindings/` — يبني `librin.so` كمكتبة مشتركة تُستدعى من Python/Node/C،
  لا كتنفيذي مستقل (`cd bindings && cmake -B build && cmake --build build`).
- `cli/windows/` — نفس فكرة هذا المجلد لكن لـ`rin.exe` على ويندوز.
- محرّك عرض الواجهات (Loomtime) ومعاينته الحية — خاص بتطبيق أندرويد فقط حالياً.

## بناء تلقائي عبر GitHub Actions
`.github/workflows/linux-cli.yml` المرفَق يبني `rin` تلقائياً على
`ubuntu-latest` عند كل push، ويرفعه كـ Artifact قابل للتنزيل مباشرة.
