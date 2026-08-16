package com.dlof.rinlang.store.languages

import android.util.Base64
import com.dlof.rinlang.store.extensions.ExtensionType
import com.dlof.rinlang.store.extensions.RinExtension
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * يحوّل "مشروع لغة مخصصة" محلياً (مجلد فيه manifest.json + Lexer.rin + ...) إلى [RinExtension]
 * بنوع [ExtensionType.LANGUAGE]، جاهزة لتمريرها إلى
 * [com.dlof.rinlang.store.extensions.ExtensionRepository.publishExtension] — بالضبط نفس مسار
 * نشر أي إضافة أخرى (PublishExtensionActivity)، فتظهر لغتك في "Rin Extensions Marketplace"
 * ويمكن لأي مستخدم آخر تثبيتها (عبر [com.dlof.rinlang.store.extensions.ExtensionManager.install])
 * فتصبح فوراً لغة يفهمها محرره ويمكنه تشغيل ملفاتها.
 *
 * "أصبحت اللغة رسمية" (badge) لا يمنحه المطوّر نفسه: [RinExtension.isOfficial] محمي في
 * firebase/database.rules.json بحيث لا يمكن للناشر تعيينه true من التطبيق، بل يتطلّب مراجعة/تفعيلاً
 * يدوياً من فريق Rin بعد فحص اللغة (استقرارها، عدم تضاربها مع لغات أخرى، جودة توثيقها) —
 * راجع docs/custom-languages.md لمعايير القبول وخطوات الطلب.
 */
object LanguageMarketplacePublisher {

    /** أسماء الملفات التي تُحزَم دوماً ضمن الأرشيف المنشور لأي مشروع لغة. */
    private val REQUIRED_ENTRIES = setOf(
        "manifest.json", "Lexer.rin", "Parser.rin", "Interpreter.rin",
        "CodeGen.rin", "run.rin", "syntax.rinsyntax.json", "README.md"
    )

    /**
     * يبني أرشيف zip (كـbase64، بنفس أسلوب [com.dlof.rinlang.store.RinPackage.base64Data]) من
     * محتوى مشروع اللغة كاملاً: الملفات الأساسية السبعة أعلاه + مجلد examples/ إن وُجد. يتجاهل
     * أي ملفات build/ أو مخرجات مؤقتة أخرى قد يكون المطوّر تركها داخل المشروع.
     */
    private fun buildBase64Archive(projectDir: File): String {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { zipOut ->
            fun addFile(file: File, entryName: String) {
                zipOut.putNextEntry(ZipEntry(entryName))
                file.inputStream().use { it.copyTo(zipOut) }
                zipOut.closeEntry()
            }

            fun addDirRecursive(dir: File, prefix: String) {
                val children = dir.listFiles() ?: return
                for (child in children.sortedBy { it.name }) {
                    if (child.name == "build" || child.name.startsWith(".")) continue
                    val entryName = if (prefix.isEmpty()) child.name else "$prefix/${child.name}"
                    if (child.isDirectory) addDirRecursive(child, entryName) else addFile(child, entryName)
                }
            }

            for (name in REQUIRED_ENTRIES) {
                val f = File(projectDir, name)
                if (f.exists()) addFile(f, name)
            }
            val examplesDir = File(projectDir, "examples")
            if (examplesDir.exists()) addDirRecursive(examplesDir, "examples")
        }
        return Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP)
    }

    /**
     * يبني [RinExtension] جاهزة للنشر من مشروع لغة مخصصة، أو null إن كان المجلد لا يحتوي
     * manifest.json صالحاً (أي ليس مشروع لغة مخصصة أصلاً). لا يقوم بأي اتصال شبكي بنفسه —
     * الشاشة المستدعية هي من تستدعي [com.dlof.rinlang.store.extensions.ExtensionRepository.publishExtension]
     * بالنتيجة، بعد الحصول على developerUid من المستخدم المسجَّل دخوله.
     */
    fun buildExtensionForPublish(
        projectDir: File,
        developerUid: String,
        developerDisplayName: String
    ): RinExtension? {
        val manifest = CustomLanguageManifest.read(projectDir) ?: return null
        val base64 = buildBase64Archive(projectDir)
        return RinExtension(
            name = manifest.name,
            version = manifest.version,
            developer = developerDisplayName.ifBlank { manifest.developer },
            developerUid = developerUid,
            description = manifest.description,
            type = ExtensionType.LANGUAGE.id,
            permissions = listOf("project.read"), // اللغة تُشغَّل داخل مشروعها الخاص فقط، بلا وصول إضافي
            languages = listOf(manifest.name, "Rin"),
            fileName = "${manifest.id}.rinex",
            base64Data = base64,
            isOfficial = false // لا يمكن تعيينها من هنا؛ راجع تعليق الكائن أعلاه
        )
    }
}
