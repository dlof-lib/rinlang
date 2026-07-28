package com.dlof.rinlang.store.extensions

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.util.Base64
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * كل منطق "تجميع" إضافة يرفعها مستخدم عادي إلى "Rin Extensions Marketplace": ضغط ملفات
 * محتوى الإضافة التي اختارها الناشر عبر SAF داخل أرشيف zip واحد ثم ترميزه base64 (بنفس فلسفة
 * [com.dlof.rinlang.store.PackagingUtils] لحزم المكتبات — بلا Firebase Storage مدفوع)، وتحويل
 * لقطات الشاشة المُختارة إلى [ExtensionScreenshot] مضغوطة الحجم لتناسب حدود Realtime Database.
 */
object ExtensionPackagingUtils {

    /** أقصى بُعد (طول/عرض) لصورة لقطة شاشة واحدة بعد إعادة تحجيمها، لضبط حجمها قبل الترميز. */
    private const val SCREENSHOT_MAX_DIMENSION = 1080

    /**
     * يضغط كل ملفات [contentUris] (بأسمائها الأصلية، في جذر الأرشيف) داخل zip واحد في cacheDir
     * ويرجع الملف الناتج، جاهزاً لترميزه base64 عبر [encodeFileToBase64].
     */
    fun buildExtensionZip(context: Context, extensionName: String, contentUris: List<Uri>): File {
        val cacheDir = File(context.cacheDir, "extension_publish").apply { mkdirs() }
        val zipFile = File(cacheDir, "$extensionName.zip")
        if (zipFile.exists()) zipFile.delete()

        val usedNames = mutableSetOf<String>()
        ZipOutputStream(BufferedOutputStream(FileOutputStream(zipFile))).use { zipOut ->
            contentUris.forEachIndexed { index, uri ->
                val rawName = queryDisplayName(context, uri) ?: "file_$index"
                val entryName = uniqueName(rawName, usedNames)
                context.contentResolver.openInputStream(uri)?.use { input ->
                    zipOut.putNextEntry(ZipEntry(entryName))
                    input.copyTo(zipOut)
                    zipOut.closeEntry()
                }
            }
        }
        return zipFile
    }

    fun encodeFileToBase64(file: File): String =
        Base64.encodeToString(file.readBytes(), Base64.NO_WRAP)

    /**
     * يقرأ صورة [uri]، يعيد تحجيمها إن كانت أكبر من [SCREENSHOT_MAX_DIMENSION] (حفاظاً على حجم
     * معقول داخل Realtime Database)، ويرجعها كنص base64 مضغوط JPEG، أو null إن تعذّرت القراءة.
     */
    fun encodeScreenshot(context: Context, uri: Uri): String? = try {
        val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return null
        val original = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return null
        val scaled = downscaleIfNeeded(original)
        val out = ByteArrayOutputStream()
        scaled.compress(Bitmap.CompressFormat.JPEG, 82, out)
        Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP)
    } catch (t: Throwable) {
        null
    }

    private fun downscaleIfNeeded(bitmap: Bitmap): Bitmap {
        val largestSide = maxOf(bitmap.width, bitmap.height)
        if (largestSide <= SCREENSHOT_MAX_DIMENSION) return bitmap
        val scale = SCREENSHOT_MAX_DIMENSION.toFloat() / largestSide.toFloat()
        val width = (bitmap.width * scale).toInt().coerceAtLeast(1)
        val height = (bitmap.height * scale).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(bitmap, width, height, true)
    }

    /** يضمن عدم تكرار اسم ملف داخل الأرشيف (يضيف لاحقة رقمية عند التعارض). */
    private fun uniqueName(rawName: String, used: MutableSet<String>): String {
        var candidate = rawName
        var counter = 1
        while (!used.add(candidate)) {
            val dot = rawName.lastIndexOf('.')
            candidate = if (dot > 0) "${rawName.substring(0, dot)}_$counter${rawName.substring(dot)}"
            else "${rawName}_$counter"
            counter++
        }
        return candidate
    }

    private fun queryDisplayName(context: Context, uri: Uri): String? = try {
        context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) cursor.getString(idx) else null
                } else null
            }
    } catch (t: Throwable) {
        null
    }
}
