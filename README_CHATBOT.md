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
  تعديل بملفات `loom/*.h` والـ Kotlin (`LoomFabricView.kt` وغيره).
- `exportChat(container, "png"|"text")` — نفس آلية `save format=png` الموجودة لـ `container.table`،
  لكن برسم فقاعات محادثة بدل شبكة خلايا.
- `stream=true` (ردّ تدريجي) — يحتاج ربط بطبقة الواجهة لعرض القطع أثناء وصولها، وليس فقط منطق المفسّر.

⚠️ ملاحظة: لم أستطع بناء (compile) المشروع فعلياً هنا (بيئة بلا Android SDK/NDK ولا اتصال شبكة)،
فراجع الباتش قبل الدمج النهائي رغم إني اتّبعت نفس الأنماط والتوقيعات الموجودة بالمشروع بدقة.
