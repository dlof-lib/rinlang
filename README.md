# تصحيح Loom — ستايل/شاشة/مساحة/طول/عرض

هذه 4 ملفات **معدَّلة فقط** (لا ملفات جديدة) من مشروع RinLang، داخل نفس مسارها
الأصلي بالضبط. لتطبيق التصحيح: انسخها فوق نفس المسارات في نسخة مستودعك
(استبدال مباشر)، ثم أعد البناء عبر Gradle/NDK كالمعتاد — لا حاجة لأي تعديل آخر
(لا تغييرات في CMakeLists.txt ولا في الـ parser).

```
app/src/main/cpp/loom/rin_loom_tokens.h     — دوال جديدة: resolveSpacing (نسخة % جديدة)،
                                               resolveSizeAttr، resolveMargin، resolveScreenPreset
app/src/main/cpp/loom/rin_loom_strand.h     — حقل Strand::geometryOuter جديد + element_margin
app/src/main/cpp/loom/rin_loom_layout.h     — نموذج الـ margin الكامل + دعم % لكل من
                                               width/height/min_*/max_* + معالجة screen=
app/src/main/cpp/loom/rin_loom_pipeline.h   — إضافة margin_left/top/right/bottom وscreen لقائمة
                                               "visual" التي تُنقّى من عناصر @element.*
```

## الميزات الجديدة (كل صياغتها تعمل بلا أي تغيير في المحلّل النحوي — key=value; عامة أصلاً)

```rin
@view.Card=box
    width="50%";        // نسبة % بدل رقم ثابت — لأي من width/height/min_width/max_width/min_height/max_height
    height=100;
    margin=20;           // كل الجهات دفعة واحدة
    margin_left=10;       // أو جهة واحدة (تتغلّب على margin= إن ذُكرت)
    margin_top=5;
    margin_right=0;
    margin_bottom=0;
    background="#3498db";
.end/view

@loop=app
    screen="phone";       // phone_small | phone | phone_large | tablet | tablet_large | desktop | desktop_wide
    // يملأ width=/height= تلقائياً إن لم تُكتَبا يدوياً — واضح صريح يفوز دوماً
.end/loop
```

- `element_margin=` على مستوى `@loop` يعمل كإعداد افتراضي موروث لكل الأبناء، بنفس منطق
  `element_padding=`/`element_radius=` الموجودة أصلاً.
- الـ margin مساحة **خارج** الصندوق: لا يُرسَم فيها خلفية/حد/محتوى أبداً، فقط تُحجَز كمساحة
  فارغة تمنع تراكب العناصر المجاورة — نفس مبدأ CSS box model.

## التحقق الذي تم إجراؤه فعلياً (وليس افتراضاً)

1. **فحص صياغي كامل (`g++ -fsyntax-only`)** لكل من `rin_loom_layout.h`، `rin_loom_pipeline.h`،
   وملف `rin_interpreter.cpp` الكامل (نقطة الدمج الحقيقية لكل من تطبيق أندرويد عبر JNI وأداة
   CLI المستقلة `rin`) — **بلا أي خطأ**.
2. **بناء وربط (link) حقيقي كامل** لأداة `tools/rin_loom_run.cpp` (أداة اختبار حقيقية موجودة
   أصلاً في المستودع، تشغّل Lexer→Parser→Interpreter→Loomtime الحقيقيين، وليست محاكاة) مقابل
   كل مصادر المحرّك الحقيقية (`rin_lexer.cpp`, `rin_parser.cpp`, `rin_interpreter.cpp`,
   `loom/rin_loom_c_api.cpp`, `diagnostics/*`, `clc/*`, ...) — **نجح الربط بلا أي خطأ**.
3. **تشغيل فعلي** لملفي اختبار `.rin` حقيقيين عبر الأداة المبنية، والتحقّق من ناتج JSON الحقيقي:
   - `screen="phone"` → أعطى فعلياً `w:390, h:844` للوحة الجذر.
   - `width="50%"` مع `margin=20` على صندوق داخل عمود بعرض 390 → أعطى فعلياً `w:175` (٪50 من
     350 = العرض المتاح بعد خصم الحافتين 20+20) و`x:20, y:20` (إزاحة الحافة الصحيحة).
   - `margin_left=10; margin_top=5;` مع `width="25%"` على صندوق ثانٍ → أعطى فعلياً `x:10`،
     `y:145` (بعد ارتفاع الصندوق الأول الكامل 100+20+20=140)، و`w:95` (٪25 من 380).

هذه نتائج فعلية من تشغيل حقيقي، لا افتراضات — أي أن التصحيح يعمل بشكل صحيح على المحرّك
الحقيقي للمشروع، وليس فقط "يترجَم بلا خطأ".

## قيد معروف يجب معرفته

النسبة المئوية لـ`width=`/`height=`/`min_*`/`max_*` مدعومة في نقطة الدخول العامة
لدالة `layout()` (تنطبق على **كل** أنواع Strand)، لكن بعض القياسات الداخلية الخاصة بنوع
معيّن (مثل عرض الدرج الجانبي الثابت في `layoutScaffold`، أو نسبة الفيديو/الصورة في
`measureMedia`) ما زالت تقرأ القيمة عبر `attrNum` المباشر القديم بلا دعم %، لتقليل حجم
ونطاق التعديل. يمكن تعميم الدعم لهذه المواضع الفرعية القليلة لاحقاً بنفس الأسلوب دون أي
تغيير بنيوي إضافي.
