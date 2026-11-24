# RinStudio — ميزات التنزيل + الأذونات

> تُطبَّق فوق: rinlang-main.zip ثم rinstudio-files-features-v4.zip (الملفات FilesActivity/FileTools/FilesExtras هنا مبنية على نسخة v4).

## تنزيل (Downloads/RinStudio عبر MediaStore — بلا إذن تخزين)
- صفحة الملفات: زر تنزيل في شريط التحديد؛ «تنزيل إلى الجهاز» و«تنزيل كـ ZIP» في قائمة الصف وقائمة «المزيد»؛
  «تنزيل المشروع كاملاً» و«تنزيل المجلد الحالي» و«تنزيل من رابط» في أدوات المشروع؛ «تنزيل من رابط» في قائمة الرفع.
- صفحة المشاريع: «تنزيل المشروع كاملاً (ZIP)» في قائمة البطاقة، وزر تنزيل في شريط التحديد لعدة مشاريع.
- ملف واحد يُحفظ كما هو، وغيره يُحفظ ZIP؛ تقدّم حقيقي بالبايتات؛ بعد الحفظ: فتح/مشاركة + إشعار اكتمال.
- التنزيل من رابط: https فقط، تحويلات يدوية (حتى 5)، ملف مؤقت .part، زر إلغاء، اسم فريد عند التعارض.

## الأذونات
- مركز الأذونات: الإعدادات ← «الأذونات» (الإشعارات، تثبيت التطبيقات، التخزين، الإنترنت).
- إذن الإشعارات يُطلب مرة واحدة بعد أول تنزيل ناجح مع شرح السبب.
- لم يُضف: READ/WRITE_EXTERNAL_STORAGE وMANAGE_EXTERNAL_STORAGE وREAD_MEDIA_* وCAMERA — غير لازمة (MediaStore + منتقي النظام + تطبيق الكاميرا).
- AndroidManifest.xml لم يتغيّر: كل الأذونات المطلوبة (INTERNET، ACCESS_NETWORK_STATE، POST_NOTIFICATIONS، REQUEST_INSTALL_PACKAGES) مصرّحة فيه أصلاً.

## ملفات
جديد: FilesDownload.kt، permissions/RinPermissions.kt، strings_downloads.xml (×4)
معدّل: RinDownloadProgressDialog (عنوان/حالة/إلغاء)، FileTools، FilesExtras، FilesActivity، ProjectsActivity، SettingsActivity، activity_files.xml، activity_projects.xml، menu_project_actions.xml
