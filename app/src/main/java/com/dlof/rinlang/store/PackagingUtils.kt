package com.dlof.rinlang.store

import android.content.Context
import android.net.Uri
import android.util.Base64
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R
import com.dlof.rinlang.RinLibrary
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.Calendar
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * كل منطق "تجميع/تفريغ" حزمة متجر Rin (.og.rinsdk): كل ملفات الحزمة (كود المكتبة +
 * README.md + LICENSE + صور/أيقونات اختيارية) تُضغَط داخل zip واحد، ثم يُرمَّز الملف
 * بالكامل base64 ليُخزَّن كنص واحد داخل Realtime Database (بلا Firebase Storage مدفوع).
 */
object PackagingUtils {

    const val PACKAGE_EXTENSION = "og.rinsdk"

    /**
     * يبني أرشيف الحزمة الكامل في cacheDir ويرجع الملف الناتج (لم يُرمَّز base64 بعد).
     * [extraAssetUris] هي صور/أيقونات اختارها الناشر (SAF)، تُحفَظ داخل مجلد assets/ بالأرشيف.
     */
    fun buildPackageZip(
        context: Context,
        libraryFile: File,
        packageName: String,
        version: String,
        description: String,
        publisherName: String,
        license: String,
        extraAssetUris: List<Uri>,
        dependencies: Map<String, String> = emptyMap(),
        /** ملف README.md اختاره الناشر عبر SAF؛ إن كان null يُولَّد ملف تلقائياً. */
        customReadmeUri: Uri? = null,
        /** ملف ترخيص (LICENSE/LICENSE.txt...) اختاره الناشر عبر SAF؛ إن كان null يُولَّد قالب MIT تلقائياً. */
        customLicenseUri: Uri? = null
    ): File {
        val cacheDir = File(context.cacheDir, "store_publish").apply { mkdirs() }
        val zipFile = File(cacheDir, "$packageName.zip")
        if (zipFile.exists()) zipFile.delete()

        // أسماء الأصول الاختيارية (صور/أيقونات) تُحسَب مسبقاً حتى تظهر ضمن قائمة "محتويات
        // الحزمة" في README.md المُولَّد تلقائياً، بنفس الأسماء التي ستُكتَب فعلياً في الأرشيف.
        val assetNames = extraAssetUris.mapIndexed { index, uri ->
            queryDisplayName(context, uri) ?: "asset_$index"
        }

        ZipOutputStream(BufferedOutputStream(FileOutputStream(zipFile))).use { zipOut ->
            // 1) ملف المكتبة نفسه، بنفس بنية lib/ المستخدمة داخل المشاريع
            zipOut.putNextEntry(ZipEntry("lib/${libraryFile.name}"))
            libraryFile.inputStream().use { it.copyTo(zipOut) }
            zipOut.closeEntry()

            // 2) README.md — يُستخدَم ملف الناشر إن رفع واحداً، وإلا يُولَّد تلقائياً بتصميم
            // احترافي كامل (رأس، معلومات، تبعيات، محتويات الحزمة بأيقونات حسب نوع كل ملف)
            val readme = readTextUriOrNull(context, customReadmeUri)
                ?: buildReadme(packageName, version, publisherName, license, description, libraryFile.name, dependencies, assetNames)
            zipOut.putNextEntry(ZipEntry("README.md"))
            zipOut.write(readme.toByteArray(Charsets.UTF_8))
            zipOut.closeEntry()

            // 3) LICENSE — يُستخدَم ملف الناشر إن رفع واحداً، وإلا يُولَّد قالب MIT تلقائي
            // باسم الناشر والسنة الحالية
            val licenseText = readTextUriOrNull(context, customLicenseUri) ?: buildLicense(publisherName)
            zipOut.putNextEntry(ZipEntry("LICENSE"))
            zipOut.write(licenseText.toByteArray(Charsets.UTF_8))
            zipOut.closeEntry()

            // 4) الصور/الأيقونات الاختيارية
            extraAssetUris.forEachIndexed { index, uri ->
                val displayName = assetNames[index]
                context.contentResolver.openInputStream(uri)?.use { input ->
                    zipOut.putNextEntry(ZipEntry("assets/$displayName"))
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
     * يفكّ ضغط [pkg] (بعد فك ترميز base64) ويثبّت ملف .og.rin واحد موجود داخل lib/ بداخله في
     * lib/ الخاص بـ [project]، ويرجع المكتبة المثبَّتة الجاهزة للاستيراد.
     */
    fun installPackage(context: Context, project: Project, pkg: RinPackage): RinLibrary {
        val bytes = Base64.decode(pkg.base64Data, Base64.NO_WRAP)
        val libDir = ProjectManager.libDir(project)
        var installed: RinLibrary? = null

        ZipInputStream(bytes.inputStream()).use { zip ->
            var entry: ZipEntry? = zip.nextEntry
            while (entry != null) {
                val name = entry.name
                if (!entry.isDirectory && name.startsWith("lib/") && name.endsWith(".og.rin")) {
                    val fileName = name.removePrefix("lib/")
                    val outFile = File(libDir, fileName)
                    BufferedOutputStream(FileOutputStream(outFile)).use { out -> zip.copyTo(out) }
                    installed = RinLibrary(fileName, outFile, outFile.length(), outFile.lastModified())
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        val result = installed ?: throw IllegalStateException("لم يتم العثور على ملف مكتبة صالح داخل الحزمة")
        PackageManifest.recordInstalled(project, pkg.name, pkg.version, result.name)
        return result
    }

    /**
     * يفكّ ضغط [pkg] (بعد فك ترميز base64) ويستخرج محتوى README.md ونص الترخيص وقائمة كل
     * الملفات داخل الأرشيف (بالاسم والحجم)، لعرضها في صفحة تفاصيل الحزمة بشكل شبيه بصفحة
     * مستودع على GitHub، دون تثبيت أي شيء فعلياً في مشروع المستخدم.
     */
    fun readContents(pkg: RinPackage): PackageContents {
        val bytes = try {
            Base64.decode(pkg.base64Data, Base64.NO_WRAP)
        } catch (t: Throwable) {
            return PackageContents(readme = null, license = null, files = emptyList())
        }

        var readme: String? = null
        var license: String? = null
        val files = mutableListOf<PackageFileEntry>()

        ZipInputStream(bytes.inputStream()).use { zip ->
            var entry: ZipEntry? = zip.nextEntry
            while (entry != null) {
                if (!entry.isDirectory) {
                    val content = zip.readBytes()
                    files.add(PackageFileEntry(name = entry.name, sizeBytes = content.size.toLong()))
                    when {
                        entry.name.equals("README.md", ignoreCase = true) ->
                            readme = content.toString(Charsets.UTF_8)
                        entry.name.equals("LICENSE", ignoreCase = true) ||
                            entry.name.equals("LICENSE.txt", ignoreCase = true) ->
                            license = content.toString(Charsets.UTF_8)
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return PackageContents(readme, license, files.sortedBy { it.name })
    }

    /**
     * يُولِّد README.md احترافياً كاملاً لحزمة لم يرفع ناشرها ملف README خاصاً بها: رأس جذّاب،
     * قسم معلومات، وصف، تبعيات (إن وُجدت)، طريقة الاستخدام، ثم قائمة "محتويات الحزمة" الكاملة
     * (المكتبة + README + LICENSE + كل الأصول). النص خالٍ تماماً من الإيموجي — التنظيم يعتمد على
     * عناوين ونقاط وتشديد فقط (كل ما يدعمه [MarkdownLite]: عناوين، **تشديد**، `كود`، قوائم،
     * ```كتل كود```، ---). أيقونات الملفات الفعلية (الرسومات الحقيقية) تُعرَض بدلاً من ذلك في
     * قائمة "ملفات الحزمة" الأصلية داخل شاشة تفاصيل الحزمة عبر [iconResFor].
     */
    private fun buildReadme(
        name: String,
        version: String,
        publisherName: String,
        license: String,
        description: String,
        libraryFileName: String,
        dependencies: Map<String, String> = emptyMap(),
        assetFileNames: List<String> = emptyList()
    ): String {
        val year = Calendar.getInstance().get(Calendar.YEAR)
        val cleanDescription = description.ifBlank { "حزمة Rin جاهزة للاستيراد والاستخدام مباشرة في مشروعك." }

        val dependenciesSection = if (dependencies.isEmpty()) "" else buildString {
            append("\n## التبعيات\n\n")
            append("تحتاج هذه الحزمة قبل تثبيتها إلى توفّر الحزم التالية:\n\n")
            dependencies.entries.forEach { (depName, depVersion) ->
                append("- **$depName** — الإصدار `$depVersion`\n")
            }
        }

        // قائمة "محتويات الحزمة": ملف المكتبة نفسه أولاً، ثم README وLICENSE، ثم كل الأصول
        // الاختيارية. أيقونات هذه الملفات الحقيقية تظهر لاحقاً في قائمة الملفات الأصلية بالتطبيق.
        val contentsSection = buildString {
            append("\n## محتويات الحزمة\n\n")
            append("- `lib/$libraryFileName` — الكود المصدري للمكتبة\n")
            append("- `README.md` — هذا الملف\n")
            append("- `LICENSE` — نص الترخيص\n")
            assetFileNames.forEach { assetName ->
                append("- `assets/$assetName`\n")
            }
        }

        return """
        |# $name
        |
        |**$cleanDescription**
        |
        |---
        |
        |## معلومات الحزمة
        |
        |- **الاسم:** `$name`
        |- **الإصدار:** `$version`
        |- **الناشر:** $publisherName
        |- **الترخيص:** $license
        |- **سنة النشر:** $year
        |
        |## الوصف
        |
        |$cleanDescription
        |$dependenciesSection
        |## طريقة الاستخدام
        |
        |أضف سطر الاستيراد التالي في أعلى ملف Rin الخاص بك:
        |
        |```
        |@import "lib/$libraryFileName";
        |```
        |
        |بعد ذلك، يمكنك مباشرة استخدام كل ما تُصدِّره هذه المكتبة من دوال وعناصر داخل كودك.
        |$contentsSection
        |---
        |
        |### عن هذه الحزمة
        |
        |تم إعداد هذا الملف تلقائياً ونشر الحزمة عبر **متجر Rin (Rin Store)** — تصفّح وثبّت
        |وشارك حزم مجتمع Rin مباشرة من داخل التطبيق، بلا حاجة لأي أداة خارجية.
    """.trimMargin()
    }

    /**
     * يختار أيقونة الرسم الشعاعي (vector drawable) الحقيقية من موارد التطبيق نفسه لتمثيل نوع
     * الملف [fileName] بالاعتماد على امتداده — تُستخدَم في قائمة "ملفات الحزمة" داخل شاشة تفاصيل
     * الحزمة ([PackageDetailActivity]) بدلاً من أي إيموجي.
     */
    fun iconResFor(fileName: String): Int {
        val lower = fileName.lowercase()
        return when {
            lower == "license" || lower == "license.txt" -> R.drawable.ic_license_shield
            lower.endsWith(".og.rin") || lower.endsWith(".rin") -> R.drawable.ic_file_solid
            lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
                lower.endsWith(".webp") || lower.endsWith(".gif") || lower.endsWith(".bmp") -> R.drawable.ic_log_image
            lower.endsWith(".svg") -> R.drawable.ic_log_palette
            lower.endsWith(".json") -> R.drawable.ic_log_grid
            lower.endsWith(".md") -> R.drawable.ic_readme_doc
            lower.endsWith(".txt") -> R.drawable.ic_log_file
            lower.endsWith(".zip") || lower.endsWith(".rinsdk") -> R.drawable.ic_log_archive
            lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".ogg") -> R.drawable.ic_log_audio
            lower.endsWith(".mp4") || lower.endsWith(".webm") -> R.drawable.ic_log_video
            lower.endsWith(".ttf") || lower.endsWith(".otf") -> R.drawable.ic_log_font
            else -> R.drawable.ic_file_solid
        }
    }

    /**
     * يختار لون تمييز مناسباً لنوع الملف [fileName] (نفس تصنيف [iconResFor])، يُستخدَم لتلوين
     * أيقونة الملف وخلفيتها الدائرية الخفيفة في قائمة "ملفات الحزمة"، بدل لون تمييز واحد ثابت
     * لكل أنواع الملفات — لتمييزها بصرياً بسرعة (كود المكتبة بلون التمييز الأساسي، الترخيص
     * بالأخضر، الصور بالوردي، الصوت/الفيديو بالبرتقالي... إلخ).
     */
    fun iconColorResFor(fileName: String): Int {
        val lower = fileName.lowercase()
        return when {
            lower == "license" || lower == "license.txt" -> R.color.rin_accent_green
            lower.endsWith(".og.rin") || lower.endsWith(".rin") -> R.color.rin_accent
            lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
                lower.endsWith(".webp") || lower.endsWith(".gif") || lower.endsWith(".bmp") -> R.color.rin_like_active
            lower.endsWith(".svg") -> R.color.rin_star_gold
            lower.endsWith(".json") -> R.color.syntax_tag
            lower.endsWith(".md") -> R.color.rin_accent
            lower.endsWith(".txt") -> R.color.rin_editor_hint
            lower.endsWith(".zip") || lower.endsWith(".rinsdk") -> R.color.rin_star_gold
            lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".ogg") -> R.color.rin_accent_green
            lower.endsWith(".mp4") || lower.endsWith(".webm") -> R.color.rin_like_active
            lower.endsWith(".ttf") || lower.endsWith(".otf") -> R.color.syntax_tag
            else -> R.color.rin_accent
        }
    }

    /**
     * يبني شجرة مجلدات/ملفات حقيقية من [files] (قائمة مسطَّحة مساراتها الكاملة مثل
     * "lib/table_utils.og.og.rin") بدل عرضها كقائمة مسطَّحة بمسار كامل مكرَّر في كل سطر —
     * تُستخدَم في قسم "ملفات الحزمة" بشاشة تفاصيل الحزمة ([PackageDetailActivity.bindFiles])
     * لعرض شكل مرتَّب شبيه بمستكشف ملفات حقيقي: كل مجلد رأس قسم واحد يظهر مرّة واحدة، والملفات
     * تحته باسمها المجرَّد فقط (بلا تكرار بادئة المسار). المجلدات مرتَّبة أبجدياً قبل الملفات في
     * كل مستوى (تماماً كمستكشف ملفات المشروع في التطبيق)، وكلاهما مرتَّب أبجدياً ضمن نوعه.
     */
    fun buildFileTree(files: List<PackageFileEntry>): FileTreeFolder {
        val root = FileTreeFolder(name = "", path = "")
        for (file in files) {
            val parts = file.name.split("/").filter { it.isNotEmpty() }
            if (parts.isEmpty()) continue
            var current = root
            for (i in 0 until parts.size - 1) {
                val part = parts[i]
                current = current.folders.getOrPut(part) {
                    FileTreeFolder(name = part, path = if (current.path.isEmpty()) part else "${current.path}/$part")
                }
            }
            current.files.add(FileTreeLeaf(simpleName = parts.last(), entry = file))
        }
        root.sortRecursively()
        return root
    }

    /**
     * يصنِّف كل ملف داخل شجرة الحزمة إلى تسمية/لون "نوع محتوى" (بنفس تصنيف [iconColorResFor]
     * تماماً، فتبقى الألوان متّسقة بصرياً بين أيقونات الملفات وشريط التركيبة) — تُستخدَم في
     * [languageBreakdown] فقط، ومنفصلة عنه لتبقى [iconColorResFor] نفسها بلا أي تغيير.
     */
    private fun languageLabelAndColorFor(fileName: String): Pair<String, Int> {
        val lower = fileName.lowercase()
        return when {
            lower.endsWith(".og.rin") || lower.endsWith(".rin") -> "Rin" to R.color.rin_accent
            lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
                lower.endsWith(".webp") || lower.endsWith(".gif") || lower.endsWith(".bmp") -> "صور" to R.color.rin_like_active
            lower.endsWith(".svg") -> "SVG" to R.color.rin_star_gold
            lower.endsWith(".json") -> "JSON" to R.color.syntax_tag
            lower.endsWith(".txt") -> "نصوص" to R.color.rin_editor_hint
            lower.endsWith(".zip") || lower.endsWith(".rinsdk") -> "أرشيف" to R.color.rin_star_gold
            lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".ogg") -> "صوت" to R.color.rin_accent_green
            lower.endsWith(".mp4") || lower.endsWith(".webm") -> "فيديو" to R.color.rin_like_active
            lower.endsWith(".ttf") || lower.endsWith(".otf") -> "خطوط" to R.color.syntax_tag
            else -> "أخرى" to R.color.rin_editor_hint
        }
    }

    /**
     * يحسب "تركيبة ملفات الحزمة" الفعلية (بنفس مبدأ شريط تركيبة اللغات في GitHub) من الحجم
     * الحقيقي بالبايت لكل نوع ملف — لا نسب وهمية. يستثني README.md وLICENSE عمداً (تماماً كما
     * يستثني GitHub التوثيق من إحصاء اللغات) حتى لا يطغى حجم نص التوثيق على حصة كود Rin الفعلي،
     * ولتُبرِز الحصيلة النهائية حصة "Rin" (البنفسجي، هوية التطبيق) بوضوح في أي حزمة كودها Rin
     * أصلاً — انظر PackageDetailActivity.renderFilesLanguageBar.
     */
    fun languageBreakdown(files: List<PackageFileEntry>): List<FileLanguageSlice> {
        val counted = files.filterNot {
            val lower = it.name.lowercase()
            lower == "readme.md" || lower == "license" || lower == "license.txt"
        }
        if (counted.isEmpty()) return emptyList()

        val totals = linkedMapOf<String, Pair<Int, Long>>() // label -> (colorRes, bytes)
        for (file in counted) {
            val (label, colorRes) = languageLabelAndColorFor(file.name)
            val previousBytes = totals[label]?.second ?: 0L
            totals[label] = colorRes to (previousBytes + file.sizeBytes.coerceAtLeast(1))
        }
        val totalBytes = totals.values.sumOf { it.second }.coerceAtLeast(1)
        return totals.entries
            .map { (label, pair) -> FileLanguageSlice(label, pair.first, pair.second, pair.second * 100f / totalBytes) }
            .sortedByDescending { it.bytes }
    }

    private fun buildLicense(publisherName: String): String {
        val year = Calendar.getInstance().get(Calendar.YEAR)
        return """
            |MIT License
            |
            |Copyright (c) $year $publisherName
            |
            |Permission is hereby granted, free of charge, to any person obtaining a copy
            |of this software and associated documentation files (the "Software"), to deal
            |in the Software without restriction, including without limitation the rights
            |to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
            |copies of the Software, and to permit persons to whom the Software is
            |furnished to do so, subject to the following conditions:
            |
            |The above copyright notice and this permission notice shall be included in all
            |copies or substantial portions of the Software.
            |
            |THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
            |IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
            |FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
            |AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
            |LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
            |OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
            |SOFTWARE.
        """.trimMargin()
    }

    /** يقرأ محتوى [uri] كنص UTF-8، أو يرجع null إن كان [uri] فارغاً أو تعذّرت قراءته. */
    private fun readTextUriOrNull(context: Context, uri: Uri?): String? {
        if (uri == null) return null
        return try {
            context.contentResolver.openInputStream(uri)?.use { it.readBytes().toString(Charsets.UTF_8) }
        } catch (t: Throwable) {
            null
        }
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

/** حصة واحدة من [PackagingUtils.languageBreakdown]: تسمية نوع المحتوى، لون تمييزه (نفس لون
 *  [PackagingUtils.iconColorResFor] لهذا النوع)، إجمالي بايتاته الحقيقية، ونسبته المئوية. */
data class FileLanguageSlice(val label: String, val colorRes: Int, val bytes: Long, val percent: Float)

/** ملف واحد داخل أرشيف الحزمة: مساره الكامل داخل الـ zip (مثال: "lib/mylib.og.rin") وحجمه. */
data class PackageFileEntry(val name: String, val sizeBytes: Long)

/** ملف واحد داخل شجرة الملفات ([PackagingUtils.buildFileTree]): اسمه المجرَّد وسجله الكامل. */
data class FileTreeLeaf(val simpleName: String, val entry: PackageFileEntry)

/**
 * مجلد واحد داخل شجرة الملفات ([PackagingUtils.buildFileTree]): مجلداته الفرعية المباشرة
 * (بالاسم) وملفاته المباشرة. الجذر (بلا اسم ولا مسار) يمثّل الحزمة نفسها.
 */
class FileTreeFolder(val name: String, val path: String) {
    val folders = sortedMapOf<String, FileTreeFolder>(String.CASE_INSENSITIVE_ORDER)
    val files = mutableListOf<FileTreeLeaf>()

    /** يرتّب ملفات هذا المجلد أبجدياً (المجلدات مرتَّبة أصلاً عبر sortedMapOf)، بعمق كامل. */
    fun sortRecursively() {
        files.sortWith(compareBy(String.CASE_INSENSITIVE_ORDER) { it.simpleName })
        folders.values.forEach { it.sortRecursively() }
    }

    /** إجمالي عدد الملفات داخل هذا المجلد وكل ما تحته من مجلدات فرعية (لعرضه في حبّة العدّاد). */
    fun totalFileCount(): Int = files.size + folders.values.sumOf { it.totalFileCount() }
}

/** محتوى حزمة مُستخرَج من أرشيفها، جاهز للعرض في صفحة تفاصيل شبيهة بصفحة مستودع GitHub. */
data class PackageContents(
    val readme: String?,
    val license: String?,
    val files: List<PackageFileEntry>
)
