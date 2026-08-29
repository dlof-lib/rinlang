# container.chatbot — الطبقة الأساسية بالـ interpreter

هذا الباتش يضيف `ContainerKind::CHATBOT` (نوع حاوية جديد `@container.chatbot=` / `@chatbot=`)
مع الدوال native الأساسية، باتّباع نفس نمط `container.doc` / `container.api` / `watch` /
`subscribe` الموجود أصلاً بالمشروع، بلا أي تغيير في اللكسر (lexer) أو أي بنية جديدة بالـ AST
غير قيمة enum واحدة.

## الملفات المعدَّلة (انظر `chatbot_container.patch`)
- `app/src/main/cpp/rin_ast.h` — إضافة `ContainerKind::CHATBOT` + توثيق الصيغة.
- `app/src/main/cpp/rin_parser.cpp` — التعرّف على `container.chatbot` / `chatbot` (صيغتان،
  بنفس مبدأ `container.doc`/`doc`). **لم تُضَف** لقائمة `validateDataContainerBody`، لأنها
  تحتاج تسمح بدوال (`fun`) واستدعاءات حقيقية بداخلها (تسجيل معالجات أحداث)، بعكس
  `container.table`/`container.doc` المقيَّدة بـ"بيانات نقية".
- `app/src/main/cpp/rin_interpreter.h` — تخزين سجلّ الرسائل/مؤشر الكتابة/معالجات الأحداث.
- `app/src/main/cpp/rin_interpreter.cpp` — الدوال الجديدة + `fireChatEvent`.

## الدوال الجديدة (native)
| الدالة | الوصف |
|---|---|
| `sendMessage(container, role, text)` | يضيف رسالة، يُطلق معالجات `"message"` |
| `botReply(container, text)` | اختصار لـ `sendMessage(container, "bot", text)` |
| `attachToChat(container, role, fileRef, caption)` | رسالة مرفق (kind="attachment") |
| `chatHistory(container)` | كل الرسائل كمصفوفة |
| `lastChatMessage(container)` | آخر رسالة أو nil |
| `chatMessageCount(container)` | عدد الرسائل |
| `clearChat(container)` | يمسح السجلّ فقط (لا يمسّ warp/المعالجات) |
| `setChatTyping(container, bool)` / `isChatTyping(container)` | مؤشر الكتابة، يُطلق `"typing"` عند التغيّر |
| `openChat(container)` / `closeChat(container)` | يُطلقان `"open"`/`"close"` |
| `onChat(container, event, fn)` | تسجيل معالج (`event` ∈ message/open/close/typing)، `fn` مُعرَّفة بـ `fun` مسبقاً |
| `offChat(container, event)` | إزالة كل معالجات حدث بعينه |
| `exportChat(container, "text"\|"png", path?)` | تصدير حقيقي إلى ملف: نصّ `role: text` سطراً لكل رسالة، أو صورة PNG بفقاعات محادثة حقيقية (تُبنى بنفس أدوات رسم `container.table` — `rinfont`/`pngutil` — سطر واحد لكل فقاعة، بلا التفاف نص، محاذاة يمين لِـ `role=="user"` ويسار لغيره) |
| `sendMessage(container, role, text, format?)` | نفس السابقة + وسيط رابع اختياري `format`: `"text"` (افتراضي)/`"markdown"`/`"code"` يُخزَّن على الرسالة |
| `botReply(container, text, format?)` | نفس السابقة + `format` اختياري، يُمرَّر لـ `sendMessage` |
| `botReplyMarkdown(container, text)` | اختصار لـ `botReply(container, text, "markdown")` |
| `botReplyCode(container, code, language?)` | اختصار لـ `botReply(container, code, "code")`، مع `language` اختياري (`"python"`/`"cpp"`/...) يُخزَّن في `meta.language` |
| `streamReply(container, text, chunkSize?)` | **ردّ تدريجي حقيقي (`stream=true`)**: يضيف فقاعة بوت واحدة ثم يُحدِّثها بالمرجع جزءاً فجزء (`chunkSize` حرف، افتراضياً 8) بدل إنشاء فقاعة جديدة كل مرة؛ يُطلق `onChat(container,"message",fn)` مع كل تحديث ليعيد الرسم فقط لا يُضيف. `meta.streaming` = `true` أثناء البث و`false` عند الاكتمال |

**الذاكرة (memory)** لا تحتاج دالة خاصة: أعلن `warp memory = {};` بداخل `@container.chatbot`
مثل أي `warp` عادية (نفس آلية `Home` في `container_loom_api_demo.rin`).

**model / system** أيضاً مجرد متغيرات عادية (`text model = "..."`) بداخل الحاوية — لا تحتاج
native جديدة، تُقرأ لاحقاً كأي متغيّر حاوية آخر.

## التطبيق
```bash
cd rinlang-main   # جذر مشروعك (نفس بنية الملفات أعلاه)
git apply chatbot_container.patch   # أو patch -p0 < chatbot_container.patch
```
جرّب `examples/chatbot_container_demo.rin` بعد التطبيق (`rin_run examples/chatbot_container_demo.rin`).

## لم يُنفَّذ بعد (خطوات لاحقة، حسب الترتيب اللي حددناه)
- عناصر Loom للعرض (`@view.Chat`, `@view.ChatInput`, ربط `markdown=true`/`code=true`) — تحتاج
  تعديل بملفات `loom/*.h` والـ Kotlin (`LoomFabricView.kt` وغيره). **ملاحظة مهمة:** فحصت
  `rin_ast.h`/`rin_parser.cpp` ولقيت إن `@view.<AnyKind>=` أصلاً عام تماماً (بلا قائمة مسموحة
  بالـ parser) — يعني `@view.ChatHistory=` و`@view.ChatInput=` بالفعل يُقرآن بنجاح اليوم بلا أي
  تعديل، ويصلان لـ `rin_loom_strand.h` كـ `StrandKind::CUSTOM` (نظام Bolt plugin الموجود أصلاً،
  انظر `strandKindFromTag`). كذلك `Input`/`TextArea` (بما فيها `placeholder=`) و`Button` جاهزان
  فعلاً كعناصر Loom قياسية — فصندوق الإدخال وزر الإرسال ممكن تركيبهم اليوم بدون أي كود جديد.
  المتبقي الحقيقي هو: (أ) `StrandKind` جديد مخصَّص لعرض قائمة رسائل تفاعلية مربوطة بـ
  `chatHistory()`/`onChat` (بدل تركيبها يدوياً)، و(ب) عرض Markdown/كود فعلي داخل الفقاعة على
  مستوى الـ Kotlin renderer.
- ~~`exportChat(container, "png"|"text")`~~ ✅ **تم تنفيذها** (نفس آلية `save format=png`
  الموجودة لـ `container.table`، لكن برسم فقاعات محادثة حقيقية بدل شبكة خلايا — انظر
  `buildChatPng` في `rin_interpreter.cpp`).
- ~~`markdown`/`code` (تنسيق الرسالة)~~ ✅ **تم تنفيذها على مستوى المفسّر**: `sendMessage`/
  `botReply` يقبلان وسيطاً رابعاً اختيارياً `format` (`"text"`/`"markdown"`/`"code"`)، بالإضافة
  لاختصارين جاهزين `botReplyMarkdown(container, text)` و`botReplyCode(container, code, language?)`.
  القيمة تُخزَّن في حقل `format` بكل رسالة (`chatHistory()`/`lastChatMessage()`). **الرسم الفعلي**
  لِـ Markdown/كتلة كود ملوّنة داخل الفقاعة نفسها لا يزال يحتاج منطق عرض على مستوى الـ Kotlin
  renderer (البند التالي).
- ~~`stream=true` (ردّ تدريجي)~~ ✅ **منطق المفسّر منفَّذ**: دالة جديدة `streamReply(container,
  text, chunkSize?)` تضيف فقاعة بوت واحدة ثم تُحدِّثها بالمرجع جزءاً فجزء (بدل فقاعة جديدة لكل
  جزء)، وتُطلق `onChat(container, "message", fn)` مع كل تحديث فيكفي لطبقة العرض إعادة رسم آخر
  رسالة فقط. **الربط الفعلي** بعرض القطع تدريجياً على الشاشة (بدل إعادة قراءة `chatHistory()`
  كاملة) لا يزال يحتاج كود Kotlin في `LoomFabricView.kt` يستمع لحدث `"message"` هذا ويحدّث نفس
  الفقاعة بدل إعادة بناء القائمة.
- عناصر Loom للعرض (`@view.Chat`, `@view.ChatInput`, ربط `format` بالرسم الفعلي) — تحتاج
  تعديل بملفات `loom/*.h` والـ Kotlin (`LoomFabricView.kt` وغيره). **ملاحظة مهمة:** فحصت
  `rin_ast.h`/`rin_parser.cpp` ولقيت إن `@view.<AnyKind>=` أصلاً عام تماماً (بلا قائمة مسموحة
  بالـ parser) — يعني `@view.ChatHistory=` و`@view.ChatInput=` بالفعل يُقرآن بنجاح اليوم بلا أي
  تعديل، ويصلان لـ `rin_loom_strand.h` كـ `StrandKind::CUSTOM` (نظام Bolt plugin الموجود أصلاً،
  انظر `strandKindFromTag`). كذلك `Input`/`TextArea` (بما فيها `placeholder=`) و`Button` جاهزان
  فعلاً كعناصر Loom قياسية — فصندوق الإدخال (`input`) وزر الإرسال (`send_button`) و`placeholder`
  ممكن تركيبهم اليوم بدون أي كود جديد، بربطهم يدوياً بـ `sendMessage`/`chatHistory` عبر Rin عادي.
  المتبقي الحقيقي هو: (أ) `StrandKind` جديد مخصَّص لعرض قائمة رسائل تفاعلية مربوطة تلقائياً بـ
  `chatHistory()`/`onChat` (بدل تركيبها يدوياً)، و(ب) عرض Markdown/كود فعلي (تلوين/تنسيق) داخل
  الفقاعة على مستوى الـ Kotlin renderer، مستفيداً من حقل `format` الجديد أعلاه.


## مثال UI جاهز (بلا تعديل محرك Loom) — `examples/chatbot_loom_ui_demo.rin`
بناءً على طلب صريح بعدم لمس محرك Loom (`loom/*.h`)، أُضيف مثال متكامل يستخدم `Input`(`TextArea`)/
`Button`/`placeholder` الموجودة أصلاً + `warp`/`onTap` (نفس الآلية المُختبَرة في
`tools/test_loom_button.cpp`) لعرض 4 فقاعات رسائل بعدد ثابت، صندوق كتابة، وزر إرسال — تفاعل حقيقي
100% (مُتحقَّق منه فعلياً في `tools/test_chatbot_loom_ui_demo.cpp`، كل الفحوصات ✅ ناجحة).

**فحصت الكود فعلياً (مو تخمين) واكتشفت فجوة حقيقية:** `onTap` يمرّ عبر
`loom::dispatchTap -> Interpreter::callTopLevelFunction`، وهذه الأخيرة تُنشئ `rin::Interpreter`
**جديدة تماماً لكل نقرة** تحمّل فيها الدوال (`fun`) وخلايا `warp` فقط — **بلا** تنفيذ أي
`@container.chatbot=` معرَّف بنفس الملف. يعني نداء `sendMessage()`/`botReply()` مباشرة من داخل
دالة `onTap` سيفشل اليوم (الحاوية غير مسجَّلة بتلك الـ Interpreter المؤقّتة).

لهذا فُصلت الطبقتان بوضوح بالمثال: طبقة عرض (Loom/warp/onTap، حقيقية ومُختبَرة) وطبقة تخزين
(`container.chatbot`، حقيقية ومُختبَرة أيضاً لكن مستقلة) — تماماً كما طلبت. ربط الطبقتين فعلياً
(بحيث نفس اللمسة تُحدِّث الـ UI **و** تُسجِّل بتاريخ `chatHistory()` الحقيقي معاً) يحتاج تعديل
الجسر بين Kotlin/JNI ومحرّك Loom على أندرويد (`LoomFabricView.kt`)، وهو تعديل لمحرّك Loom نفسه —
خارج نطاق هذه الجولة عمداً بناءً على اختيارك.

✅ ملاحظة بناء: تم الآن فعلاً بناء المشروع (CLI لينكس، `cli/linux`) محلياً من الصفر وتشغيل
`examples/chatbot_container_demo.rin` بنجاح فوق التعديلات أعلاه — بما فيها كل الدوال الجديدة
(`sendMessage(...,format)`, `botReplyMarkdown`, `botReplyCode`, `streamReply`). أثناء ذلك تبيّن
وتم تصحيح خطأ سابق (غير متعلّق بالشات بوت) في توقيع `Interpreter::buildSaveDocument` في
`rin_interpreter.cpp` كان يمنع تجميع الملف بالكامل.
