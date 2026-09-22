# api.md — واجهة C المسطَّحة (Embedding API)

طبقة C خالصة (`extern "C"`) لتضمين محرّك Rin في أي تطبيق مضيف (أندرويد عبر JNI —
انظر [`android.md`](./android.md) — أو أي تطبيق C/C++ آخر مباشرة). كل نتيجة نصيّة
مُخصَّصة بـ`malloc`، تُحرَّر دائمًا عبر `rin_free_string()`.

## `rin_c_api.h` — تنفيذ عام (بلا واجهة)
```c
char* rin_run(const char* source, const char* basePath);
void  rin_free_string(char* s);
const char* rin_engine_version(void);

RinSession* rin_session_create(const char* basePath);
char*       rin_session_run(RinSession* session, const char* source);
void        rin_session_free(RinSession* session);
```
`rin_run` تشغيل لمرة واحدة يُرجع كل مخرجات `print`/`show` كنص واحد (basePath
يُستخدَم لحلّ مسارات `@import` النسبية، انظر
[`cross-file-containers.md`](./cross-file-containers.md#تنبيه-دقيق-كيف-يُحلَّل-المسار)).
`rin_session_*` نسخة تحتفظ بحالة المفسِّر بين استدعاءات متعددة على نفس الجلسة
(مفيد لـREPL تفاعلي).

## `rin_indsin_c_api.h` — عرض الواجهة الحيّة
يبني فوق `rin_c_api.h` بنفس القاعدة (`malloc`/`rin_free_string`):

```c
// عرض بلا حالة (stateless) لمرة واحدة:
char* rin_indsin_render_json(const char* source, int rootWidth);
char* rin_indsin_render_container_json(const char* source, const char* containerName, int rootWidth);

// جلسة حيّة (تحتفظ بحالة Fabric+Warp بين الاستدعاءات، فيُصبح onTap قادرًا فعلاً
// على تغيير الحالة عبر Needle بدل إعادة بناء المعاينة كاملة من الصفر):
void* rin_indsin_session_create(const char* source, int rootWidth);
void* rin_indsin_session_create_for_container(const char* source, const char* containerName, int rootWidth);
void  rin_indsin_session_set_viewport(void* session, int viewportHeight); // افتراضي 844px
char* rin_indsin_session_render_json(void* session);
char* rin_indsin_session_tap(void* session, double x, double y);
char* rin_indsin_session_long_press(void* session, double x, double y);
char* rin_indsin_session_double_tap(void* session, double x, double y);
char* rin_indsin_session_hover(void* session, double x, double y, int entering);
char* rin_indsin_session_tick(void* session);
char* rin_indsin_session_update_source(void* session, const char* newSource); // Hot Reload
void  rin_indsin_session_free(void* session);

// عرض نقطي (raster) بدل JSON -- مفيد لتصدير معاينة كصورة فعلية بلا مضيف يرسم بنفسه:
unsigned char* rin_indsin_session_render_rgb(void* session, int* outW, int* outH);
void           rin_indsin_free_buffer(unsigned char* buf);
int            rin_indsin_session_export_png(void* session, const char* path);
```

### شكل نتيجة `rin_indsin_render_json`/`rin_indsin_session_render_json`
شجرة Fabric كاملة (`kind`/`name`/`line`/الهندسة/كل السمات المحلولة، بشكل
تكراري) — أو `{"error": "...", "line": N}` إن فشل التحليل أو لم يوجد جذر
`@view`/`@element`/`@loop` (انظر قسم "دمج @element بـindsin" في
[`../indsin.md`](../indsin.md)). نسخة الجلسة تضيف أيضًا خطة الرسم الفعلية
(`paint`، نفس مصفوفة `DrawCommand` من محرّك Dye — انظر
[`rinflow.md`](./rinflow.md) لمفهوم مشابه في سياق مختلف).

### شكل نتيجة `rin_indsin_session_tap`/`...long_press`/`...double_tap`/`...hover`
```json
{"ok": true, "handled": true, "targetId": 7, "changed": ["counter"], "fabric": { ... }}
```
`handled` يعني وُجد `onTap=` على العنصر المستهدَف فعلًا؛ `changed` أسماء كل
Warp cell تغيّرت نتيجة تشغيله؛ `error` يظهر فقط إن وُجد المعالج لكنه فشل وقت
التشغيل (وسائط خاطئة مثلًا).

## انظر أيضًا
- [`android.md`](./android.md) — `RinEngine.kt`، الجسر عبر JNI فوق هذه الواجهة بالضبط.
- [`indsin.md`](../indsin.md) — المحرّك الذي تُشغِّله كل دوال `rin_indsin_*`.
- [`rinflow.md`](./rinflow.md) — طبقة Flow (لم تُعرَض هنا عبر C API مباشرة، فقط عبر `Interpreter::runProgramAsFlow` بلغة C++، ثم `RinEngine.kt` في Kotlin).
