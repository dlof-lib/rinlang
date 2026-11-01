# Namespaces — `container` / `Containers.Group` / `Volume` كفضاءات أسماء

كل `@container` و`@Containers.Group` و`@Volume` **مُسمّاة** هي الآن عقدة في شجرة فضاءات أسماء حقيقية.
**`container` هو الأساس** ويعمل كفضاء أسماء وحده؛ أما `Group` و`Volume` فمستويات اختيارية تُضاف
فوقه متى شئت (بأي ترتيب وأي عمق)، ولا يلزم وجودها أبداً.

## الوصول المؤهَّل

```kotlin
@container=app
    state counter = 0;
    let title = "Rin";
    fun inc() { counter = counter + 1; return counter; }
    on update(prev) { print "was " + prev["counter"]; }
.end/container

print app.title;        // قراءة
app.counter = 5;        // كتابة — تُطلق on update لحقول state كأي إسناد عادي
print app.inc();        // استدعاء دالة معرَّفة داخله
```

يعمل نفس الشيء بأي عمق: `Store.Inventory.items.count`، `Store.Inventory.items.add(4)`.
يعمل في المحرّكين (المفسّر الشجري وRinVM).

## العزل: نفس الاسم في أماكن مختلفة بلا تعارض

```kotlin
@Volume=Store
    @Containers.Group=Inventory
        @container=items
            let count = 3;
        .end/container
    .end/Containers.Group
    @Containers.Group=Archive
        @container=items
            let count = 100;
        .end/container
    .end/Containers.Group
.end/Volume

print Store.Inventory.items.count;   // 3
print Store.Archive.items.count;     // 100
```

كل فضاء أسماء يملك **فقط** ما أُعلن مباشرةً بداخله. `Store.owner` لا يرى متغيّرات `items` والعكس.

## القواعد

| الحالة | السلوك |
|---|---|
| تعشيش `@container` داخل `@container` | يصبح فضاء أسماء فرعياً: `outer.inner.x` |
| الحل النسبي | داخل مجموعة/حاوية يمكن مناداة الأشقاء باسمهم المجرَّد: `prices.apple` من داخل `cart` |
| متغيّر عادي بنفس اسم فضاء أسماء | **المتغيّر يغلب** (لا يكسر أي كود قديم) |
| عضو غير موجود | خطأ `E0001` يسمّي الفضاء ويقترح أقرب اسم (`did you mean ...`) |
| إسناد لعضو غير مُعلَن | خطأ (لا يُنشأ حقل ضمنياً — استخدم `let` داخل الفضاء) |
| إسناد لفضاء فرعي (`conf.db = 1`) | خطأ `E0004` |
| فضاء فرعي وحقل بنفس الاسم | الفضاء الفرعي يُقرأ أولاً |
| `@container` بلا اسم | يعمل كما كان، لكنه لا يدخل الشجرة |
| إعادة تنفيذ تعريف بنفس الاسم (مثلاً في حلقة) | يستبدل العقدة القديمة |

قراءة فضاء فرعي كقيمة (`print Store.Inventory;`) تُعيد **لقطة map** بحقوله (بلا الدوال) وفضاءاته الفرعية.

## الفروقات: `container` هو الأقوى

الثلاثة فضاءات أسماء قابلة للتعشيش، لكن صلاحيات الوصول المؤهَّل **مختلفة**:

| القدرة | `Volume` | `Containers.Group` | `container` |
|---|:-:|:-:|:-:|
| قراءة الحقول (`a.x`) | ✅ | ✅ | ✅ |
| كتابة الحقول (`a.x = v`) | ❌ | ✅ | ✅ |
| استدعاء الدوال (`a.f()`) | ❌ | ❌ | ✅ |
| `state` + `on update` عند الكتابة | — | ❌ | ✅ |
| معاملة ذرّية `ns.transaction` | ❌ | ❌ | ✅ |
| احتواء فضاءات فرعية | ✅ | ✅ | ✅ |

- **Volume**: طبقة تنظيم للقراءة فقط (إعدادات/ثوابت/تجميع منطقي). الكتابة أو الاستدعاء عبر اسمه المؤهَّل خطأ `E0035`.
- **Group**: حقول مشتركة قابلة للقراءة والكتابة بين أعضائها، بلا استدعاء دوال مؤهَّل.
- **container**: كل شيء — وهو الوحيد الذي تُنفَّذ دوالُه عبر الاسم المؤهَّل، وتُطلق الكتابة فيه خُطّافات `state`، ويدعم `ns.transaction(path, fn)` (تراجع كامل إن رمت `fn` خطأً).

الدوال المعرَّفة داخل Group/Volume تبقى تعمل **من الداخل** بأسمائها المجرَّدة كما كانت (الفرق في الوصول المؤهَّل فقط).
`ns.capabilities(path)` تُرجع map بهذه القدرات: `{kind, read, write, call, state, transaction, nest}`.

## `ns.*` — استعلام ووصول ديناميكي (مسارات مطلقة منقوطة)

| الدالة | الناتج |
|---|---|
| `ns.has(path)` | هل الفضاء موجود |
| `ns.kind(path)` | `"container"` أو `"group"` أو `"volume"` أو `nil` |
| `ns.parent(path)` | مسار الأب أو `nil` للجذر |
| `ns.members(path)` | أسماء الفضاءات الفرعية المباشرة |
| `ns.fields(path)` | أسماء ما أُعلن مباشرةً (متغيرات ودوال) |
| `ns.capabilities(path)` | قدرات الفضاء حسب نوعه |
| `ns.transaction(path, fn)` | معاملة ذرّية (container فقط) |
| `ns.snapshot(path)` | لقطة map (حقول + فرعية) |
| `ns.get(path, field)` / `ns.set(path, field, v)` | قراءة/كتابة بمسار نصي |
| `ns.call(path, fn, [args])` | استدعاء دالة |
| `ns.paths()` | كل مسارات الشجرة |

## توافق

الواجهات القديمة (`container.get/set/...`, `groupVars`, `volumeContainers`, ...) لم تتغيّر؛ سجلّها
المسطّح بالأسماء المجرَّدة باقٍ كما هو. الشجرة الجديدة تُضاف بجانبه ولا تستبدله.

الاختبارات: `tests/verification/namespaces.rin` و`namespaces_err.rin`. التنفيذ: `NsNode` في
`rin_interpreter.h`، و`ns*` في `rin_interpreter.cpp`، وتعليمات `NsGet/NsSetTest/NsSet` في RinVM.
