package com.dlof.rinlang.store

import android.content.Context
import android.net.Uri
import android.util.Base64
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
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

        ZipOutputStream(BufferedOutputStream(FileOutputStream(zipFile))).use { zipOut ->
            // 1) ملف المكتبة نفسه، بنفس بنية lib/ المستخدمة داخل المشاريع
            zipOut.putNextEntry(ZipEntry("lib/${libraryFile.name}"))
            libraryFile.inputStream().use { it.copyTo(zipOut) }
            zipOut.closeEntry()

            // 2) README.md — يُستخدَم ملف الناشر إن رفع واحداً، وإلا يُولَّد تلقائياً من اسم
            // المكتبة والناشر والوصف
            val readme = readTextUriOrNull(context, customReadmeUri)
                ?: buildReadme(packageName, version, publisherName, license, description, libraryFile.name, dependencies)
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
                val displayName = queryDisplayName(context, uri) ?: "asset_$index"
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

    private fun buildReadme(
        name: String,
        version: String,
        publisherName: String,
        license: String,
        description: String,
        libraryFileName: String,
        dependencies: Map<String, String> = emptyMap()
    ): String = """
        |# $name
        |
        |**الإصدار:** $version
        |**الناشر:** $publisherName
        |**الترخيص:** $license
        |
        |## الوصف
        |${description.ifBlank { "لا يوجد وصف." }}
        |${if (dependencies.isEmpty()) "" else "\n## التبعيات\n" + dependencies.entries.joinToString("\n") { "- ${it.key} ${it.value}" } + "\n"}
        |## طريقة الاستخدام
        |```
        |@import "lib/$libraryFileName";
        |```
        |
        |---
        |تم نشر هذه الحزمة عبر متجر Rin (Rin Store).
    """.trimMargin()

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

/** ملف واحد داخل أرشيف الحزمة: مساره الكامل داخل الـ zip (مثال: "lib/mylib.og.rin") وحجمه. */
data class PackageFileEntry(val name: String, val sizeBytes: Long)

/** محتوى حزمة مُستخرَج من أرشيفها، جاهز للعرض في صفحة تفاصيل شبيهة بصفحة مستودع GitHub. */
data class PackageContents(
    val readme: String?,
    val license: String?,
    val files: List<PackageFileEntry>
)
