# تحديث نمط شاشة «الملفات» (RinStudio)

انسخ المجلد `app/` فوق مشروعك (يستبدل/يضيف الملفات بنفس المسارات).

## جديد
- res/values/colors_files.xml + values-night/colors_files.xml  (ألوان rin_files_* معزولة، لا تعتمد على غيرها)
- res/drawable/bg_file_*.xml , ic_file_trash.xml
- java/.../FileRowStyle.kt  (مصغّرات الصور والفيديو)

## معدَّل
- res/layout/activity_files.xml , item_file.xml , item_folder.xml
- java/.../FilesActivity.kt  (الشرائح، خلفيات التحديد، ربط المصغّرات)

لم تُعدَّل أي ملفات أخرى (الشاشات الأخرى تبقى كما هي).
