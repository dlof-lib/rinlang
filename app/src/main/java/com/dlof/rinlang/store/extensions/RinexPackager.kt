package com.dlof.rinlang.store.extensions

import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.util.Base64
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * التنسيق الخاص بـ "Rin Extensions": ملف مضغوط واحد بامتداد **.rinex**، مستقل تماماً عن Firebase،
 * يحتوي في جذره:
 *   - extension.rinext  → وصف الإضافة كاملاً بصيغة JSON (نفس [ExtensionManifestFile.toJson])
 *   - كل ملفات محتوى الإضافة نفسها، كما رفعها المطوّر عبر [PublishExtensionActivity]
 *
 * هذا هو ما يجعل "Rin Extensions" شكلاً حقيقياً قابلاً للمشاركة والتثبيت بلا اتصال إنترنت
 * (نسخ الملف بين الأجهزة، إرساله عبر تطبيقات المراسلة، إلخ)، وليس فقط عناصر داخل قاعدة بيانات
 * بعيدة — بمعزل تام عن [ExtensionRepository]:
 *   - [exportToDownloads] يبني ملف .rinex من إضافة محمَّلة بالفعل (من المتجر أو مثبَّتة محلياً)
 *     ويحفظه في مجلد Downloads العام على الجهاز.
 *   - [importFromUri] يقرأ ملف .rinex اختاره المستخدم عبر منتقي الملفات، ويعيد بناء [RinExtension]
 *     كاملة منه (جاهزة لتمريرها مباشرة إلى [ExtensionManager.install]) دون أي حاجة لاتصال.
 */
object RinexPackager {

    const val FILE_EXTENSION = "rinex"
    const val MIME_TYPE = "application/octet-stream"

    /** يبني أرشيف .rinex ذاتي الاكتفاء لـ [ext]: محتواها (من base64Data) + extension.rinext. */
    private fun buildRinexBytes(ext: RinExtension): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { zipOut ->
            // 1) نسخ كل ملفات المحتوى الأصلية من أرشيف zip الداخلي (base64Data) كما هي.
            if (ext.base64Data.isNotBlank()) {
                val contentBytes = Base64.decode(ext.base64Data, Base64.DEFAULT)
                ZipInputStream(ByteArrayInputStream(contentBytes)).use { zipIn ->
                    var entry = zipIn.nextEntry
                    while (entry != null) {
                        if (!entry.isDirectory && entry.name != ExtensionManifestFile.MANIFEST_FILE_NAME) {
                            zipOut.putNextEntry(ZipEntry(entry.name))
                            zipIn.copyTo(zipOut)
                            zipOut.closeEntry()
                        }
                        entry = zipIn.nextEntry
                    }
                }
            }
            // 2) وصف الإضافة (extension.rinext) في جذر الأرشيف، ليكون الملف مستقلاً بذاته.
            zipOut.putNextEntry(ZipEntry(ExtensionManifestFile.MANIFEST_FILE_NAME))
            zipOut.write(ExtensionManifestFile.toJson(ext).toString(2).toByteArray(Charsets.UTF_8))
            zipOut.closeEntry()
        }
        return out.toByteArray()
    }

    private fun safeFileName(ext: RinExtension): String {
        val base = ext.name.ifBlank { "extension" }
            .trim()
            .replace(Regex("[\\\\/:*?\"<>|]"), "_")
        val version = ext.version.ifBlank { "1.0.0" }
        return "$base-$version.$FILE_EXTENSION"
    }

    /**
     * يحفظ [ext] كملف .rinex واحد داخل مجلد Downloads العام على الجهاز، بنفس أسلوب
     * [com.dlof.rinlang.RinDownloadManager] (MediaStore على أندرويد 10+، ومسار عام + FileProvider
     * على ما دونه). يستدعي [onDone] بالرابط الناتج، أو بخطأ إن فشلت العملية.
     */
    fun exportToDownloads(activity: Activity, ext: RinExtension, onDone: (uri: Uri?, error: Throwable?) -> Unit) {
        val mainHandler = Handler(Looper.getMainLooper())
        Thread {
            try {
                val bytes = buildRinexBytes(ext)
                val fileName = safeFileName(ext)
                val resolver = activity.contentResolver
                var destUri: Uri?

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    val values = ContentValues().apply {
                        put(MediaStore.Downloads.DISPLAY_NAME, fileName)
                        put(MediaStore.Downloads.MIME_TYPE, MIME_TYPE)
                        put(MediaStore.Downloads.IS_PENDING, 1)
                    }
                    destUri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                        ?: throw java.io.IOException("تعذّر إنشاء الملف داخل Downloads")
                    resolver.openOutputStream(destUri)?.use { it.write(bytes) }
                        ?: throw java.io.IOException("تعذّر فتح مجرى الكتابة")
                    resolver.update(destUri, ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }, null, null)
                } else {
                    @Suppress("DEPRECATION")
                    val downloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                    if (!downloadsDir.exists()) downloadsDir.mkdirs()
                    val destFile = uniqueFile(downloadsDir, fileName)
                    destFile.writeBytes(bytes)
                    destUri = androidx.core.content.FileProvider.getUriForFile(
                        activity, "${activity.packageName}.fileprovider", destFile
                    )
                    @Suppress("DEPRECATION")
                    activity.sendBroadcast(
                        android.content.Intent(
                            android.content.Intent.ACTION_MEDIA_SCANNER_SCAN_FILE,
                            android.net.Uri.fromFile(destFile)
                        )
                    )
                }
                mainHandler.post { onDone(destUri, null) }
            } catch (t: Throwable) {
                mainHandler.post { onDone(null, t) }
            }
        }.start()
    }

    private fun uniqueFile(dir: File, name: String): File {
        var candidate = File(dir, name)
        if (!candidate.exists()) return candidate
        val dot = name.lastIndexOf('.')
        val base = if (dot >= 0) name.substring(0, dot) else name
        val extPart = if (dot >= 0) name.substring(dot) else ""
        var i = 1
        while (candidate.exists()) {
            candidate = File(dir, "$base ($i)$extPart")
            i++
        }
        return candidate
    }

    /**
     * يقرأ ملف .rinex من [uri] (اختاره المستخدم عبر منتقي الملفات) ويعيد بناء [RinExtension]
     * كاملة منه، جاهزة لتمريرها مباشرة إلى [ExtensionManager.install] — بلا أي اتصال بالإنترنت.
     *
     * إن وُجد extension.rinext داخل الأرشيف نُعيد بناء كل الحقول منه. إن لم يوجد (أرشيف عام لا
     * يتبع تنسيق Rin Extensions) نبني إضافة افتراضية باسم الملف، بلا توقيع رقمي (تُعرَض للمستخدم
     * صراحة كـ"غير موقَّعة" في شاشة الأمان قبل التثبيت).
     */
    fun importFromUri(context: Context, uri: Uri, displayName: String?): RinExtension? {
        val rawBytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return null
        if (rawBytes.isEmpty()) return null

        var manifestJson: org.json.JSONObject? = null
        try {
            ZipInputStream(ByteArrayInputStream(rawBytes)).use { zipIn ->
                var entry = zipIn.nextEntry
                while (entry != null) {
                    if (!entry.isDirectory && entry.name == ExtensionManifestFile.MANIFEST_FILE_NAME) {
                        manifestJson = org.json.JSONObject(zipIn.readBytes().toString(Charsets.UTF_8))
                        break
                    }
                    entry = zipIn.nextEntry
                }
            }
        } catch (_: Throwable) {
            // أرشيف غير صالح كـ zip على الإطلاق — يُعامَل لاحقاً كفشل استيراد من الشاشة المستدعية.
            return null
        }

        val base64 = Base64.encodeToString(rawBytes, Base64.NO_WRAP)
        val fallbackName = (displayName ?: "extension").removeSuffix(".$FILE_EXTENSION").ifBlank { "extension" }

        val imported = manifestJson?.let { ExtensionManifestFile.fromJson(it, base64Data = base64) }
            ?: RinExtension(name = fallbackName, developer = "", base64Data = base64)

        // معرّف محلي مستقر (لا يصطدم مع معرّفات المتجر التي يولّدها Firebase push())، ومسح أي
        // توقيع رقمي قديم لأن محتوى الأرشيف المُعاد بناؤه هنا يشمل extension.rinext وليس مطابقاً
        // حرفياً لما وُقِّع وقت النشر الأصلي — فيُعرَض بصدق كإضافة محلية غير موقَّعة.
        val localId = if (imported.id.isNotBlank()) "local_${imported.id}" else "local_${System.currentTimeMillis()}"
        return imported.copy(
            id = localId,
            fileName = safeFileName(imported),
            signature = "",
            sizeBytes = rawBytes.size.toLong()
        )
    }
}
