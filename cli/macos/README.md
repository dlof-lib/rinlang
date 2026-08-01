# rin — Rin Language CLI (macOS)

مُفسِّر Rin كسطر أوامر مستقل تماماً عن تطبيق أندرويد/JNI/NDK. يستخدم *نفس*
ملفات المحرّك الحقيقية (`app/src/main/cpp/rin_lexer.cpp`, `rin_parser.cpp`,
`rin_interpreter.cpp`) — لا نسخة مختلفة ولا محاكاة. مثل لينكس، لا يحتاج أي
تعديل توافقية على هذه الملفات (POSIX قياسي يدعمه macOS مباشرة).

## المتطلبات
```bash
xcode-select --install     # مترجم clang++ (إن لم يكن مثبَّتاً بالفعل)
brew install cmake          # يحتاج Homebrew: https://brew.sh
```

## البناء
```bash
cd cli/macos
./build.sh
```
أو للحصول على ملف تنفيذي "عام" (universal2) يعمل على Apple Silicon وIntel معاً:
```bash
./build.sh --universal
```
أو يدوياً:
```bash
cd cli/macos
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
الناتج: `build/rin`

## التثبيت العالمي (اختياري)
```bash
sudo cmake --install build
# متاح بعدها من أي مكان في PATH (عادة /usr/local/bin/rin على Intel،
# أو /opt/homebrew/bin يدوياً على Apple Silicon حسب إعداد PATH لديك)
```
أو بدون صلاحيات root:
```bash
mkdir -p ~/bin
cp build/rin ~/bin/
# تأكد أن ~/bin موجود في PATH (أضِفه في ~/.zshrc إن لم يكن كذلك)
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

## ملاحظة Gatekeeper (عند تنزيل rin جاهزاً بدل بنائه محلياً)
ملف تنفيذي جرى تنزيله (مثلاً من GitHub Actions Artifact) يُعلَّم تلقائياً
"quarantine" فيرفض macOS تشغيله بضغطة عادية. إمّا:
```bash
xattr -d com.apple.quarantine ./rin
```
أو من Finder: كليك يمين على الملف ← Open ← تأكيد "Open" في نافذة التحذير
(مرة واحدة فقط). البناء محلياً عبر `build.sh` لا يواجه هذه المشكلة إطلاقاً.

## ما هو خارج نطاق هذا الـCLI
هذا يبني `rin` (**المفسِّر** فقط). لا علاقة له بـ:
- `compiler/rinc.cpp` — مترجم (transpiler) منفصل يولّد C ثم ملفاً تنفيذياً أصلياً.
- `bindings/` — يبني `librin.dylib` كمكتبة مشتركة تُستدعى من Python/Node/C.
- `cli/windows/` و`cli/linux/` — نفس الفكرة لمنصّتيهما.
- محرّك عرض الواجهات (Loomtime) ومعاينته الحية — خاص بتطبيق أندرويد فقط حالياً.

## بناء تلقائي عبر GitHub Actions
`.github/workflows/macos-cli.yml` المرفَق يبني `rin` تلقائياً على
`macos-latest` (Apple Silicon) عند كل push، ويرفعه كـ Artifact قابل للتنزيل.
