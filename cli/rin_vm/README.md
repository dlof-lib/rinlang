# rin_vm — صورة VM لـ Rin (مكتبة header-only + أداة)

تحوّل برنامج Rin وكل ما يستورده إلى **ملف واحد مغلق ذاتياً** في أقل من ثانية، يعمل بلا ملفات مرافقة.

```
rin_vm pack prog.rin -o prog.rvm     # حزم (يلتقط @import / import…from الحرفية)
rin_vm info prog.rvm                 # محتوى الصورة
rin_vm unpack prog.rvm -d out/       # استخراج المصدر والمستورَدات
rin image prog.rvm [args…]           # تنفيذ (rin من cli/linux)
rin_ava -fvm prog.rin -o prog        # تنفيذي مستقل بنفس الصورة
```

## rinvm — ملف VM الكامل
`rinvm` تنفيذي واحد (المحرّك الكامل + البايتكود + هذه المكتبة، بلا طرفية ولا RinPM):
```
rinvm prog.rvm [args]        # تنفيذ صورة
rinvm prog.rin [args]        # حزم فوري (يضم @import) ثم تنفيذ
rinvm pack prog.rin -o p.rvm / rinvm info p.rvm
```
وهو runtime الذي يفضّله `rin_ava -fvm` (التنفيذي الناتج = rinvm + الصورة، ويعمل بلا أي ملف آخر).
تحويل مجلد كامل والتحقق من المطابقة مع المفسّر:
`cli/rin_vm/tools/convert_all.sh rinvm rin examples out/`  ← على أمثلة المشروع: 37/37 مطابقة.

## من C++
```cpp
#include "rin_vm.h"
auto im = rin_vm::build("prog.rin", "src/prog.rin", source);      // + bytecode اختياري كوسيط رابع
std::string bytes = rin_vm::serialize(im);                         // RVM1 + FNV-1a checksum
rin_vm::Image back; rin_vm::deserialize(bytes, back, &err);
// التنفيذ في rin: interp.setVirtualFiles({back.files...}); (انظر runImageBytes في cli/linux/src/main.cpp)
```

## الدلالات
- الصورة بلا بايتكود ← المحرّك الكامل (rin_interpreter): **كل اللغة** بدلالات المفسّر نفسها.
- الصورة ببايتكود ← `toolchain/BytecodeVM` (نواة اللغة، `rin_ava -fvm-auto`).
- المستورَدات غير الموجودة وقت البناء (مكتبات مدمجة/حزم RinPM) تُسجَّل في `unresolved` وتُحلّ وقت التشغيل.
- `Interpreter::setVirtualFiles` تُفحَص قبل القرص، وتبقى فارغة افتراضياً فلا أثر على أي استدعاء قديم.
- لا تُنفِّذ الصورةُ شيئاً أثناء الحزم، والفك يرفض المسارات الخارجة عن المجلد.
