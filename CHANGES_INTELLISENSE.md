# CHANGES_INTELLISENSE — IntelliSense احترافي في المحرر

استُبدل الإكمال القديم (بادئة ← 6 كلمات) بمحرك سياقي كامل. المنطق كله في C++ (قابل للاختبار بـ g++)، والواجهة في Kotlin.

## ما الجديد
- **إكمال سياقي** (`rin_intellisense.h`): يعرف أين المؤشر فيقترح ما يناسب فقط:
  - `obj.` أعضاء: مساحات الأسماء (`Set.` `Env.` `container.` `container.artifact.` …)، متغيّر `Set` (`s.add` بلا المعامل الأول)، أصناف المستخدم (مع الوراثة، `self.`/`super.`، الثوابت `static`، حالات `enum`)، مفاتيح الكائنات الحرفية `{a: 1}`.
  - `@` وسوم (`@container.*` `@element.*` `@view` `@theme` `@import` …) وقوالب `@name` المعرّفة في الملف · `#words` · مسار `@import "lib/…"` (76 وحدة) · تسميات بعد `break/continue` · أصناف فقط بعد `extends/implements/uses`.
  - بعد `@tmpl name(…)`: `print` `style` `text` `each` · داخل `print`/`show`: سمات الطباعة (`bold` `color` `width` `if` … مع `=` والمؤشر داخل علامتي الاقتباس).
  - **نطاقات حقيقية**: `let`، معاملات الدوال، متغيّرات `each/repeat/retry/for/catch/as`، متغيّرات الـ comprehension؛ لا تظهر خارج نطاقها. الدوال/الأصناف العلوية مرفوعة (hoisted).
  - الكلمات المحجوزة حسب الموضع (`while` لا تُقترح داخل تعبير)، القوالب (snippets) في بداية العبارة فقط.
  - صامت داخل التعليقات والنصوص وعند تسمية تعريف جديد (`let |` `fun f(a, |`).
- **ترتيب ضبابي**: بادئة > مقاطع camelCase (`iN` ← `inputNumber`) > تسلسل جزئي، + أولوية للأقرب نطاقاً + تعلّم من الاستخدام (MRU).
- **أنواع تقريبية** تظهر بجانب المتغيّر (`number` `string` `array` `Set` اسم صنف …) وتعليقات `//` فوق التعريف تصير توثيقاً.
- **Signature Help**: توقيع الدالة مع المعامل النشط مُبرَز (مدمجة، مستخدم، منشئ صنف عبر `init`، قالب `@name(`، `Set` عبر المتغير). يتتبّع المؤشر أيضاً دون كتابة.
- **Hover/ضغط طويل**: يعرض توقيع وتوثيق الرمز (مستخدم/مدمج/كلمة محجوزة).
- **بيانات الدوال** (`rin_intellisense_data.h`، مولَّد بـ `scripts/gen_intellisense_data.py`): 892 دالة، عدد معاملاتها مستخرج من المفسّر نفسه؛ 120 منها موثّقة يدوياً ومحقَّقة من تنفيذها (يفشل المولّد إن تعارض التوثيق مع المصدر). الباقي يظهر بعدد المعاملات فقط.
- **واجهة** (`RinCompletionPopup.kt`): شارات أنواع ملوّنة بألوان التلوين، إبراز الحروف المطابقة، تفصيل خافت، لوحة توثيق للعنصر المحدّد، تمرير، وضع فوق/تحت السطر حسب المساحة المرئية (يراعي لوحة المفاتيح).
- **لوحة مفاتيح خارجية**: ↑↓ تنقّل · Tab يقبل · Enter يقبل بعد التنقّل بالأسهم فقط (وإلا سطر جديد) · Esc يغلق · Ctrl+Space إكمال صريح (`triggerCompletion()` عام للاستدعاء من شريط الأدوات).
- الإعداد الحالي `autocomplete` يتحكم بالقائمة والتوقيع معاً.

## الملفات
جديد: `cpp/editor/rin_intellisense.h` · `rin_intellisense_data.h` (مولَّد) · `RinCompletionPopup.kt` · `scripts/gen_intellisense_data.py` · `scripts/intellisense_docs.json` · `tests/editor/intellisense_selftest.cpp` · هذا الملف.
معدَّل: `rin_editor_engine.h/.cpp` (`complete` `signatureHelp` `describeAt` `recordCompletionUse`) · `rin_editor_jni.cpp` · `RinNativeEditor.kt` · `RinCodeEditorView.kt` (يفترض تطبيق تعديل ستايل الحاويات قبله) · `tests/editor/run.sh`.
`collectSuggestions` القديمة باقية (تستعملها passkit).

## التحقق
`sh tests/editor/run.sh`: المحرر 115/115 + IntelliSense 113/113، على 49 ملفاً حقيقياً (examples/ + tests/loops + tests/print) بلا أي انهيار؛ أسوأ استدعاء 9ms، ومستند 16 ألف سطر 60ms (-O0).
**لم يُبنَ**: Kotlin وJNI على Android (لا SDK/NDK هنا). فُحصت صياغة Kotlin بـ kotlinc بلا أخطاء نحوية، لكن الأنواع لم تُتحقَّق — ابنِ بـ Gradle وجرّب على الجهاز.
## حدود معروفة
- أنواع المتغيّرات استنتاج سطحي (أول تعبير بعد `=`)؛ استدعاءات `obj.method().` لا تُحلَّل.
- الدوال المدمجة غير الموثّقة يدوياً تُعرض بأسماء معاملات عامة (`arg1…`).
- تعلّم الاستخدام (MRU) في الذاكرة فقط (يُمسح عند إغلاق التطبيق).
