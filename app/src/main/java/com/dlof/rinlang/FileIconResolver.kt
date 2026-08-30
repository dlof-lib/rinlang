package com.dlof.rinlang

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.os.Handler
import android.os.Looper
import android.widget.ImageView
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * يحدّد أيقونة "حقيقية" لأي ملف يُرفع للمشروع (صورة/فيديو/خط/ملف لغة برمجة أخرى مثل
 * .py .js .html .cpp .java ...)، بدل عرض أيقونة .rin العامة نفسها للجميع كما كان سابقاً.
 *
 * الاستراتيجية حسب نوع الملف:
 *  - صورة (jpg/png/webp/gif...)  -> مصغّرة (thumbnail) حقيقية من محتوى الملف نفسه، محلياً بلا شبكة.
 *  - فيديو (mp4/mkv/webm...)     -> إطار مصغّر حقيقي من الفيديو نفسه عبر MediaMetadataRetriever، محلياً بلا شبكة.
 *  - .rin                        -> الأيقونة المضمَّنة أصلاً (ic_rin_file).
 *  - أي امتداد آخر معروف (خط أو
 *    لغة برمجة: py/js/ts/html/css/java/kt/c/cpp/json/xml/...) -> الأيقونة "الرسمية" الحقيقية
 *    لذلك النوع، تُجلَب عبر Iconify API العام (https://api.iconify.design) وتُخزَّن على القرص
 *    (icon_cache/) لتُستخدم من الكاش في المرات القادمة بلا اتصال متكرر.
 *  - غير معروف                   -> أيقونة ملف عامة محلية.
 *
 * الاستخدام: FileIconResolver.load(imageView, file)
 */
object FileIconResolver {

    private val mainHandler = Handler(Looper.getMainLooper())

    // امتداد -> (iconify prefix, iconify name) لمجموعة أيقونات لغات/ملفات شهيرة (logos set الحقيقية،
    // نفس شعارات كل لغة الرسمية: شعار بايثون الحقيقي، شعار JS الحقيقي...إلخ)
    private val extensionToIconifyIcon: Map<String, Pair<String, String>> = mapOf(
        "py" to ("logos" to "python"),
        "js" to ("logos" to "javascript"),
        "mjs" to ("logos" to "javascript"),
        "ts" to ("logos" to "typescript-icon"),
        "jsx" to ("logos" to "react"),
        "tsx" to ("logos" to "react"),
        "html" to ("logos" to "html-5"),
        "htm" to ("logos" to "html-5"),
        "css" to ("logos" to "css-3"),
        "cpp" to ("logos" to "c-plusplus"),
        "cc" to ("logos" to "c-plusplus"),
        "cxx" to ("logos" to "c-plusplus"),
        "hpp" to ("logos" to "c-plusplus"),
        "c" to ("logos" to "c"),
        "h" to ("logos" to "c"),
        "java" to ("logos" to "java"),
        "kt" to ("logos" to "kotlin"),
        "kts" to ("logos" to "kotlin"),
        "swift" to ("logos" to "swift"),
        "go" to ("logos" to "go"),
        "rs" to ("logos" to "rust"),
        "rb" to ("logos" to "ruby"),
        "php" to ("logos" to "php"),
        "cs" to ("logos" to "c-sharp"),
        "json" to ("vscode-icons" to "file-type-json"),
        "xml" to ("vscode-icons" to "file-type-xml"),
        "yml" to ("vscode-icons" to "file-type-yaml"),
        "yaml" to ("vscode-icons" to "file-type-yaml"),
        "md" to ("vscode-icons" to "file-type-markdown"),
        "sql" to ("vscode-icons" to "file-type-sql"),
        "sh" to ("vscode-icons" to "file-type-shell"),
        // خطوط: لا شعار "لغة" لها، لكن نستخدم أيقونة خط حقيقية موحّدة من نفس المزوّد
        "ttf" to ("vscode-icons" to "file-type-font"),
        "otf" to ("vscode-icons" to "file-type-font"),
        "woff" to ("vscode-icons" to "file-type-font"),
        "woff2" to ("vscode-icons" to "file-type-font")
    )

    private val imageExtensions = setOf("jpg", "jpeg", "png", "webp", "gif", "bmp", "heic")
    private val videoExtensions = setOf("mp4", "mkv", "webm", "3gp", "mov", "avi")

    /** يحمّل الأيقونة/المصغّرة المناسبة لملف [file] داخل [imageView]، بشكل غير متزامن. */
    fun load(imageView: ImageView, file: File) {
        val ext = file.extension.lowercase()

        // 0) project.og.urin: ملف بيانات وصفية خاص (حاوية مختومة)، له أيقونة مميّزة عن أي ملف
        //    .rin عادي حتى يتضح أنه يُدار تلقائياً وليس كوداً يُعدَّل يدوياً.
        if (file.name == "project.og.urin") {
            imageView.setImageResource(R.drawable.ic_project_meta_container)
            return
        }

        // 1) .rin -> الأيقونة المضمَّنة كما كانت دائماً، بلا أي عمل إضافي.
        if (ext == "rin" || ext.isEmpty()) {
            imageView.setImageResource(R.drawable.ic_rin_file)
            return
        }

        // نضع أيقونة افتراضية فوراً (بلا وميض فراغ) بينما يُحضَّر أي شيء أدق بالخلفية.
        imageView.setImageResource(R.drawable.ic_rin_stack)
        // نربط الطلب بالـ ImageView نفسه لتفادي "تسرّب" نتيجة متأخرة لعنصر أعيد تدويره لملف آخر.
        val requestTag = file.absolutePath
        imageView.tag = requestTag

        Thread {
            val bitmap: Bitmap? = when {
                ext in imageExtensions -> decodeImageThumbnail(file)
                ext in videoExtensions -> decodeVideoThumbnail(file)
                else -> loadIconifyBitmap(imageView.context, ext)
            }
            mainHandler.post {
                if (imageView.tag == requestTag) {
                    if (bitmap != null) imageView.setImageBitmap(bitmap)
                    // فشل الجلب (لا اتصال مثلاً) -> تبقى ic_rin_stack الافتراضية، بلا كسر للواجهة.
                }
            }
        }.start()
    }

    private fun decodeImageThumbnail(file: File): Bitmap? = try {
        val opts = BitmapFactory.Options().apply { inSampleSize = 4 }
        BitmapFactory.decodeFile(file.absolutePath, opts)
    } catch (t: Throwable) {
        null
    }

    private fun decodeVideoThumbnail(file: File): Bitmap? = try {
        MediaMetadataRetriever().use { retriever ->
            retriever.setDataSource(file.absolutePath)
            retriever.frameAtTime
        }
    } catch (t: Throwable) {
        null
    }

    /** يجلب الأيقونة الرسمية عبر Iconify API، ويخزّنها محلياً (icon_cache/) لإعادة الاستخدام. */
    private fun loadIconifyBitmap(context: Context, ext: String): Bitmap? {
        val (prefix, name) = extensionToIconifyIcon[ext] ?: return null
        val cacheDir = File(context.cacheDir, "icon_cache").apply { mkdirs() }
        val cacheFile = File(cacheDir, "$prefix-$name.png")

        if (cacheFile.exists()) {
            return BitmapFactory.decodeFile(cacheFile.absolutePath)
        }

        return try {
            val url = URL("https://api.iconify.design/$prefix/$name.png?height=96")
            val connection = (url.openConnection() as HttpURLConnection).apply {
                connectTimeout = 4000
                readTimeout = 4000
                requestMethod = "GET"
            }
            connection.inputStream.use { input ->
                val bytes = input.readBytes()
                FileOutputStream(cacheFile).use { it.write(bytes) }
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            }
        } catch (t: Throwable) {
            null // بلا اتصال أو فشل الطلب: يبقى العنصر بالأيقونة الافتراضية، لا استثناء يُرمى للواجهة
        }
    }

    private inline fun MediaMetadataRetriever.use(block: (MediaMetadataRetriever) -> Bitmap?): Bitmap? {
        return try {
            block(this)
        } finally {
            release()
        }
    }
}
