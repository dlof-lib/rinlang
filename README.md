<div align="center">

<img src="./docs/assets/brand/icon-192.png" alt="Rin" width="112" height="112" />

# Rin

**لغة برمجة صغيرة وواضحة بمحرّك C++17 واحد — تعمل من سطر الأوامر وعلى الهاتف وفي المتصفح**

<sub>A small, readable programming language · one C++17 engine · CLI, Android IDE (RinStudio) and WebAssembly</sub>

<p>
<a href="https://github.com/dlof-lib/rinlang/actions/workflows/rin__apk.yml"><img alt="CI" src="https://github.com/dlof-lib/rinlang/actions/workflows/rin__apk.yml/badge.svg" /></a>
<img alt="Version" src="https://img.shields.io/badge/version-1.0.0-1DB143" />
<img alt="Engine" src="https://img.shields.io/badge/engine-C%2B%2B17-00599C?logo=cplusplus&logoColor=white" />
<img alt="Android" src="https://img.shields.io/badge/RinStudio-Android%2015%2B-3DDC84?logo=android&logoColor=white" />
<img alt="WASM" src="https://img.shields.io/badge/web-WebAssembly-654FF0?logo=webassembly&logoColor=white" />
<a href="./LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-4E4E4E" /></a>
</p>

<p>
<a href="https://dlof-lib.github.io/rinlang/"><b>🌐 الموقع</b></a> ·
<a href="./docs/getting-started.md"><b>🚀 البداية</b></a> ·
<a href="./docs/language-reference.md"><b>📖 المرجع</b></a> ·
<a href="./lib/README.md"><b>📚 المكتبات</b></a> ·
<a href="https://dlof-lib.github.io/rinlang/?view=libraries"><b>📦 متجر الحزم</b></a> ·
<a href="./.changelog/CHANGELOG.md"><b>📝 التغييرات</b></a>
</p>

<br/>

<img src="./docs/assets/screenshots/hero.png" alt="لقطات من RinStudio" width="100%" />

</div>

---

<p align="center">
<a href="#-نظرة-عامة">✨ نظرة عامة</a> &nbsp;·&nbsp;
<a href="#-لقطات-من-rinstudio">📸 لقطات من RinStudio</a> &nbsp;·&nbsp;
<a href="#-مثال-سريع">⚡ مثال سريع</a> &nbsp;·&nbsp;
<a href="#-التثبيت-والتشغيل">🚀 التثبيت والتشغيل</a> &nbsp;·&nbsp;
<a href="#-اللغة-باختصار">🧩 اللغة باختصار</a> &nbsp;·&nbsp;
<a href="#-rinstudio-لأندرويد">📱 RinStudio لأندرويد</a> &nbsp;·&nbsp;
<a href="#-المكتبات-والحزم">📚 المكتبات والحزم</a> &nbsp;·&nbsp;
<a href="#-المعمارية">🏗️ المعمارية</a> &nbsp;·&nbsp;
<a href="#-بنية-المستودع">🗺️ بنية المستودع</a> &nbsp;·&nbsp;
<a href="#-التوثيق">📖 التوثيق</a> &nbsp;·&nbsp;
<a href="#-المساهمة">🤝 المساهمة</a> &nbsp;·&nbsp;
<a href="#-ci-وأدوات-المستودع">🤖 CI وأدوات المستودع</a> &nbsp;·&nbsp;
<a href="#-الترخيص">📄 الترخيص</a>
</p>

---

## ✨ نظرة عامة

<details open>
<summary><b>عرض / إخفاء</b></summary>

**Rin** لغة برمجة صغيرة: Lexer + Parser + Interpreter مكتوبة بـ C++17. **المحرّك نفسه** يُستخدم في كل مكان — لا نسخ مكرَّرة ولا محاكاة — لذلك يعطي البرنامج النتيجة نفسها أينما شغّلته:

| | المضيف | الاستخدام |
|:-:|---|---|
| 💻 | **أداة `rin`** | تشغيل الملفات، REPL، فحص نحوي، إنشاء المشاريع، إدارة الحزم |
| 📱 | **RinStudio** | بيئة تطوير كاملة على أندرويد: محرّر، مشاريع، معاينة واجهات، نشر حزم |
| 🌍 | **المتصفح** | تشغيل Rin عبر WebAssembly في [Playground](./docs/playground.html) |

**أبرز الميزات**

- بنية بسيطة: متغيّرات، دوال، `enum`، `match`، أنابيب `|>`، حاويات `@container` وكائنات `@Object`.
- لغة واجهات مدمجة (**Indsin**) بمعاينة حيّة.
- **82 مكتبة رسمية** جاهزة: بيانات، رياضيات، نصوص، شبكة، تشفير، ذكاء اصطناعي، وصناعة لغات.
- مدير حزم (RinPM) ومتجر حزم على الويب.
- أدوات ربط (bindings) لاستدعاء Rin من Python وNode.js وC.

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 📸 لقطات من RinStudio

<details open>
<summary><b>عرض / إخفاء</b></summary>

لقطات حقيقية من التطبيق (واجهة عربية وإنجليزية). كل اللقطات المفردة في [`docs/assets/screenshots`](./docs/assets/screenshots).

<details open>
<summary><b>✍️ المحرّر والطرفية</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-editor.png" alt="المحرّر" width="62%" /></div>

محرّر بتلوين نحوي وعدّاد أسطر وطيّ للكتل، وتحته **Rin Shell** (طرفية تفاعلية) وتبويب **التشغيلات** لسجل المخرجات.

</details>

<details>
<summary><b>🧭 القوائم</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-menus.png" alt="قوائم File / Edit / View / Run" width="100%" /></div>

`File` (ملف جديد، فتح، حفظ، مشاريع، مكتبات) · `Edit` (بحث واستبدال، نقل/تكرار الأسطر، إدراج مقتطف Rin) · `View` (الخط، الطيّ، المخطط Outline، الإعدادات) · `Run` (تشغيل، فحص الأقواس، معاينة حيّة، فحص وسوم الحاويات).

</details>

<details>
<summary><b>🗂️ المشاريع والملفات</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-projects.png" alt="المشاريع والملفات" width="100%" /></div>

إدارة المشاريع وملفاتها (`main.rin` · `container.rin` · `index.html` · `style.css`)، ومعاينة مشروع **HTML + Rin** يعمل مباشرةً: الواجهة من `index.html` والمنطق من ملف الحاوية.

</details>

<details>
<summary><b>🐙 تطبيق حقيقي: GitHub Explorer</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-github.png" alt="GitHub Explorer مكتوب بـ Rin" width="100%" /></div>

برنامج Rin من 310 أسطر يستدعي واجهة GitHub REST (`apiRegister` · `apiHeader` · `apiGet`) ويعرض الملف الشخصي والمستودعات والفروع والإصدارات وحدّ الاستعلام.

</details>

<details>
<summary><b>➕ مشروع جديد</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-new-project.png" alt="مشروع جديد" width="82%" /></div>

قوالب **الويب** (HTML + Rin، HTML + JS) وقوالب **Rin** (Free Project · Container · Table · UI · Illust)، مع قالب Indsin اختياري ومصمّم واجهة بمعاينة مباشرة (الألوان، نمط الأزرار، الخط، الشريط العلوي والجانبي).

</details>

<details>
<summary><b>📦 تصدير APK</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-apk.png" alt="تصدير APK" width="38%" /></div>

تصدير المشروع كتطبيق أندرويد: الاسم، ملف البداية، الأيقونة، شاشة البداية، والتوافق (الحد الأدنى API 35 / الهدف API 36).

</details>

<details>
<summary><b>🛒 المتجر والحزم</b></summary>


<div align="center"><img src="./docs/assets/screenshots/strip-store.png" alt="المتجر وتفاصيل الحزمة" width="70%" /></div>

متجر حزم Rin: بحث وتقييم وتثبيت/إدراج بنقرة، وصفحة تفاصيل لكل حزمة تعرض ملفاتها (`assets` · `examples` · `lib`).

</details>


<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## ⚡ مثال سريع

<details open>
<summary><b>عرض / إخفاء</b></summary>

```kotlin
@import "lib/math.og.rin";

fun greet(name) {
    return "مرحباً يا " + name + "!";
}

print greet("Rin");
print factorial(5);      // 120
```

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 🚀 التثبيت والتشغيل

<details open>
<summary><b>عرض / إخفاء</b></summary>

### 1. بناء أداة `rin` (Linux)

المتطلبات: `cmake` و`g++` و`zlib1g-dev`.

```bash
git clone https://github.com/dlof-lib/rinlang.git
cd rinlang/cli/linux
./build.sh
```

| المنصة | المسار | أمر البناء |
|---|---|---|
| Linux | [`cli/linux`](./cli/linux) | `./build.sh` |
| macOS | [`cli/macos`](./cli/macos) | `./build.sh` |
| Windows | [`cli/windows`](./cli/windows) | `build.bat` |
| الكل عبر `make` | الجذر | `make apk` · `make web` · `make desktop` · `make all` |

### 2. الاستخدام

```bash
rin -c 'print 1 + 1;'                  # تشغيل كود مباشر
rin run hello.rin                      # تشغيل ملف
rin check hello.rin                    # فحص نحوي بلا تشغيل
rin new myproj --template console      # مشروع جديد (rin.toml + src/main.rin)
```

<details>
<summary><b>🧰 كل الأوامر</b></summary>

<br/>

| الأمر | الغرض |
|---|---|
| `rin` | REPL تفاعلي (أو قراءة من stdin) |
| `rin run <file>` | تشغيل ملف بالمفسِّر (الطريقة الكاملة لأي برنامج) |
| `rin check <file>` | فحص نحوي بلا تشغيل |
| `rin test [dir]` | تشغيل ملفات اختبار `.rin` |
| `rin fmt <file> [--write]` | إعادة محاذاة المسافات البادئة |
| `rin new <name> --template <t>` | إنشاء مشروع جديد |
| `rin build <file> -o out` | تنفيذي أصلي عبر `rinc` — مجموعة فرعية من اللغة فقط |
| `rin doctor` | فحص بيئة التطوير |

> `rin build` يدعم حالياً الجزء الإجرائي الأساسي + `@container` البسيطة فقط؛ استخدم `rin run` لأي برنامج كامل. التفاصيل في [`getting-started.md`](./docs/getting-started.md).

</details>

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 🧩 اللغة باختصار

<details>
<summary><b>عرض / إخفاء</b></summary>

| المفهوم | مثال | التوثيق |
|---|---|---|
| متغيّرات ومصفوفات وقواميس | `let x = 5;` | [`variables.md`](./docs/variables.md) |
| شروط وحلقات | `if` · `when/otherwise` · `while` · `for` · `match` | [`control-flow.md`](./docs/control-flow.md) |
| دوال ولامبدا | `fun add(a, b) { return a + b; }` | [`functions.md`](./docs/functions.md) |
| قوائم اختيار | `enum` | [`enums.md`](./docs/enums.md) |
| أنابيب حسابية | `\|>` · `reckon` | [`pipelines.md`](./docs/pipelines.md) · [`RECKON.md`](./docs/RECKON.md) |
| حاويات وكائنات | `@container` · `@Object` | [`containers.md`](./docs/containers.md) · [`objects.md`](./docs/objects.md) |
| استيراد المكتبات | `@import "lib/x.og.rin";` | [`lib/README.md`](./lib/README.md) |
| واجهات (Indsin) | `@view` · `@element` · `@loop` | [`indsin.md`](./indsin.md) · [`RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) |

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 📱 RinStudio لأندرويد

<details>
<summary><b>عرض / إخفاء</b></summary>

بيئة التطوير الرسمية لـ Rin على أندرويد (`com.dlof.rinlang`)، مبنيّة على المحرّك نفسه عبر JNI.

| | الإصدار |
|---|---|
| الحد الأدنى | Android 15 (API 35) |
| الهدف | Android 16 (API 36) |

- ✍️ محرّر أصلي بتلوين نحوي، ومستكشف مشاريع.
- 👁️ معاينة حيّة للواجهات (Indsin) وتتبّع مسار التنفيذ (Flow / Pipeline).
- 📚 شاشة **المكتبات**: المدمجة + مكتبات مشروعك + حزم المجتمع.
- 📤 **RinPM**: انشر حزمتك من التطبيق فتظهر في [متجر الحزم](https://dlof-lib.github.io/rinlang/?view=libraries).
- 📦 تصدير المشروع كتطبيق APK موقَّع.

التفاصيل: [`docs/android.md`](./docs/android.md) · سجل إصدارات أندرويد في [`.changelog/CHANGELOG.md`](./.changelog/CHANGELOG.md#android16).

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 📚 المكتبات والحزم

<details>
<summary><b>عرض / إخفاء</b></summary>

مجلد [`lib/`](./lib) يضم **82 مكتبة رسمية** بصيغة `.og.rin`، بترويسة موحَّدة (وصف · `@version` · `@category` · `@prefix` · الاستيراد):

| المجال | أمثلة |
|---|---|
| 🗂️ البيانات والمجموعات | `data` · `collections` · `stack` · `queue` · `tree` · `graph` |
| 🔤 النصوص والصيغ | `strings` · `jsonkit` · `csv` · `nlpkit` · `bob` · `ringo` |
| 📐 الرياضيات والعلوم | `math` · `matrix` · `geometry` · `units` · `physics` |
| ✅ التحقق والوظيفية | `validate` · `functional` · `requirekit` |
| 🖥️ النظام والويب | `syskit` · `httpkit` · `urlkit` · `router` · `logger` · `rinzip` |
| 🔁 الحلقات والتفاعل | `loopkit` · `gridkit` · `movingmask` · `behaviorkit` |
| 🎨 الواجهات | `colors` · `layout` · `rinxg` · `relyRIN` · `previewkit` · `indsinweb` · `boat` |
| 🔐 كلمات المرور والتشفير | `passkit` · `passkitcrypt` · `passkitdb` · `passkitlang` |
| 🤖 الذكاء الاصطناعي والبيانات | `rindf` · `rinml` · `rinai` (انظر [`docs/rinml.md`](./docs/rinml.md)) |
| 🧬 صناعة اللغات | `langkit` · `lexkit` · `parsekit` · `astwalk` · `oglang` |

```kotlin
@import "lib/strings.og.rin" as strx;     // كحاوية باسم مستعار
@import "lib/data.og.rin";                // أو دمج مباشر
```

الفهرس الكامل بالأوصاف والأحجام: **[`lib/README.md`](./lib/README.md)**.

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 🏗️ المعمارية

<details>
<summary><b>عرض / إخفاء</b></summary>

```text
            ┌──────────────────────────────┐
            │   Lexer → Parser → AST       │
            │        Interpreter           │   app/src/main/cpp  (C++17)
            └──────────────┬───────────────┘
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   rin CLI           RinStudio (JNI)     WebAssembly
 linux·macos·win        Android          المتصفح / Pages
```

مسار البناء والاختبار الرئيسي (`rin__apk.yml`): **فحص مبكر ← اختبارات المحرّك ‖ بناء APK موقَّع ← Release**.

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 🗺️ بنية المستودع

<details>
<summary><b>عرض / إخفاء</b></summary>

```text
rinlang/
├── README.md · indsin.md · LICENSE · VERSION
├── .changelog/      سجل التغييرات المدمج (مجلد مخفي)
│
├── app/             RinStudio (أندرويد) + المحرّك في app/src/main/cpp
├── cli/             أداة rin لـ linux · macos · windows
├── compiler/        rinc — المترجم الأصلي
├── gorin/           محرّك Go مساعد
├── pitok/           لغة واجهات مستقلة (تجريبي)
│
├── lib/             المكتبات الرسمية (.og.rin) + الفهرس
├── examples/        أمثلة وقوالب
├── templates/ · content-packs/   قوالب المشاريع وحزم المحتوى واللغات
├── docs/            التوثيق الكامل
│
├── web/ · user-site/   الموقع + متجر الحزم + WebAssembly
├── bindings/        Python · Node.js · C
├── src/ · syntaxes/ · editors/ · linguist-submission/   دعم المحرّرات
│
├── tests/           الاختبارات (tools · verification · diagnostics · vm · bench · web)
├── tools/ · scripts/   أدوات التشغيل والبناء والنشر
├── signing/ · packaging/ · schemas/
└── .github/         سير عمل CI
```

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 📖 التوثيق

<details>
<summary><b>عرض / إخفاء</b></summary>

| | |
|---|---|
| 🚀 **البداية** | [`getting-started.md`](./docs/getting-started.md) → [`syntax.md`](./docs/syntax.md) → [`variables.md`](./docs/variables.md) |
| 📘 **المرجع** | [`language-reference.md`](./docs/language-reference.md) — خريطة ترابط كل المفاهيم |
| 🧱 **بيئة التشغيل** | [`standard-library.md`](./docs/standard-library.md) · [`errors.md`](./docs/errors.md) · [`storage.md`](./docs/storage.md) · [`http.md`](./docs/http.md) |
| 🎛️ **الواجهات** | [`indsin.md`](./indsin.md) · [`RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) · [`events-effects.md`](./docs/events-effects.md) |
| 🔌 **API والتكامل** | [`api.md`](./docs/api.md) · [`bindings/`](./bindings/README.md) · [`android.md`](./docs/android.md) |

الفهرس الكامل: [`docs/README.md`](./docs/README.md)

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 🤝 المساهمة

<details>
<summary><b>عرض / إخفاء</b></summary>

1. ابنِ الأداة وشغّل الاختبارات: `rin test tests`.
2. **مكتبة جديدة؟** أضف `lib/name.og.rin` بترويسة موحّدة تبدأ بسطر `// lib/name.og.rin — الوصف`، ثم شغّل workflow **Cleanup & Organize Project** ليحدّث [`lib/README.md`](./lib/README.md).
3. سجّل أي تغيير مهم في [`CHANGELOG.md`](./.changelog/CHANGELOG.md).
4. افتح Pull Request بوصف واضح للتغيير والاختبارات التي أضفتها.

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 🤖 CI وأدوات المستودع

<details>
<summary><b>عرض / إخفاء</b></summary>

| Workflow | الغرض |
|---|---|
| [`rin__apk.yml`](.github/workflows/rin__apk.yml) | الخط الرئيسي: فحص مبكر ← اختبارات المحرّك ‖ بناء APK موقَّع ← Release |
| [`build-android-versions.yml`](.github/workflows/build-android-versions.yml) | حزمة APK لكل إصدار أندرويد (على الوسوم `v*`) |
| `linux-cli` · `macos-cli` · `windows-cli` | بناء واختبار أداة `rin` لكل منصة |
| [`pages.yml`](.github/workflows/pages.yml) | بناء الموقع + WASM ونشره على GitHub Pages |
| [`build.yml`](.github/workflows/build.yml) | بناء إضافة Visual Studio (`RinLangVSSDK`) |
| [`cleanup-project.yml`](.github/workflows/cleanup-project.yml) | تنظيف المستودع، دمج سجلات التغييرات، توليد فهرس المكتبات (يدوي) |
| [`highlight-rin.yml`](.github/workflows/highlight-rin.yml) | تلوين كود Rin على GitHub (يدوي) |

**تلوين Rin على GitHub:** لغة Rin غير معروفة لـ [linguist](https://github.com/github-linguist/linguist) بعد (إضافتها تتطلب انتشار ≥2000 ملف `.rin` علناً — انظر [`linguist-submission/`](./linguist-submission/README.md)). لذلك يضبط `highlight-rin.yml` قاعدة `linguist-language` في `.gitattributes` فتظهر ملفات `.rin` و`.og.rin` بتلوين Kotlin (الأقرب نحوياً). للتلوين الكامل في المحرّرات استخدم [`syntaxes/rin.tmLanguage.json`](./syntaxes/rin.tmLanguage.json).

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>

## 📄 الترخيص

<details>
<summary><b>عرض / إخفاء</b></summary>

[MIT](./LICENSE) © 2026 Rin Project Contributors.
اسم Rin وشعارها وهويتها البصرية غير مرخّصة للاستخدام المطلق تلقائياً؛ راجع [`COPYRIGHT`](./COPYRIGHT) و[`NOTICE`](./NOTICE).

<div align="center">
<sub>Rin 1.0.0 · Edition 2026</sub>
</div>

<div align="left"><a href="#rin">⬆ للأعلى</a></div>

</details>
