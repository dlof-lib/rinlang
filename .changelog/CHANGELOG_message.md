## message — نظام الرسائل الموحّد (أقوى + متكامل)

إضافة بحتة بالكامل. القيمة `message` تصبح الأساس المشترك بين:

- **الطباعة** (أيقونات + code + meta)
- **الأخطاء** (`throw msg! ...` ↔ `catch` يعيد message غنيّ)
- **Banner** (`banner(m)` يختار Info/Success/Warning/Error تلقائياً)
- **Dialog** (رسالة الحوار يمكن أن تكون قيمة message)
- **البث والسجل** (`emit` + `on message` + `messages()`)

### البناء والاختصارات
- `message(level, body, meta?, code?)`
- اختصارات سياقية غير محجوزة: `msg` · `msg!` · `msg?` · `msg+` · `msg#`
- دعم `code "E.."`, meta، وقوالب `{key}` من الـ meta.

### دوال جديدة
`is_message` · `message_level/body/meta/code/ts/rank` · `as_message` · `as_error` · `banner` · `emit` · `messages` · `clear_messages` · `message_min_level` · `message_style`.

### خصائص التصميم
- تفكيك مبكر في المحلّل → لا تكلفة على RinVM.
- لا كلمات محجوزة حقيقية → توافق خلفي كامل.
- السجل و`on message` خلف راية تُفعَّل عند أول استخدام.
- المستويات مرتّبة: debug < info < success < warning < error.

التوثيق: `docs/message.md`  
الاختبارات: `tests/print/message.rin` + `tests/verification/message_integration.rin`
