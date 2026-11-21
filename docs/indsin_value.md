# indsin::Value — القيم والأنواع

طبقتان:

| الملف | الدور |
|---|---|
| `indsin/rin_indsin_eval.h` | `Value` نفسه: رقم أو نص + مساعدات (`isTruthy` · `asBool` · `asInt` · `asMeasure` · `looseEquals` · `compare` · `formatNumber`) |
| `indsin/rin_indsin_value.h` | **نظام الأنواع** (`namespace indsin::vx`): ألوان · أزمنة · زوايا · أطوال · قوائم · تعدادات · استيفاء · تحقق · تنسيق |

الملف الثاني إضافي بحت: لا يغيّر `Value` ولا المقيِّم، ويُضمَّن عند الحاجة فقط.

## Value (الأساس)

```cpp
Value::boolean(true)            // "true" (نص — لا Kind جديد)
v.isTruthy()                    // قاعدة Rin: رقم≠0 · نص ليس "" ولا "false"
v.asBool(def)                   // متسامح: true/false/1/0/yes/no/on/off
v.asMeasure()                   // "12px" → {12,"px"} · "50%" · "-1.5em"
v.asInt()                       // تقريب + حماية من الفيض
Value::formatNumber(1234567)    // "1234567" (لا 1.23457e+06)
a.looseEquals(b) / a.compare(b) // "3"==3 · "10" > "9" رقمياً
```

## vx — الأنواع

كل دالة تحليل تعيد `Result<T>` فيه `ok` و`value` و`error` (سبب الفشل بالنص)، أو تُستعمل `valueOr(def)`.

| المجال | الدوال |
|---|---|
| زمن | `toDurationMs` — `ms · s · min · h` (رقم بلا وحدة = ms) |
| زاوية | `toAngleDeg` — `deg · rad · grad · turn` · `normalizeDeg` |
| طول | `toPx(v, LengthContext)` — `px dp pt pc in cm mm em rem % vw vh vmin vmax` |
| لون | `parseColor` — `#rgb #rgba #rrggbb #rrggbbaa rgb() rgba() hsl() hsla()` + 20 اسماً + `transparent` |
| حسابات لون | `mix · lighten · darken · saturate · rotateHue · withAlpha · toHex · toCssRgb` |
| تباين | `relativeLuminance · contrastRatio · wcagLevel(ratio, large) · readableOn(bg)` |
| قوائم | `splitList · toList · joinList` (الفاصلة داخل `()` والاقتباس لا تفصل) |
| تعداد | `toEnum(v, options)` — `Center_Left == center-left` |
| استنتاج | `inferType(v)` → `NUMBER BOOL COLOR DURATION ANGLE LENGTH LIST TEXT EMPTY` |
| استيفاء | `lerp(a, b, t)` — أرقام · أطوال بنفس الوحدة · ألوان؛ غير ذلك يقفز عند 0.5 |
| تقييد | `clampValue(v, lo, hi)` يحفظ الوحدة |
| تنسيق | `formatFixed · formatPercent · formatCompact (1.2K) · formatBytes · formatDuration` |

### Typed

```cpp
Typed t(Value::txt("1.5s"));
t.type();            // Type::DURATION
t.durationMs();      // Result<double> = 1500
t.color(); t.px(ctx); t.enumOf({...}); t.list(); t.toBool(); t.toInt();
```

### تحقق مخطّطي

```cpp
std::vector<Spec> specs = {
  spec("color",    Type::COLOR).req(),
  spec("duration", Type::DURATION).range(0, 5000),     // ms
  spec("size",     Type::INT).atLeast(1),
  spec("align",    Type::ENUM).oneOf({"start","center","end"}).withDefault("start"),
};
auto issues = validate(attrs, specs);          // {key, message, error}
auto full   = withDefaults(attrs, specs);      // يملأ الافتراضيات
```

المفاتيح غير المعرّفة تُبلَّغ تحذيراً (`error=false`)؛ مرّر `warnUnknown=false` لإيقافه.

## ملاحظات

- أسماء الألوان محجوزة في `inferType`: `"red"` لون. ثبّت النوع بـ `Spec` حين يكون نصاً.
- `%` تحتاج `LengthContext::parentPx` و`vw/vh` تحتاج حجم الشاشة، وإلا تفشل بسبب واضح (لا تخمين).
- الاختبارات: `tests/indsin_value/value_test.cpp` و`value_ext_test.cpp`.

```bash
g++ -std=c++20 -Wall -Wextra -I app/src/main/cpp tests/indsin_value/value_ext_test.cpp -o /tmp/vx && /tmp/vx
```

## التدقيق التلقائي (`rin indsin check`)

`rin_indsin_audit.h` يستعمل `vx` لفحص قيم الخصائص، و`check` صار يطبع كل ملاحظة: `ملف:سطر: severity[CODE]: رسالة` مع تلميح الإصلاح.

| الكود | الشدة | يعني |
|---|---|---|
| `VALUE-001` | warning | حرفية لون (`#…` أو `rgb()/hsl()`) غير صالحة، مثل `#12345` أو `rgb(1,2)` |
| `VALUE-002` | warning | قيمة زمن غير صالحة (`text_duration="soon"`) أو `*_duration` سالبة |
| `VALUE-003` | warning | سمة تقرأ ميلي ثانية رقماً صرفاً (`type_duration` · `type_delay` · `type_hold` · `type_erase_duration` · `type_cursor_blink`) كُتبت بوحدة (`2s`)، فتُتجاهل بصمت |

لا إنذارات كاذبة عمداً: أسماء الثيم (`text_muted`) والتدرجات (`bg="#a,#b,135"`) وروابط `url()` ورموز الزمن `fast|normal|slow` تُترك كما هي.
الاختبار: `bash tests/indsin_value/run.sh`.

## إصلاح في سكربت البناء

`scripts/build_indsin.sh` كان يقارن اسم الملف بنص خاطئ، فلا يعيد ترجمة `rin_indsin_c_api` عند تغيّر ترويسات `indsin/*.h`، وتبقى الأداة تعمل بنسخة قديمة. صُحّحت المقارنة.
