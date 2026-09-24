<div align="center">

<img src="./docs/assets/brand/icon-192.png" alt="Rin" width="112" height="112" />

# Rin

### لغة برمجة صغيرة بمحرّك C++17 واحد — من سطر الأوامر، إلى الهاتف، إلى المتصفح

<sub>A small programming language · one C++17 engine · CLI, Android IDE (RinStudio) and WebAssembly</sub>

<p>
<img alt="Version" src="https://img.shields.io/badge/Rin-1.0.0-1DB143?style=for-the-badge" />
<img alt="Engine" src="https://img.shields.io/badge/engine-C%2B%2B17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" />
<img alt="Android" src="https://img.shields.io/badge/RinStudio-Android%207.0%2B-3DDC84?style=for-the-badge&logo=android&logoColor=white" />
<img alt="WASM" src="https://img.shields.io/badge/Web-WebAssembly-654FF0?style=for-the-badge&logo=webassembly&logoColor=white" />
<img alt="License" src="https://img.shields.io/badge/license-MIT-4E4E4E?style=for-the-badge" />
</p>

<p>
<a href="https://dlof-lib.github.io/rinlang/"><b>🌐 الموقع</b></a> ·
<a href="./docs/getting-started.md"><b>🚀 ابدأ</b></a> ·
<a href="./docs/language-reference.md"><b>📖 المرجع</b></a> ·
<a href="./lib/README.md"><b>📚 المكتبات</b></a> ·
<a href="https://dlof-lib.github.io/rinlang/?view=libraries"><b>📦 متجر الحزم</b></a>
</p>

</div>

---

## ✨ لمحة

**Rin** لغة صغيرة وواضحة: Lexer + Parser + Interpreter مكتوبة بـ C++17، ونفس المحرّك بالضبط يعمل في ثلاثة أماكن — لا نسخ مكرَّرة ولا محاكاة:

| | المضيف | الاستخدام |
|:-:|---|---|
| 💻 | **`rin` CLI** | تشغيل الملفات، REPL، فحص نحوي، مشاريع جديدة |
| 📱 | **RinStudio** | بيئة تطوير كاملة على أندرويد: محرّر، مشاريع، معاينة واجهات، نشر حزم |
| 🌍 | **المتصفح** | تشغيل Rin عبر WebAssembly ([playground](./docs/playground.html)) |

```rin
@import "lib/math.og.rin";

fun greet(name) {
    return "مرحباً يا " + name + "!";
}

print greet("Rin");
print factorial(5);      // 120
```

---

## 🚀 ابدأ في دقيقة

```bash
git clone https://github.com/dlof-lib/rinlang.git
cd rinlang/cli/linux && ./build.sh        # يحتاج cmake + g++ + zlib1g-dev
```

```bash
rin -c 'print 1 + 1;'      # تشغيل كود مباشر
rin run hello.rin          # تشغيل ملف
rin check hello.rin        # فحص نحوي بلا تشغيل
rin new myproj --template console   # مشروع جديد (rin.toml + src/main.rin)
```

<details>
<summary><b>🧰 بقية الأوامر ومنصات البناء</b></summary>

<br/>

| الأمر | الغرض |
|---|---|
| `rin` | REPL تفاعلي (أو قراءة من stdin) |
| `rin test [dir]` | تشغيل ملفات اختبار `.rin` |
| `rin fmt <file> [--write]` | إعادة محاذاة المسافات البادئة |
| `rin build <file> -o out` | تنفيذي أصلي عبر `rinc` — مجموعة فرعية من اللغة فقط |
| `rin doctor` | فحص بيئة التطوير |

| المنصة | المسار |
|---|---|
| Linux | [`cli/linux`](./cli/linux) — `./build.sh` |
| macOS | [`cli/macos`](./cli/macos) — `./build.sh` |
| Windows | [`cli/windows`](./cli/windows) — `build.bat` |
| الكل عبر `make` | `make apk` · `make web` · `make desktop` · `make all` |

> `rin run` (المفسِّر) هو الطريقة الكاملة لتشغيل أي برنامج Rin؛ أمّا `rin build` فيدعم حالياً الجزء الإجرائي الأساسي + `@container` البسيطة فقط. التفاصيل في [`getting-started.md`](./docs/getting-started.md).

</details>

---

## 🧩 اللغة في نظرة

| المفهوم | مثال | التوثيق |
|---|---|---|
| متغيّرات ومصفوفات وقواميس | `let x = 5;` | [`variables.md`](./docs/variables.md) |
| شروط وحلقات | `if` · `when/otherwise` · `while` · `for` · `match` | [`control-flow.md`](./docs/control-flow.md) |
| دوال ولامبدا | `fun add(a, b) { return a + b; }` | [`functions.md`](./docs/functions.md) |
| قوائم اختيار | `enum` | [`enums.md`](./docs/enums.md) |
| أنابيب حسابية | `\|>` · `reckon` | [`pipelines.md`](./docs/pipelines.md) · [`RECKON.md`](./docs/RECKON.md) |
| حاويات وكائنات | `@container` · `@Object` | [`containers.md`](./docs/containers.md) · [`objects.md`](./docs/objects.md) |
| استيراد المكتبات | `@import "lib/x.og.rin";` | [`lib/README.md`](./lib/README.md) |
| واجهات (Indsin) | `@view` · `@element` · `@loop` | [`indsin.md`](./docs/indsin.md) · [`RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) |

---

## 📱 RinStudio

بيئة التطوير الرسمية لـ Rin على أندرويد (`com.dlof.rinlang`) — مبنيّة على نفس المحرّك عبر JNI:

- ✍️ محرّر أصلي بتلوين نحوي، ومستكشف مشاريع وألبومات.
- 👁️ معاينة حيّة للواجهات (Indsin) وتتبّع مسار التنفيذ (Flow / Pipeline).
- 📚 شاشة **المكتبات**: المكتبات المدمجة + مكتبات مشروعك + حزم المجتمع.
- 📤 **RinPM**: انشر حزمتك من التطبيق فتظهر تلقائياً في [متجر الحزم](https://dlof-lib.github.io/rinlang/?view=libraries).
- 📦 تصدير المشروع كتطبيق APK موقَّع.

التفاصيل: [`docs/android.md`](./docs/android.md)

---

## 📚 المكتبات والحزم

مجلد [`lib/`](./lib) يضم **61 مكتبة رسمية** بصيغة `.og.rin`، مرتّبة في 8 مجالات، بترويسة موحَّدة (وصف · `@version` · `@category` · `@prefix` · الاستيراد):

| المجال | أمثلة |
|---|---|
| 🗂️ البيانات والمجموعات | `data` · `collections` · `stack` · `queue` · `tree` · `graph` |
| 🔤 النصوص والصيغ | `strings` · `jsonkit` · `csv` · `nlpkit` · `bob` · `ringo` |
| 📐 الرياضيات والعلوم | `math` · `matrix` · `geometry` · `units` · `physics` |
| ✅ التحقق والوظيفية | `validate` · `functional` · `requirekit` |
| 🖥️ النظام والويب | `syskit` · `httpkit` · `urlkit` · `router` · `logger` · `rinzip` |
| 🔁 الحلقات والتفاعل | `loopkit` · `gridkit` · `movingmask` · `behaviorkit` |
| 🎨 الواجهات | `colors` · `layout` · `rinxg` · `relyRIN` · `boat` |
| 🧬 صناعة اللغات | `langkit` · `lexkit` · `parsekit` · `astwalk` · `oglang` |

```rin
@import "lib/strings.og.rin" as strx;      // كحاوية باسم مستعار
@import "lib/data.og.rin";                 // أو دمج مباشر
```

👉 الفهرس الكامل بالأوصاف والدوال والأحجام: **[`lib/README.md`](./lib/README.md)** (يُولَّد آلياً بـ `scripts/rin_lib_tool.py`).

---

## 🗺️ بنية المستودع

```text
rinlang/
├── 📖 README.md · CHANGELOG.md · LICENSE · VERSION
│
├── 📱 app/            RinStudio (أندرويد) + محرّك C++ في app/src/main/cpp
├── 💻 cli/            أداة rin لـ linux · macos · windows
├── ⚙️ compiler/       rinc — المترجم الأصلي
├── 🎨 pitok/          لغة واجهات مستقلة (تجريبي: Lexer + Parser)
│
├── 📚 lib/            مكتبات Rin الرسمية (.og.rin) + فهرسها
├── 🧪 examples/       أمثلة وقوالب (samples · customlang · indsin_templates …)
├── 📄 docs/           التوثيق الكامل (+ docs/dev: ملاحظات التطوير الداخلية)
│
├── 🌍 web/            موقع Rin + متجر المكتبات + WebAssembly
├── 🔗 bindings/       استدعاء Rin من Python · Node.js · C
├── ✏️ src/ · syntaxes/ · linguist-submission/   دعم المحرّرات (Visual Studio · TextMate · Linguist)
│
├── ✅ tests/          الاختبارات (tools · diagnostics · web · verification)
├── 🛠️ tools/ · scripts/   أدوات التشغيل والبناء والنشر
└── 🤖 .github/        سير عمل CI (بناء APK · CLI · صفحات الموقع)
```

---

## 📖 التوثيق

| | |
|---|---|
| 🚀 **البداية** | [`getting-started.md`](./docs/getting-started.md) → [`syntax.md`](./docs/syntax.md) → [`variables.md`](./docs/variables.md) |
| 📘 **المرجع** | [`language-reference.md`](./docs/language-reference.md) — خريطة ترابط كل المفاهيم |
| 🧱 **بيئة التشغيل** | [`standard-library.md`](./docs/standard-library.md) · [`errors.md`](./docs/errors.md) · [`storage.md`](./docs/storage.md) · [`http.md`](./docs/http.md) |
| 🎛️ **الواجهات** | [`indsin.md`](./docs/indsin.md) · [`RIN_ELEMENTS.md`](./docs/RIN_ELEMENTS.md) · [`events-effects.md`](./docs/events-effects.md) |
| 🔌 **API والتكامل** | [`api.md`](./docs/api.md) · [`bindings/`](./bindings/README.md) · [`android.md`](./docs/android.md) |

الفهرس الكامل لكل الصفحات: [`docs/README.md`](./docs/README.md)

---

## 🤝 المساهمة

1. ابنِ الأداة وشغّل الاختبارات في [`tests/`](./tests).
2. مكتبة جديدة؟ أضف `lib/name.og.rin` بالترويسة الموحّدة، سجِّلها في `scripts/rin_lib_tool.py`، ثم:
   ```bash
   python3 scripts/rin_lib_tool.py format && python3 scripts/rin_lib_tool.py check
   ```
3. ملاحظات التطوير الداخلية في [`docs/dev/`](./docs/dev).

---

## 📄 الترخيص

[MIT](./LICENSE) © 2026 Rin Project Contributors — اسم Rin وشعارها وهويتها البصرية غير مرخّصة للاستخدام المطلق تلقائياً؛ راجع [`COPYRIGHT`](./COPYRIGHT) و[`NOTICE`](./NOTICE).

<div align="center">
<sub>صُنعت بـ 💚 — Rin 1.0.0 · Edition 2026</sub>
</div>
