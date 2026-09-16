# RIN CONTAINER SQL (RCSQL) 1.0

نظام استعلام مصغّر ومميّز خاص بلغة Rin، للبحث/الفلترة داخل مجموعات المستندات القائمة أصلاً في
اللغة عبر `@container.doc` / `@doc` (وليس نظام تخزين جديد — RCSQL يستعلم بيانات موجودة، لا يعرّفها).

## الفكرة

- **بنية البيانات المُستعلَمة تبقى `@` كما هي دوماً**: تُعرَّف المستندات كالمعتاد بـ
  `@container.doc=name ... document id="..." fields={...}; ... .end/container.doc`.
- **الاستعلام نفسه يُكتَب بخمسة رموز تركيبية فقط**: `/` `:` `&` `()` `#` — بالإضافة لحروف/أرقام
  المعرّفات (لاتينية أو عربية). أي رمز آخر (مثل `|` أو `=` أو `,`) يُرفَض فوراً بخطأ `E0042`.
- **الاستدعاء يكون بالأقنعة (masks)**: يمكن تعريف استعلام RCSQL كحاوية `@` مستقلة قابلة لإعادة
  الاستخدام عبر `@sql`/`@container.sql`، ثم استدعاؤه لاحقاً بقناعها فقط دون تكرار نص الاستعلام.

## الصياغة

```
query      := target ( "&" predicate )*
target     := "#" IDENT                  -- قناع (mask) يُحلّ إلى اسم حاوية
            | IDENT ( "/" IDENT )*        -- اسم حاوية مباشر، أو مسار Group/حاوية متداخل
predicate  := field ":" OP "(" ARG? ")"
field      := IDENT ( "/" IDENT )*        -- يدعم حقول map متداخلة: address/city
OP         := eq | ne | gt | gte | lt | lte | has | like
           | starts | ends | contains | exists | missing | empty | notempty | isnull | notnull
ARG        := IDENT                       -- وسيط واحد فقط
```

### الأهداف (target)

| الشكل            | المعنى                                                                 |
|-------------------|--------------------------------------------------------------------------|
| `#mask`          | يُحلّ عبر سجل الأقنعة (`mask="...";`) إلى اسم الحاوية الفعلي              |
| `name`           | اسم حاوية `@container.doc`/`@doc` مباشر                                  |
| `group/name`     | حاوية متداخلة داخل `Containers.Group` — يُتحقَّق من العضوية خطوة بخطوة    |

### العمليات (predicate OP)

| العملية | المعنى                                                              |
|---------|------------------------------------------------------------------------|
| `eq`    | يساوي (`valuesEqual`)                                                  |
| `ne`    | لا يساوي                                                                |
| `gt`/`gte`/`lt`/`lte` | مقارنة رقمية (يتطلّب أن يكون الحقل والوسيط رقمَين، وإلا فالنتيجة false) |
| `has`   | الحقل مصفوفة تحوي الوسيط، أو map يحوي مفتاحاً بنفس اسم الوسيط           |
| `like`  | الحقل نص، ويحوي الوسيط كنص جزئي (substring) بغضّ النظر عن حالة الأحرف |
| `contains` | مرادف واضح لـ `like` للمطابقة الجزئية غير الحساسة لحالة الأحرف |
| `starts` | النص يبدأ بالوسيط، بدون حساسية لحالة الأحرف |
| `ends` | النص ينتهي بالوسيط، بدون حساسية لحالة الأحرف |
| `exists` | الحقل موجود؛ بدون وسيط: `exists()` |
| `missing` | الحقل غير موجود؛ بدون وسيط: `missing()` |
| `empty` | القيمة `nil` أو نص/مصفوفة/map فارغة؛ بدون وسيط |
| `notempty` | القيمة غير فارغة؛ بدون وسيط |
| `isnull` | الحقل موجود وقيمته `nil`؛ بدون وسيط |
| `notnull` | الحقل موجود وقيمته ليست `nil`؛ بدون وسيط |

الوسيط (ARG) يُفسَّر تلقائياً: `true`/`false` → منطقي، `null`/`nil` → nil، نص يُقرأ كاملاً كرقم
صالح → رقم، وإلا → نص كما كُتب (لا توجد علامات اقتباس ضمن الصياغة المسموحة).

## أمثلة

```rin
@container.doc=users
    mask="appUsers";
    document id="u1" fields={ name: "Ali",  role: "admin", age: 25, address: { city: "Cairo" } };
    document id="u2" fields={ name: "Sara", role: "user",  age: 30, address: { city: "Giza" } };
.end/container.doc

// استعلام خام مباشر عبر القناع
print sql("#appUsers & role:eq(admin) & age:gte(18)");

// أول نتيجة فقط / عدد النتائج فقط
print sqlOne("#appUsers & address/city:eq(Cairo)");
print sqlCount("#appUsers & role:eq(admin)");
```

### تعريف استعلام مُسمّى واستدعاؤه بالقناع

```rin
@sql=activeAdmins
    mask="qActiveAdmins";
    text query = "#appUsers & role:eq(admin) & age:gte(18)";
.end/sql

// لا حاجة لتكرار نص RCSQL في كل مرة -- استدعاء بالقناع فقط:
print sql("qActiveAdmins");
```

`sql()`/`sqlOne()`/`sqlCount()`/`sqlExists()`/`sqlIds()`/`sqlExplain()` تتعامل مع الوسيط الممرَّر بشكل موحَّد: إن كان قناعاً لحاوية
`@sql`/`@container.sql` معرَّفة مسبقاً تُستبدَل تلقائياً بنص RCSQL المخزَّن بداخلها؛ وإلا يُعامَل
كنص RCSQL خام فوري.

## القيم المُعادة

`sql(query)` تعيد مصفوفة؛ كل عنصر فيها map يحمل حقل `_id` إضافياً (معرّف المستند) بالإضافة لكل
حقول المستند الأصلية. `sqlOne(query)` تعيد أول عنصر مطابق (بنفس الشكل) أو `nil`. `sqlCount(query)`
تعيد عدداً فقط. `sqlExists(query)` تعيد `true/false`، و`sqlIds(query)` تعيد IDs فقط، و`sqlExplain(query)` تعرض تحليل RCSQL 1.0 الأساسي.

حاوية/قناع غير موجود، أو بلا أي تطابق، يعيد مصفوفة فارغة `[]` بصمت (بلا خطأ) — بنفس سلوك بقية
دوال `docStore` الحالية (`queryDocs`/`findDoc`/...).

## الأخطاء

رمز خارج القائمة المسموحة (`/ : & () #` وحروف/أرقام المعرّفات)، أو أي خلل تركيبي آخر (حقل بلا
عملية، عملية غير معروفة، قوس ناقص...)، يُرفَض بخطأ تشخيصي `E0042` (`InvalidSql`) يوضّح موضع الخلل
بالضبط داخل نص الاستعلام.

## الملفات

- `rin_container_sql.h` / `rin_container_sql.cpp` — المحلّل النحوي البحت (Query/Predicate/parse)،
  بلا أي اعتماد على `Interpreter`/`Value`.
- `rin_interpreter.h`/`.cpp` — الربط الفعلي: `sqlViews`، حل القناع/الهدف، التنفيذ، العدّ، فحص الوجود، استخراج IDs، و`sqlExplain`، مع predicates RCSQL 1.0 الموسّعة.
- `rin_ast.h` — `ContainerKind::SQL`.
- `rin_parser.cpp` — التعرّف على وسمَي `@sql`/`@container.sql` (حاوية بيانات نقية، بنفس قيود
  `container.data`/`table`/`doc`/...).
- `diagnostics/diagnostic.h`/`.cpp` — `E0042_InvalidSql`.
- `tools/test_container_sql.cpp` (ومثلها في `tests/tools/`) — اختبارات RCSQL 1.0 عبر مسار CLI الحقيقي.
