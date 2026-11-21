# container_storage.md — `@container.storage` (تخزين محلي في المتصفح)

حاوية بيانات تُحفَظ قيمها داخل المتصفح فتبقى **بعد إغلاق الصفحة وإعادة فتحها** (إعدادات الوضع الليلي، محتويات سلة التسوّق،
عدّاد الزيارات…). تعمل في صفحات [`indsin2web`](./indsin2web.md) و[`RinHTML`](#rinhtml-الحالة-الدائمة-persist). اختصارها `@storage=name … .end/storage`.

```kotlin
@container.storage=prefs
    let dark = false;        // كل let حقل يُحفَظ ويُستعاد
    let name = "";
    let visits = 0;
    let cart = [];
    let version = 1;         // إعداد (انظر الجدول): لا يصير حقلاً
.end/container.storage

prefs.visits = prefs.visits + 1;                       // كتابة ⇒ حفظ تلقائي + تحديث الواجهة
fun addItem() { push(prefs.cart, "منتج"); }            // push داخل مصفوفة تُحفَظ أيضاً (التفاعل عميق)
```
```xml
<Column#Root bg={prefs.dark ? "#0F172A" : "#FFFFFF"}>
  <Switch#Dark checked={prefs.dark} />                  <!-- ربط ثنائي الاتجاه: يكتب في prefs.dark ويقرؤه -->
  <Input#Name value={prefs.name} />
  <Text text={"في السلة: " + len(prefs.cart)} />
  <Button onTap={prefs.reset("cart")}>أفرغ السلة</Button>
</Column>
```
مثال كامل: `examples/samples/indsin_storage_fetch.rin` — `build/indsin2web examples/samples/indsin_storage_fetch.rin -o demo.html`.

## حقول الإعداد (اختيارية)
| الحقل | المعنى |
|---|---|
| `store` | `"local"` (الافتراضي: يبقى بعد إغلاق المتصفح) · `"session"` (حتى إغلاق التبويب) · `"memory"` (بلا حفظ) |
| `storeKey` | مفتاح التخزين (الافتراضي اسم الحاوية) — يُخزَّن تحت `rin:<storeKey>` |
| `version` | رقم الإصدار (الافتراضي 1). عند تغييره تُسقَط البيانات القديمة ويبدأ التطبيق من القيم الافتراضية (هجرة آمنة) |
| `ttl` | عمر البيانات بالميلي ثانية (`ttl = 7d`): تنتهي بعد آخر حفظ فتعود القيم الافتراضية |
| `sync` | `true` (الافتراضي): تعديل في تبويب يظهر فوراً في بقية التبويبات المفتوحة (حدث `storage`) |

## الدوال
`prefs.reset()` كل الحقول إلى افتراضياتها · `prefs.reset("cart")` حقل واحد · `prefs.clear()` يمسح المحفوظ من المتصفح ·
`prefs.save()` حفظ فوري · `prefs.has("k")` · `prefs.keys()` · `prefs.info()` ⇒ `{key, area, restored, expired, error, savedAt}`.
وتعمل أيضاً `container.get("prefs", "dark")` و`container.set("prefs", "dark", true)` كما في المفسّر الأصلي.

## قواعد وضمانات
- **حاوية بيانات نقية**: `let` فقط (`fun` وخطّافات الحياة مرفوضة بخطأ `E0014` كبقية حاويات البيانات).
- **النوع محروس**: عند الاستعادة تُقبَل القيمة المحفوظة فقط إن كان نوعها مثل نوع القيمة الافتراضية (رقم/نص/منطقي/مصفوفة/كائن)؛
  وما ليس معرَّفاً في الحاوية يُتجاهَل. كتابة حقل غير معرَّف (`prefs.nmae = …`) تُطبع كتحذير ولا تُنشئ حقلاً.
- **الحفظ مجمَّع**: عدة كتابات متتالية ⇒ كتابة واحدة في `localStorage`.
- **الفشل آمن**: وضع التصفح الخاص أو امتلاء الحصّة أو حظر التخزين ⇒ تعمل الصفحة بالذاكرة فقط ويظهر السبب في `prefs.info().error`.
- **لا تخزّن أسراراً**: `localStorage` مقروء لأي سكربت في نفس الأصل. (الرموز/كلمات المرور لا تُحفَظ هنا.)
- في **المفسّر الأصلي** (CLI/أندرويد) تعمل `container.storage` كحاوية بيانات عادية في الذاكرة (`kindOf` ⇒ `container.storage`)؛ الحفظ الدائم ميزة المتصفح.

## RinHTML: الحالة الدائمة (`persist`)
في صفحات `<rin-app>` (محرك WASM) تُحفَظ المتغيّرات العامة المسمّاة فقط، وتحتاج الصلاحية `storage`:
```html
<rin-app src="main.rin" allow="dom storage" persist="theme count"></rin-app>
```
الاستعادة قبل أول رسم، وبنفس النوع فقط؛ `persist-key="…"` يغيّر المفتاح (الافتراضي `rinhtml:<id أو src>`)، و`persist-store="session"` لتخزين الجلسة.
بلا `allow="storage"` لا يُحفَظ شيء (آمن افتراضياً).

## الاختبار
`bash tests/indsin2web/run.sh` — يشغّل Chromium حقيقياً: بقاء الزيارات/الوضع الليلي/الاسم/السلة بعد إغلاق الصفحة، `reset`، مزامنة التبويبات،
تغيير `version`، وتعطّل التخزين. و`node tests/rinhtml/browser_storage_fetch.js` لـ `persist`.
