## نظام التوكنات — صلاحيات وجلسات ورسائل موقَّعة

إضافة بحتة. قيمة `token` منظمة تحمل subject + scopes + عمر + توقيع داخلي بسيط.

### القدرات الرئيسية
- إنشاء: `token(subject, scopes, opts)` + اختصار سياقي `tok`
- فحص: `token_ok` · `token_has` · `token_require` (يرمي message)
- إدارة: `token_revoke` · `token_refresh` · `token_narrow`
- تكامل مع `message`: `msg ... with t` و`message_token(m)`
- تكامل مع الحاويات: `need token` + `allow`/`deny`
- أحداث: `token_event` قابلة لـ `emit`
- مخزن جلسة بسيط: `token_store` / `token_load` / `token_purge_expired`

### أكواد الخطأ القياسية
`E_TOKEN_EXPIRED` · `E_TOKEN_REVOKED` · `E_TOKEN_SCOPE` · `E_TOKEN_INVALID`

التوثيق: `docs/token-system.md`  
الاختبارات: `tests/print/token.rin` + `tests/verification/token_integration.rin`
