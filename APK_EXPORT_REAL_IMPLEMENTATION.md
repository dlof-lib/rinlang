# تصدير APK حقيقي — التنفيذ الكامل (لا محاكاة)

انسخ الملفات هنا فوق نظيراتها في المستودع مباشرة (نفس المسارات).

## الملفات
| الملف | الحالة |
|---|---|
| `app/src/main/java/com/dlof/rinlang/apk/RinApkExporter.kt` | استُبدل بالكامل (كان stub فارغاً) — يبني الخط الكامل الآن |
| `app/src/main/java/com/dlof/rinlang/apk/AxmlManifestPatcher.kt` | **جديد** — محرِّر AndroidManifest.xml ثنائي حقيقي |
| `app/src/main/java/com/dlof/rinlang/apk/ApkRepackager.kt` | **جديد** — إعادة تجميع zip + zipalign حقيقي |
| `app/src/main/java/com/dlof/rinlang/apk/ApkV1Signer.kt` | **جديد** — توقيع v1/JAR حقيقي (PKCS#7 يدوي) |
| `app/src/main/java/com/dlof/rinlang/apk/Asn1.kt` | **جديد** — ترميز DER بسيط لبناء CERT.RSA |
| `app/src/main/java/com/dlof/rinlang/apk/RinSigningIdentity.kt` | بلا تغيير (كان حقيقياً بالفعل — RSA-2048 في AndroidKeyStore) |
| `app/src/main/res/values/strings.xml` | سطر واحد فقط مُحدَّث (apk_export_note_limitation) |

`ApkExportActivity.kt` و`ExportedRunActivity.kt` لم يتغيّرا — كانا يستدعيان بالفعل نفس التوقيع
العام لـ `RinApkExporter.export(...)`، فبقيا متوافقين تماماً بلا أي تعديل.

## الفكرة المعمارية
بدل مُصرِّف Dex/aapt2 كامل على الجهاز (غير واقعي في تطبيق أندرويد)، التصدير يعيد تجميع
**نفس ثنائي RinStudio المثبَّت فعلياً على الجهاز** (الذي يحمل أصلاً classes.dex + محرّك Rin
الأصلي + `ExportedRunActivity`)، ويُبدّل فيه فقط:
1. `AndroidManifest.xml` — بايتاً بايت، عبر `AxmlManifestPatcher` (بلا aapt): معرّف حزمة فريد،
   اسم تطبيق معروض جديد، وتفريد كل سلطات `<provider>` (FileProvider/Firebase/...) لتفادي
   تعارض التثبيت مع RinStudio نفسها.
2. حقن ملفات المشروع كـ `assets/rin_export_project/*` + بيان JSON يقرأه `ExportedRunActivity`.
3. إعادة الضغط مع محاذاة zipalign حقيقية (4096 لمكتبات `.so`، 4 لباقي STORED).
4. توقيع v1/JAR حقيقي بمفتاح RSA-2048 من AndroidKeyStore (`RinSigningIdentity`، لم يتغيّر).

## محدوديات معروفة (موثّقة داخل الكود أيضاً)
- توقيع v1 فقط (بلا v2/v3) — كافٍ للتثبيت والتشغيل على كل أندرويد حالي.
- الأيقونة تبقى أيقونة RinStudio (resources.arsc لم يُلمس) — الاسم المعروض فقط يتغيّر.
- لم تُختبر فعلياً على جهاز (بيئة العمل هنا بلا Android SDK) — راجع/اختبر قبل الإصدار للإنتاج،
  خصوصاً محاذاة zipalign والتحقق من التوقيع v1 عبر `apksigner verify` على جهاز حقيقي.
