# Rin Mask (قناع)

## الفكرة

`mask` هو **معرّف منطقي** لعنصر أو مجموعة أو حاوية أو Volume. يشبه `id` من حيث الهوية، لكنه مفهوم Rin مستقل ومصمم للعمل عبر طبقات Rin وLoom.

- الاسم (`name`) يعرّف الكائن في بنية المصدر.
- القناع (`mask`) يعرّف الكائن بهوية منطقية مستقرة.
- يمكن استخدام القناع للعثور على الكائن حتى لو تغيّر اسمه.
- يُفضّل أن يكون القناع فريداً داخل النطاق الذي ستستخدم فيه عملية البحث.

## الصيغة

### عناصر Loom

```rin
@element.button="saveButton"
mask="save-button";
.end/element
```

وتعمل الصيغة نفسها مع `@view` و`@loop`:

```rin
@view.Column="screen"
mask="main-screen";
.end/view
```

### الحاويات والمجموعات وVolume

```rin
@container="app"
mask = "app-root";
.end/container

@Containers.Group="ui"
mask = "ui-root";
.end/Containers.Group

@Volume="assets"
mask = "assets-root";
.end/Volume
```

## API

### `findMask(mask)`

يعيد الاسم المرتبط بالقناع، أو `nil` إذا لم يوجد:

```rin
let target = findMask("app-root");
print target;
```

ترتيب البحث الحالي للحاويات هو: `container` ثم `Containers.Group` ثم `Volume`.

### `maskOf(name)`

يعيد القناع المرتبط باسم حاوية/مجموعة/Volume، أو `nil`:

```rin
let m = maskOf("app");
print m;
```

## مبدأ التصميم

القناع لا يستبدل `name` ولا يعيد تسمية الكائن. هو طبقة هوية إضافية:

```text
name  -> اسم المصدر / السجل
mask  -> الهوية المنطقية
id    -> المعرف الداخلي الذي يستخدمه Loom
```

في Loom يتم حفظ القناع على الـ `Strand` ويمكن البحث عنه مباشرة بواسطة `findByMask()` داخل المحرك.


## قدرات القناع المتقدمة

القناع ليس مجرد `id` نصّي؛ في Rin يمكن استخدامه كطبقة هوية للوصول إلى الكائن دون الاعتماد على اسمه الحالي.

### الاستعلام

```rin
maskExists("save-button");
maskTarget("save-button");
maskKind("save-button");
maskInfo("save-button");
```

النتيجة المنطقية هي أن تغيير `name` لا يغيّر طريقة الوصول بواسطة `mask`.

### قوائم الأقنعة

```rin
maskNames();
maskNames("container");
maskNames("group");
maskNames("volume");
```

### التعامل مع المجموعات

```rin
maskMembers("ui-root");
```

يعيد أسماء الأعضاء المباشرين للمجموعة.

### العلاقات الهرمية

```rin
maskParent("child-mask");
maskChildren("app-root");
```

`maskParent` يعيد قناع الحاوية الأب، و`maskChildren` يعيد أقنعة الأبناء المباشرين عندما تكون العلاقة مسجّلة في شجرة الحاويات.

### Loom

يتمتع محرك Loom أيضاً ببحث متعدد النتائج، لأن أكثر من Strand يمكن أن يحمل القناع نفسه في حال كان النطاق يسمح بذلك:

```cpp
findByMask(root, "toolbar");
findAllByMask(root, "toolbar");
countByMask(root, "toolbar");
```

`findByMask` يعيد أول تطابق، بينما `findAllByMask` يعيد جميع التطابقات و`countByMask` يحسب عددها.

### قواعد التصميم

1. `mask` هوية منطقية، وليس بديلاً عن `name`.
2. يفضّل أن يكون القناع فريداً داخل النطاق الذي يحتاج بحثاً مباشراً.
3. يمكن استخدام نفس القناع أكثر من مرة في Loom عندما يكون المقصود مجموعة من العناصر، ثم استخدام `findAllByMask`.
4. `maskInfo` يوفر طبقة معلومات موحّدة مناسبة للأدوات والمحرر وعمليات debugging.
5. القناع لا يفرض تغييراً على بنية `name` أو على المعرف الداخلي `StrandId`.

## Mask v1 — الهوية القابلة للتركيب

أصبح `mask` في v3 طبقة هوية تشغيلية أوسع من مجرد `id`.

### Alias
```rin
maskAlias("save", "save-button");
let m = maskResolve("save");
```
يمكن إنشاء عدة أسماء بديلة للقناع، مع بقاء القناع الأصلي ثابتاً.

### Tags
```rin
maskTag("save-button", "primary");
maskHasTag("save-button", "primary");
let tags = maskTags("save-button");
maskUntag("save-button", "primary");
let all = maskByTag("primary");
```

### References
```rin
maskRef("save-button", "toolbar.save");
let refs = maskRefs("save-button");
```
الـ reference رابط منطقي إضافي يمكن للطبقات العليا استعماله دون تغيير الاسم أو القناع.

### Lock
```rin
maskLock("app-root");
maskLocked("app-root");
```
بعد القفل لا تسمح عمليات v3 التي تغيّر metadata للقناع بإضافة alias/tag/ref إليه.

### Compare / Summary
```rin
maskCompare("save", "save-button");
let info = maskSummary("save-button");
```
`maskSummary` يعيد `mask`, `target`, `kind`, `locked`, `tags`, `refs`, و`aliases`.

### نموذج الهوية
```text
Mask
 ├─ canonical identity
 ├─ aliases[]
 ├─ tags[]
 ├─ refs[]
 ├─ parent/children
 └─ target
      ├─ element
      ├─ group
      ├─ container
      └─ volume
```

## Mask v1 — Identity Registry

Mask v4 turns the mask into a programmable identity registry. The identity remains separate from the object's name and can carry operational metadata and relations.

### Lifecycle

```rin
maskEnable("screen.main");
maskDisable("screen.main");
maskActive("screen.main");
```

### Namespace and version

```rin
maskNamespace("screen.main", "ui/home");
maskNamespace("screen.main");
maskByNamespace("ui/home");
maskVersion("screen.main", "2.1");
maskVersion("screen.main");
maskNote("screen.main", "Main application screen");
```

### Explicit mask graph

```rin
maskParentSet("button.save", "panel.editor");
maskParentGet("button.save");
maskChildrenOf("panel.editor");
maskDescendants("panel.editor");
maskRoot("button.save");
maskDepth("button.save");
maskDetach("button.save");
```

The runtime rejects self/cyclic parent relations and respects `maskLock` when mutating protected metadata.

### Tags and references

```rin
maskTag("button.save", "action");
maskHasTag("button.save", "action");
maskHasAllTags("button.save", ["action", "primary"]);
maskHasAnyTag("button.save", ["danger", "action"]);
maskClearTags("button.save");
maskRef("button.save", "save.operation");
maskHasRef("button.save", "save.operation");
maskUnref("button.save", "save.operation");
maskClearRefs("button.save");
```

### Registry inspection

```rin
maskCount();
maskKinds();
maskKindCount("container");
maskSummary("button.save");
```

`maskSummary()` now reports namespace, version, note, active state, and explicit parent when present.
