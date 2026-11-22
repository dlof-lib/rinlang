# EXTRA13 — خمس إضافات بنمط Rin

مستلهَمة من أفكار لغات أخرى لكن بأسماء وصيغ Rin (camelCase، حقول مسمّاة، عائلات `utf8*` / `findIf`). المصدر: `rin_extra_natives13.cpp`.

## 1) `bytes.*` — بايتات بمواصفة مسمّاة (لحاويات ثنائية مثل `.rcl`/zip)
```kotlin
let spec = "magic:str4 ver:u16le size:u32be ratio:f32 pad2 name:str*";
let raw  = bytes.encode(spec, "RIN1", 3, 258, 1.5, "hello");   // أو بقاموس: bytes.encode(spec, {magic: "RIN1", ...})
let head = bytes.decode(spec, raw);                             // {magic, ver, size, ratio, name, _end}
print bytes.size("a:u8 b:u32be");                               // 5 (ثابت؛ str* يجعله غير ثابت)
```
الأنواع: `u8 i8 u16 i16 u32 i32 u64 i64 f32 f64` (+ `le` الافتراضي أو `be` لغير 8 بت) · `strN` (يُحشى بـ NUL ويُقصّ عند القراءة) · `str*` (الباقي، آخر حقل) · `hexN` · `padN` · تكرار `u8*4` (يُقرأ مصفوفة).
حقل بلا اسم يُسمّى `_1` `_2`… · `_end` = الموضع بعد آخر حقل. القيم خارج المدى أو البيانات القصيرة ترمي خطأً واضحاً.
ملاحظة: الأعداد 64-بت تمرّ عبر `number` (دقة حتى 2^53).

## 2) `dur.*` — مدد زمنية (رقم بالميلي ثانية، كبقية Rin)
`dur.parse("1h30m")` (وحدات `w d h min m s sec ms` · ISO مثل `PT1H30M` · رقم بلا وحدة = ms) · `dur.of({h:1,m:5})` · `dur.parts(ms)` · `dur.text(ms)` → `"1h 30m"` ·
`dur.fmt(ms, "{hh}:{mm}:{ss}")` (`{d} {h} {m} {s} {ms} {ms3} {totalH} {totalM} {totalS} {sign}`) · `dur.add` · `dur.sub` · `dur.scale` · `dur.abs` · `dur.cmp` · `dur.valid`.
كل دالة تقبل رقماً أو نصاً مثل `"90s"`.

## 3) `firstOf` · `lastOf` · `firstKey` · `lastKey` · `findKeyIf`
تعمل على مصفوفة/قاموس/Set. `firstOf([], "x")` ← `"x"`. `findKeyIf(items, fn)` يعيد **مفتاح/فهرس** أول عنصر يحقق الشرط (`findIf` تعيد العنصر)؛ إن كانت `fn` تقبل وسيطين استلمت `(قيمة، مفتاح)`.

## 4) `utf8Trim` · `utf8TrimStart` · `utf8TrimEnd` · `utf8CapFirst` · `utf8LowFirst` · `utf8Pad`
قصّ يفهم المسافات الأحادية (NBSP، U+3000…) وعلامات الاتجاه `U+200E/200F/061C` المنتشرة في النص العربي. `utf8Pad(s, n, حشو=" ", "end"|"start"|"both")` يعدّ **الأحرف** لا البايتات. التحويل الحرفي يغطي اللاتينية/اليونانية/السيريلية (والعربية بلا حالة أحرف).

## 5) `state fixed` — حالة تُهيَّأ مرة واحدة داخل الحاوية
```kotlin
@container=Cfg
    state fixed version = 2;      // قراءة فقط بعد الإعلان
    state hits = 0;               // قابلة للتغيير وتُطلق on update كالمعتاد
.end/container
```
أي إسناد لاحق (من دالة الحاوية أو عبر `setField`/الروابط) يرمي `E0003`. `fixed` كلمة سياقية: `state fixed = 1;` تبقى حقلاً عادياً اسمه `fixed`.

الاختبار: `tests/print/extra13.rin` (لا يجب أن يظهر `FAIL`).
