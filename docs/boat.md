# Boat

مكتبة `lib/boat.og.rin` تجمع مفاهيم أساسية فوق stdlib الأساسية في Rin: قيم
منطقية بأسماء أوضح، بنى بيانات (قوائم/صفوف/قواميس/مجموعات)، قاموس بعنوان
(tmap)، ونافذة/صفحة بسيطة (window) بجسم قابل للربط بحاوية بيانات.

## استيراد

```rin
@import "lib/boat.og.rin";
// أو باسم مستعار (حاوية import):
@import "lib/boat.og.rin" as boat;
```

## 1) Functions

Rin تدعم الدوال كقيم من الدرجة الأولى أصلاً (`fun`، وتمرير/تخزين قيمة دالة في
متغيّر واستدعائها باسمه). تضيف boat فقط:

- `boatApply(fn, items)` — يطبّق `fn` على كل عنصر من `items` ويُعيد مصفوفة
  النتائج.

## 2) None / No / Yes

- `yes` (= `true`)، `no` (= `false`)، `none` (= `nil`)
- `isYes(v)` / `isNo(v)` / `isNone(v)`

## 3) القوائم (list)

- `list(items)` / `ls(items)` — منشئان متكافئان لقائمة boat فوق مصفوفة.
- `groupTm(items)` — نفس الشيء أيضاً؛ ومتاح كذلك عبر الكائن `group` بصيغة
  أقرب لـ"group.tm" (لأن Rin لا تدعم نداءً عاماً بصيغة `a.b(...)` على قيمة
  عادية، فالوصول يكون بخطوتين):

  ```rin
  let makeList = group["tm"];
  let a = makeList([1, 2, 3]);
  ```

- `listLen(l)`, `listGet(l, i)`, `listSet(l, i, x)`, `listPush(l, x)`,
  `listPop(l)`, `listToArray(l)`

## 4) الصفوف (tuple)

- `tuple(items)` — نفس تمثيل القائمة داخلياً، لكن بوسم "tuple" منطقي.
- `tupleLen(t)`, `tupleGet(t, i)`, `tupleToArray(t)`
- `tupleSet(t, i, x)` — **مرفوضة عمداً**: تطبع تحذيراً وتُعيد `t` بلا تعديل
  (Rin لا تملك حماية تعديل حقيقية على مستوى المصفوفات، فهذا التزام واجهة).

## 5) القواميس (dict)

```rin
let d = dict([["name", "Rin"], ["age", 1]]);
```

- `dictGet(d, key)`, `dictSet(d, key, val)`, `dictHas(d, key)`,
  `dictKeys(d)`, `dictValues(d)`, `dictToMap(d)`

## 6) المجموعات (set)

- `set(items)` — يزيل التكرار مع الحفاظ على أول ظهور.
- `setHas(s, x)`, `setAdd(s, x)`, `setRemove(s, x)`, `setLen(s)`,
  `setToList(s)`, `setUnion(a, b)`, `setIntersect(a, b)`

## 7) tmap (عنوان الخريطة)

قاموس مع عنوان يصفه:

```rin
let tm = tmap("Users Table", [["u1", "Ali"], ["u2", "Sara"]]);
```

- `tmapTitle(t)`, `tmapEntries(t)`, `tmapGet(t, key)`, `tmapSet(t, key, val)`

## 8) window (body)

```rin
let win = window("Home", 1080, 720, "#101018", ["Cairo", "Inter"]);
body(win, ["Header", "Content"]);
bodyAdd(win, "Footer");
windowLink(win, "usersContainer");
```

- `window(name, width, length, bg, fonts)` — `length` هنا هي ارتفاع/طول
  الصفحة (تمييزاً عن `width`).
- `body(win, children)` — يضبط جسم الصفحة (window body) كاملاً.
- `bodyAdd(win, child)` — يضيف عنصراً واحداً للجسم دون استبدال الباقي.
- `bodyOf(win)` — يقرأ جسم الصفحة الحالي.
- الوصول/التعديل المباشر:
  `windowName`/`windowSetName`, `windowWidth`/`windowSetWidth`,
  `windowLength`/`windowSetLength`, `windowBg`/`windowSetBg`,
  `windowFonts`/`windowSetFonts`, `windowContainer`.
- `windowLink(win, containerName)` — يسجّل اسم container مرتبط بهذه النافذة
  (توثيق/بيانات وصفية على مستوى boat فقط).

> **ملاحظة:** الكلمة `link` محجوزة أصلاً في نحو Rin (`link to=name;` /
> `link id="X";`، انظر [containers.md](containers.md)) وتُستخدم للربط/النسخ
> الفعلي بين الحاويات على مستوى اللغة. لذلك boat تستخدم `windowLink` بدل
> تعريف دالة باسم `link` (تجنّباً لتعارض نحوي)، كتكملة عملية لها على مستوى
> النافذة/الصفحة فقط — وليست بديلاً عنها. لنسخ متغيّرات container فعلياً
> إلى النطاق الحالي استخدم `link to=containerName;` أو
> `tying with=containerName;` الأصليتين.

## هيكل قيمة boat الموحَّد

كل مُنشئ من `list`/`tuple`/`dict`/`set`/`tmap`/`window` يُعيد قاموساً بالشكل:

```json
{"kind": "list" | "tuple" | "dict" | "set" | "tmap" | "window", "value": ...}
```

- `boatKind(v)` — يقرأ حقل `"kind"` بأمان (`nil` إن لم يكن قيمة boat).
- `boatValue(v)` — يقرأ حقل `"value"` الخام (المصفوفة/القاموس وراء المفهوم).

## مثال كامل

انظر [`examples/boat_demo.rin`](../examples/boat_demo.rin) لعرض تفصيلي لكل
دالة أعلاه قيد التشغيل الفعلي (`rin run examples/boat_demo.rin`).
