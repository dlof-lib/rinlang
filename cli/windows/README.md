# rin.exe — Rin Language CLI (Windows)

مُفسِّر Rin كسطر أوامر مستقل تماماً عن تطبيق أندرويد/JNI/NDK. يستخدم *نفس*
ملفات المحرّك الحقيقية (`app/src/main/cpp/rin_lexer.cpp`, `rin_parser.cpp`,
`rin_interpreter.cpp`) — لا نسخة مختلفة ولا محاكاة.

## البناء

### الطريقة السريعة
```bat
cd cli\windows
build.bat
```
يكتشف السكربت تلقائياً MSVC (`cl`) أو MinGW (`g++`) في PATH ويبني بأيهما متاح.

### يدوياً — MSVC
1. افتح **"Developer Command Prompt for VS"** (أو **x64 Native Tools Command Prompt**).
2. تأكد من تثبيت [CMake](https://cmake.org/download) و "Desktop development with C++"
   من Visual Studio Installer.
3. من داخل `cli\windows`:
   ```bat
   cmake -B build
   cmake --build build --config Release
   ```
4. الناتج: `build\Release\rin.exe`

### يدوياً — MinGW-w64
1. ثبّت [MinGW-w64](https://www.mingw-w64.org) (أو `choco install mingw` عبر
   [Chocolatey](https://chocolatey.org)) و [CMake](https://cmake.org/download).
2. من داخل `cli\windows`:
   ```bat
   cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```
3. الناتج: `build\rin.exe`

## الاستخدام
```bat
rin.exe program.rin              REM تشغيل ملف
rin.exe -c "print 1+1;"          REM تشغيل كود مُمرَّر مباشرة
rin.exe                          REM وضع تفاعلي (REPL) داخل طرفية تفاعلية
rin.exe < program.rin            REM قراءة الكود من stdin
rin.exe --version                REM رقم الإصدار
rin.exe --help                   REM المساعدة
```

مثال REPL:
```
Rin v0.1.0 - وضع تفاعلي. اكتب سطر Rin ثم Enter لتنفيذه (exit للخروج).
rin[1]> let x = 5;
rin[2]> print x * 2;
10
rin[3]> exit
```

## إضافة rin إلى PATH (اختياري لكن مُستحسَن)
```bat
setx PATH "%PATH%;C:\path\to\cli\windows\build\Release"
```
افتح نافذة أوامر جديدة بعدها، ثم `rin --version` يجب أن يعمل من أي مجلد.

## ملاحظة حول النصوص العربية في الطرفية
`enableWindowsUtf8Console()` في `src/main.cpp` تُفعِّل ترميز UTF-8 لدخل/خرج
الطرفية تلقائياً، لكن `cmd.exe` الكلاسيكي قد لا يملك خطاً يدعم عرض الأحرف
العربية بصرياً حتى مع الترميز الصحيح. **يُنصَح باستخدام
[Windows Terminal](https://aka.ms/terminal)** (افتراضي في ويندوز 11) لعرض
سليم تماماً.

## ما هو خارج نطاق هذا الـCLI
هذا يبني `rin.exe` (**المفسِّر** فقط). لا علاقة له بـ:
- `compiler/rinc.cpp` — مترجم (transpiler) منفصل يولّد C ثم ملفاً تنفيذياً أصلياً؛
  له بناء خاص به منفصل (`g++ -O2 -o rinc rinc.cpp`) وهو عابر للمنصات أساساً لكن
  لم يُدرَج بعد في هذا المجلد.
- `bindings/` — يبني `rin.dll`/`.so`/`.dylib` كمكتبة مشتركة تُستدعى من Python/Node/C،
  لا كتنفيذي مستقل.
- محرّك عرض الواجهات (Loomtime) ومعاينته الحية — خاص بتطبيق أندرويد فقط حالياً.

## بناء تلقائي عبر GitHub Actions
عند رفع هذا المجلد إلى مستودع GitHub عام، يبني ملف
`.github/workflows/windows-cli.yml` المرفَق `rin.exe` تلقائياً على `windows-latest`
عند كل push، ويرفعه كـ Artifact قابل للتنزيل مباشرة — بحيث يقدر أي شخص تنزيل
تنفيذي جاهز دون الحاجة لتثبيت أي مترجم بنفسه.
