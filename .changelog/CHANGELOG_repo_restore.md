# استرداد المستودعات (GitHub وغيره)

- جديد: استرداد مستودع GitHub/GitLab/Bitbucket/Codeberg-Gitea-Forgejo أو رابط ZIP إلى مشروع جديد أو داخل مجلد موجود.
- فرع/وسم/commit اختياري مع قائمة اختيار، ومجلد فرعي من رابط `/tree/...`، ورمز وصول اختياري للمستودعات الخاصة (لا يُحفظ).
- ثلاث طرق دمج مع نسخة احتياطية في `.rin_backup/`، وحفظ المصدر في `.rin_repo.json` لـ «تحديث من المصدر».
- ProjectManager: `newImportTempDir`, `installImportedTree`. شاشة المشاريع: «استيراد» صار قائمة (ملف / مستودع).
- ملاحظة: كُتب الكود دون مترجم Kotlin ولا Gradle في بيئة التطوير؛ لم يُبنَ ولم تُشغَّل الاختبارات (RepoCoreTest). شغّل `./gradlew testDebugUnitTest` أولاً.
