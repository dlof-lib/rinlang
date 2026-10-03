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
        "zip" to ("vscode-icons" to "file-type-zip"),
        "rar" to ("vscode-icons" to "file-type-zip"),
        "7z" to ("vscode-icons" to "file-type-zip"),
        "tar" to ("vscode-icons" to "file-type-zip"),
        "gz" to ("vscode-icons" to "file-type-zip"),
        "pdf" to ("vscode-icons" to "file-type-pdf2"),
        "txt" to ("vscode-icons" to "file-type-text"),
        "toml" to ("vscode-icons" to "file-type-toml"),
        "svg" to ("vscode-icons" to "file-type-svg"),
        "dart" to ("logos" to "dart"),
        "lua" to ("vscode-icons" to "file-type-lua"),
        "scss" to ("logos" to "sass"),
        "vue" to ("logos" to "vue"),
        // خطوط: لا شعار "لغة" لها، لكن نستخدم أيقونة خط حقيقية موحّدة من نفس المزوّد
        "ttf" to ("vscode-icons" to "file-type-font"),
        "otf" to ("vscode-icons" to "file-type-font"),
        "woff" to ("vscode-icons" to "file-type-font"),
        "woff2" to ("vscode-icons" to "file-type-font")
    )

    /**
     * الشعارات الرسمية المعروفة للغات وأنواع الملفات كـ vector محلي بلون هوية كل منها (المسارات من
     * Simple Icons CC0 وMaterial Design Icons وFont Awesome Free): فورية وبلا شبكة. ما له أيقونة هنا
     * لا يُجلب من الشبكة؛ أما [extensionToIconifyIcon] فاحتياط لامتدادات بلا أيقونة محلية.
     */
    private val extensionToLocalIcon: Map<String, Int> = mapOf(
        "py" to R.drawable.ic_lang_python, "pyw" to R.drawable.ic_lang_python,
        "ts" to R.drawable.ic_lang_ts, "jsx" to R.drawable.ic_lang_react, "tsx" to R.drawable.ic_lang_react,
        "java" to R.drawable.ic_lang_java,
        "kt" to R.drawable.ic_lang_kotlin, "kts" to R.drawable.ic_lang_kotlin,
        "swift" to R.drawable.ic_lang_swift, "go" to R.drawable.ic_lang_go, "rs" to R.drawable.ic_lang_rust,
        "rb" to R.drawable.ic_lang_ruby, "php" to R.drawable.ic_lang_php, "cs" to R.drawable.ic_lang_csharp,
        "c" to R.drawable.ic_lang_c, "cpp" to R.drawable.ic_lang_cpp, "cc" to R.drawable.ic_lang_cpp,
        "cxx" to R.drawable.ic_lang_cpp, "h" to R.drawable.ic_lang_header, "hpp" to R.drawable.ic_lang_header,
        "dart" to R.drawable.ic_lang_dart, "lua" to R.drawable.ic_lang_lua,
        "scss" to R.drawable.ic_lang_scss, "sass" to R.drawable.ic_lang_scss, "vue" to R.drawable.ic_lang_vue,
        "svg" to R.drawable.ic_lang_svg,
        "json" to R.drawable.ic_lang_json, "xml" to R.drawable.ic_lang_xml,
        "yml" to R.drawable.ic_lang_yaml, "yaml" to R.drawable.ic_lang_yaml,
        "toml" to R.drawable.ic_lang_toml,
        "ini" to R.drawable.ic_lang_ini, "cfg" to R.drawable.ic_lang_ini, "conf" to R.drawable.ic_lang_ini,
        "md" to R.drawable.ic_lang_markdown, "markdown" to R.drawable.ic_lang_markdown,
        "txt" to R.drawable.ic_lang_txt, "log" to R.drawable.ic_lang_log,
        "csv" to R.drawable.ic_lang_csv, "tsv" to R.drawable.ic_lang_csv,
        "sql" to R.drawable.ic_lang_sql, "sh" to R.drawable.ic_lang_shell, "bash" to R.drawable.ic_lang_shell,
        "zip" to R.drawable.ic_lang_archive, "rar" to R.drawable.ic_lang_archive, "7z" to R.drawable.ic_lang_archive,
        "tar" to R.drawable.ic_lang_archive, "gz" to R.drawable.ic_lang_archive, "tgz" to R.drawable.ic_lang_archive,
        "bz2" to R.drawable.ic_lang_archive, "xz" to R.drawable.ic_lang_archive, "rinproj" to R.drawable.ic_lang_archive,
        "pdf" to R.drawable.ic_lang_pdf,
        "doc" to R.drawable.ic_lang_word, "docx" to R.drawable.ic_lang_word,
        "xls" to R.drawable.ic_lang_excel, "xlsx" to R.drawable.ic_lang_excel,
        "ppt" to R.drawable.ic_lang_ppt, "pptx" to R.drawable.ic_lang_ppt,
        "apk" to R.drawable.ic_lang_apk,
        "ttf" to R.drawable.ic_lang_font, "otf" to R.drawable.ic_lang_font,
        "woff" to R.drawable.ic_lang_font, "woff2" to R.drawable.ic_lang_font,
        "sqlite" to R.drawable.ic_lang_sqlite, "sqlite3" to R.drawable.ic_lang_sqlite, "db" to R.drawable.ic_lang_sqlite,
        "gradle" to R.drawable.ic_lang_gradle, "pl" to R.drawable.ic_lang_perl, "pm" to R.drawable.ic_lang_perl,
        "r" to R.drawable.ic_lang_r, "hs" to R.drawable.ic_lang_haskell,
        "scala" to R.drawable.ic_lang_scala, "sc" to R.drawable.ic_lang_scala,
        "ex" to R.drawable.ic_lang_elixir, "exs" to R.drawable.ic_lang_elixir,
        "zig" to R.drawable.ic_lang_zig, "cjs" to R.drawable.ic_lang_node,
        "mp3" to R.drawable.ic_lang_audio, "wav" to R.drawable.ic_lang_audio, "ogg" to R.drawable.ic_lang_audio,
        "m4a" to R.drawable.ic_lang_audio, "flac" to R.drawable.ic_lang_audio
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

        // 0.5) ملفات مشروع HTML: ملف حاوية الويب الموقَّع (منطق الصفحة) و.html و.css بأيقونات محلية فورية بلا شبكة
        //      (بدل جلب شعارات Iconify التي تفشل دون اتصال).
        // ملف حاوية الويب يُعرَّف بتوقيعه (//! rin:container web) لا باسمه: أي مستخدم قد يسمّي ملفاً container.rin.
        if (ext == "rin" && RinContainerFile.isWebContainer(file)) {
            imageView.setImageResource(R.drawable.ic_container_rin_file)
            return
        }
        if (ext == "html" || ext == "htm") {
            imageView.setImageResource(R.drawable.ic_html_file)
            return
        }
        if (ext == "css") {
            imageView.setImageResource(R.drawable.ic_css_file)
            return
        }
        if (ext == "js" || ext == "mjs") {
            imageView.setImageResource(R.drawable.ic_js_file)
            return
        }

        // 1) .rin -> الأيقونة المضمَّنة كما كانت دائماً، بلا أي عمل إضافي.
        if (ext == "rin" || ext.isEmpty()) {
            imageView.setImageResource(R.drawable.ic_rin_file)
            return
        }

        // 1.5) .illust -> أيقونة "Illust" المحلية المخصصة (لوحة ألوان+فرشاة)، فوراً وبلا شبكة —
        //      نفس فلسفة .rin أعلاه؛ لغة مخصصة معروفة للتطبيق وليست ملف نوع عام يُطلَب من Iconify.
        if (ext == "illust") {
            imageView.setImageResource(R.drawable.ic_illust_file)
            return
        }

        // 1.6) .indsin -> أيقونة indsin المحلية المخصصة (شاشة + كتل تخطيط)، فوراً وبلا شبكة —
        //      نفس فلسفة .rin/.illust أعلاه؛ indsin نظام واجهات رسمي داخل Rin (سابقاً Loom)
        //      لا امتداد ملفات عام يُطلَب من Iconify.
        if (ext == "indsin") {
            imageView.setImageResource(R.drawable.ic_indsin_file)
            return
        }

        // 1.7) .rdoc -> أيقونة Documentation Container المضمَّنة (صفحة + قوسا حاوية)، لا Iconify.
        if (ext == com.dlof.rinlang.store.DocumentationContainer.EXTENSION) {
            imageView.setImageResource(R.drawable.ic_doc_container_file)
            return
        }

        // نضع أيقونة افتراضية فوراً (بلا وميض فراغ) بينما يُحضَّر أي شيء أدق بالخلفية.
        // الشعار الرسمي المحلي إن وُجد: يُعرض فوراً وينتهي الأمر (بلا شبكة).
        extensionToLocalIcon[ext]?.let { imageView.setImageResource(it); return }
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
