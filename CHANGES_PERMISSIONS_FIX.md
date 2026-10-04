# إصلاح: Unresolved reference 'permissions' (2026-10-04)

**الخطأ (compileReleaseKotlin):**
`RinApplication.kt:15:26 Unresolved reference 'permissions'`

**السبب:** `RinApplication` يستدعي `com.dlof.rinlang.permissions.RinNotifications.createChannels(this)`
لكن الحزمة `permissions` لم تكن موجودة في المستودع (على الأرجح لم تُدمج من rinlang-main-permissions.zip).

**الإصلاح:** إنشاء `app/src/main/java/com/dlof/rinlang/permissions/RinNotifications.kt`
بقنوات: عامة، تنزيلات، مهام. آمن للاستدعاء المتكرر ولا يحتاج إذناً.
بناء الـ Tesseract/JitPack من الإصلاح السابق نجح (وصل البناء إلى compileReleaseKotlin).
