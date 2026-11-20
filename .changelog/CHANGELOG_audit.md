## تدقيق الصلاحيات (`audit`) — سجل فحص وتتبع الوصول

إضافة بحتة فوق نظام التوكنات.

- تفعيل/إيقاف: `audit_on` / `audit_off` / مستويات `minimal|detailed|full`
- فحص مع تسجيل: `audit_check` · `audit_require` (يرمي عند الرفض)
- استعلام: `audit_entries` (فلاتر) · `audit_summary` · `audit_report`
- تكامل: كل رفض يمكن بثّه كـ `message`، وسجلات التدقيق تتحول إلى رسائل عبر `as_message`
- دعم الحاويات: `audit true;` داخل `@container` يضيف اسم الحاوية والدالة تلقائياً
- سجل حلقي محدود + تنظيف `audit_clear` / `audit_purge`

التوثيق: `docs/permission-audit.md`  
الاختبارات: `tests/print/audit.rin` + `tests/verification/audit_integration.rin`
