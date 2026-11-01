# إشعارات المكوّنات الخارجية — Third-Party Notices

محرّك Rin وأداة `rin` مكتوبان بـ C++17 بلا اعتماديات ثقيلة. المكوّنات أدناه **تحتفظ بتراخيصها الأصلية**
ولا تدخل تحت ترخيص MIT الخاص بـ Rin. ترخيص Rin نفسه في [`LICENSE`](./LICENSE).

> هذه القائمة مُجمَّعة من ملفات البناء (`CMakeLists.txt` و`app/build.gradle.kts`) ومن ترويسات الملفات.
> عند إضافة اعتمادية جديدة أو رفع إصدار، حدِّث هذا الملف في نفس طلب الدمج (PR).

## 1) مضمَّنة في المستودع

| المكوّن | الإصدار | المسار | الترخيص |
|---|---|---|---|
| stb_image | 2.30 | `app/src/main/cpp/third_party/stb_image.h` | MIT **أو** Public Domain (Unlicense) — حسب اختيارك |
| JetBrains Mono / JetBrains Mono NL | — | `app/src/main/res/font/rin_code_*.ttf` · `rin_sym_*.ttf` · `rin_tmpl_*.ttf` | SIL OFL 1.1 |

### stb_image — MIT (الخيار A)

```text
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

(البديل B: الملك العام — النص الكامل في ذيل الملف المصدري نفسه.)

### JetBrains Mono — SIL Open Font License 1.1

Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono)

النص الكامل مضمَّن في التطبيق: [`app/src/main/assets/licenses/JetBrainsMono-OFL.txt`](./app/src/main/assets/licenses/JetBrainsMono-OFL.txt).
ملفات الخطوط أُعيدت تسميتها (`rin_code_*` …) لكن بيانات الخط الداخلية لم تتغيّر. لا تبع الخطوط وحدها،
ولا تُصدرها تحت ترخيص آخر.

## 2) مرتبطة عند البناء (غير مضمَّنة كمصدر)

### zlib

يُستخدم للضغط الحقيقي في صيغة `.rcl` وأدوات `clc`. على أندرويد تأتي من NDK sysroot، وعلى سطح المكتب
من حزمة النظام (`zlib1g-dev`).

```text
Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## 3) تطبيق RinStudio (أندرويد فقط)

تُسحب عبر Gradle وتُوزَّع داخل ملف APK، وتخضع لتراخيصها:

| الاعتمادية | الترخيص |
|---|---|
| AndroidX (core-ktx, appcompat, activity-ktx, constraintlayout, recyclerview, swiperefreshlayout, drawerlayout) | Apache-2.0 |
| Material Components (`com.google.android.material`) | Apache-2.0 |
| ZXing core 3.5.3 | Apache-2.0 |
| apksig 8.5.2 | Apache-2.0 |
| Tesseract4Android 4.9.0 (غلاف لـ Tesseract) | Apache-2.0 |
| Firebase (Auth, Realtime Database) | Apache-2.0 للمصدر، ويخضع استخدام الخدمة لشروط Firebase |
| Google ML Kit — Text Recognition | شروط Google ML Kit |

نص Apache-2.0: <https://www.apache.org/licenses/LICENSE-2.0>

## 4) موقع الويب والأدوات (لا تُوزَّع مع المحرّك)

| العنصر | الاستخدام | الترخيص |
|---|---|---|
| IBM Plex Sans Arabic / Mono | خطوط الموقع عبر Google Fonts | SIL OFL 1.1 |
| Emscripten · LLVM | تصريف المحرّك إلى WebAssembly | أدوات بناء (MIT/NCSA · Apache-2.0 مع استثناء LLVM) |
| CMake · Android NDK · GitHub Actions | بناء ونشر | أدوات بناء |

## 5) محتوى المستخدمين

الحزم المنشورة في متجر Rin يملكها ناشروها، وكلٌّ منها بترخيصه المُعلَن في بيانات الحزمة. وجودها في المتجر
لا يجعلها جزءاً من Rin ولا يُرخِّصها تحت MIT.
