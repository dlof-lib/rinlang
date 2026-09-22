# boat.md — "Boat": مفاهيم أساسية مجمَّعة

`Boat` (`lib/boat.og.rin`، يُستورَد بـ`@import "lib/boat.og.rin";`) مكتبة تجمع عدّة
أنواع بيانات جاهزة فوق `array`/`map` الأصليتين — كل قيمة Boat هي قاموس عادي بحقل
`kind` (تُقرَأ عبر `boatKind(x)`) يُحدِّد نوعها.

## القيم المنطقية الثابتة
```rin
print yes, no, none;              // true false nil
print isYes(yes), isNo(no), isNone(none); // true true true
```

## `list` / `ls` / `groupTm` — مصفوفة قابلة للتعديل
```rin
let a = list([1, 2, 3]);
print boatKind(a), listLen(a), listGet(a, 1); // list 3 2
listPush(a, 4);
print listToArray(a); // [1, 2, 3, 4]
```
`ls(...)` و`groupTm(...)` مرادفان بنفس الشكل (`boatKind` يُرجع `"list"` لكل الثلاثة).

## `tuple` — ثابتة (immutable) فعليًا
```rin
let t = tuple([10, 20, 30]);
print boatKind(t), tupleLen(t), tupleGet(t, 2); // tuple 3 30
tupleSet(t, 0, 99); // لا يُعدِّلها -- يطبع تحذيرًا فقط ويترك القيمة كما هي
print tupleToArray(t); // [10, 20, 30]  -- لم يتغيّر شيء
```

## `dict`
```rin
let d = dict([["name", "Rin"], ["version", 1]]);
print boatKind(d), dictGet(d, "name"), dictHas(d, "version"); // dict Rin true
dictSet(d, "extra", true);
print dictKeys(d), dictValues(d); // ["name","version","extra"] ["Rin",1,true]
```

## `set` — بلا تكرار، مع عمليات مجموعات
```rin
let s1 = set([1, 2, 2, 3]);
print boatKind(s1), setLen(s1), setHas(s1, 2); // set 3 true  -- التكرار أُزيل تلقائيًا
setAdd(s1, 5);
setRemove(s1, 1);
print setToList(s1)["value"]; // [2, 3, 5]

let s2 = set([3, 4, 5]);
print setUnion(s1, s2)["value"];     // [2, 3, 5, 4]
print setIntersect(s1, s2)["value"]; // [3, 5]
```

## `tmap` — قاموس بعنوان (Titled Map)
```rin
let tm = tmap("Users Table", [["u1", "Ali"], ["u2", "Sara"]]);
print boatKind(tm), tmapTitle(tm), tmapGet(tm, "u1"); // tmap Users Table Ali
tmapSet(tm, "u3", "Omar");
print tmapEntries(tm); // {"u1": "Ali", "u2": "Sara", "u3": "Omar"}
```

## `window` / `body` / `windowLink` — نافذة تطبيق منطقية
```rin
let win = window("Home", 1080, 720, "#101018", ["Cairo", "Inter"]);
print windowName(win), windowWidth(win), windowLength(win), windowBg(win), windowFonts(win);
body(win, ["Header", "Content"]);
bodyAdd(win, "Footer");
print bodyOf(win); // ["Header", "Content", "Footer"]
windowLink(win, "usersContainer");
print windowContainer(win); // usersContainer
```
هذا وصف منطقي بحت (قاموس بيانات) — لا علاقة مباشرة له بمحرّك indsin (انظر
[`indsin.md`](../indsin.md)) رغم تشابه المفردات (نافذة/جسم/خط).

## نمط الاستدعاء غير المباشر `group["type"]`
```rin
let makeList = group["tm"];      // group قاموس جاهز يربط اسم النوع بدالة الإنشاء
let viaGroup = makeList([7, 7, 8]);
print boatKind(viaGroup), listToArray(viaGroup); // list [7, 7, 8]
```

(كل الأمثلة أعلاه من `examples/boat_demo.rin`، تحقّقتُ من تشغيلها فعليًا حرفيًا.)

## انظر أيضًا
- [`standard-library.md`](./standard-library.md) — فهرس بقية وحدات `lib/`.
- [`variables.md`](./variables.md) — المصفوفات/القواميس الأصلية التي تُبنى Boat فوقها.
