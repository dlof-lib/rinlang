# حاويات الويب — تخزين محلي وجلب في الخلفية

## لغة Rin
- حاوية جديدة `@container.storage=name … .end/container.storage` (اختصار `@storage=`): `ContainerKind::STORAGE`، حاوية بيانات نقية (`let` فقط).
  في المفسّر الأصلي تعمل كحاوية بيانات عادية (`kindOf` ⇒ `container.storage`)؛ الاختبار: `tests/print/container_storage.rin`.

## indsin2web (Indsin → HTML)
- `container.storage` ⇒ `IN.storage`: حفظ تلقائي في `localStorage`/`sessionStorage` واستعادة عند الفتح، تفاعل عميق (`push` داخل المصفوفات)،
  `version` للهجرة و`ttl` للصلاحية، مزامنة التبويبات، حراسة النوع عند الاستعادة، وتدهور آمن عند تعطّل التخزين.
- `container.api` مع `baseUrl` ⇒ `IN.api`: جلب في الخلفية (`every` دوري يتوقف والتبويب مخفي، `debounce`، «الأحدث يربح» بإلغاء الطلب السابق،
  `retry/backoff/timeout/cache/pick/persist`) يملأ `data/status/error/updated/loading`؛ آخر `data` ناجحة تبقى عند الفشل.
  بلا `baseUrl` تبقى محاكاة `route` كما كانت.
- ربط ثنائي الاتجاه لحقول الحاويات (`value={prefs.name}`, `checked={prefs.dark}`)، واستدعاءات `feed.get(...)` و`callApi(...)` و`container.get/set`، ودالة `removeAt`.
- التحديث جزئي بطبيعته: لا يُكتب في الـ DOM إلا ما تغيّرت قيمته (يتحقق منه الاختبار بـ MutationObserver).

## RinHTML (`web/rinhtml/rinhtml.js`)
- `persist="a b"` (صلاحية `storage`) لحفظ متغيّرات Rin العامة، و`rin-fetch`/`rin-into`/`rin-pick`/`rin-every`/`rin-fetch-on`/`rin-debounce`/`rin-error`/`rin-each` (صلاحية `fetch`).
  `_render` لا يكتب إلا ما تغيّر. التعريفات في `rinhtml.d.ts` (`getData`, خيارات persist).

## أدوات ومحرّر
- مثال `examples/samples/indsin_storage_fetch.rin`، اختبارات `tests/indsin2web/browser_storage_fetch.js` و`tests/rinhtml/browser_storage_fetch.js` (ضمن `tests/indsin2web/run.sh`).
- تلوين `storage` في `syntaxes/rin.tmLanguage.json` وإضافة VS Code، ومقتطفات `container.storage`/`container.fetch` (VS Code وVisual Studio).
- توثيق: `docs/container_storage.md` · `docs/container_fetch.md` · أقسام في `indsin2web.md` و`containers.md` و`api_container.md`.

## قوائم ديناميكية في الويب
- `<<each xs as p, i>>` على قائمة غير حرفية (مثل نتيجة `container.api`) ⇒ في indsin2web تُترجَم إلى `@view.Each` (قالب HTML + ربط ومعالجات كدوال في `(p, i)`)
  يكرّره الـ runtime؛ مطابقة بالموضع: يُعاد بناء العنصر المتغيّر وحده ويُضاف الجديد ويُحذف الزائد، وغير المتغيّر يبقى بعقدته. `desugarWeb` في `rin_indsin_tags.h` (الافتراضي لا يتغيّر).
- الـ runtime: التخزين المؤقت لآخر قيمة صار على الربط نفسه (`epoch`) بدل فهرس مصفوفة. مثال `indsin_live_list.rin` واختبار `browser_each.js`.
