package com.dlof.rinlang.apk

import android.content.Context
import com.android.apksig.ApkSigner
import com.dlof.rinlang.Project
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.security.cert.X509Certificate
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * يبني حزمة APK قابلة للتثبيت من مشروع Rin عبر ثلاث خطوات حقيقية بالكامل (لا شيء منها محاكاة):
 *
 *  1) **إعادة تعبئة**: تُنسخ حزمة تطبيق RinLang المُثبَّت حالياً (القالب المُصرَّف مسبقاً، ويتضمن
 *     محرّك Rin الأصلي عبر JNI/NDK بكل معماريات المعالج) كنقطة بداية، ثم تُحقن ملفات مشروع
 *     المستخدم داخل `assets/rin_export_project/` + وصف [manifest] يقرأه `ExportedRunActivity`
 *     عند إقلاع الحزمة المُصدَّرة ليشغّل المشروع مباشرة تلقائياً.
 *  2) **حذف** أي توقيعات `META-INF/*` موروثة من القالب الأصلي (إلزامي: لا يمكن توقيع حزمة تحمل
 *     توقيعاً سابقاً بمفتاح مختلف).
 *  3) **توقيع** حقيقي بمخطط Android الرسمي (Signature Scheme v1 + v2 + v3) عبر مكتبة `apksig`
 *     الرسمية من Google — نفس الكود الذي تستخدمه أداة `apksigner` سطر الأوامر ذاتها — بمفتاح
 *     RSA-2048 محفوظ في AndroidKeyStore (انظر [RinSigningIdentity]).
 *
 * محدودية معروفة: اسم التطبيق/أيقونته في الشاشة الرئيسية للحزمة الناتجة يبقيان كما في قالب
 * RinLang (تغييرهما يتطلب إعادة ترجمة `resources.arsc`/`AndroidManifest.xml` الثنائيين عبر
 * `aapt2`، غير المتوفر كأداة تعمل داخل تطبيق أندرويد نفسه). هوية المشروع الفعلية (الاسم، نقطة
 * الدخول) تظهر بدلاً من ذلك داخل شاشة تشغيل المشروع نفسها بعد فتح التطبيق المُصدَّر.
 */
object RinApkExporter {

    private const val MANIFEST_ASSET_PATH = "assets/rin_export_manifest.json"
    private const val PROJECT_ASSET_PREFIX = "assets/rin_export_project/"

    data class ExportResult(
        val apkFile: File,
        val certSha256: String
    )

    /** سطر تقدّم واحد يُعرض في شاشة التصدير بأسلوب طرفية (نفس روح RinFlow). */
    sealed class Progress {
        data class Log(val text: String, val ok: Boolean = true) : Progress()
        data class Done(val result: ExportResult) : Progress()
        data class Failed(val message: String) : Progress()
    }

    /** [entryFile] اسم ملف .rin الجذر الذي يُشغَّل أولاً عند فتح الحزمة المُصدَّرة (افتراضياً main.rin). */
    fun export(
        context: Context,
        project: Project,
        appDisplayName: String,
        entryFile: String = "main.rin",
        onProgress: (Progress) -> Unit
    ) {
        val workDir = File(context.cacheDir, "apk_export").apply {
            deleteRecursively()
            mkdirs()
        }
        try {
            onProgress(Progress.Log("$ locate build template (installed host package)"))
            val templateApk = File(context.applicationInfo.sourceDir)
            if (!templateApk.exists()) {
                onProgress(Progress.Failed("تعذّر الوصول لحزمة القالب المُثبَّتة"))
                return
            }

            val safeName = sanitizeFileName(project.name)
            val repackaged = File(workDir, "$safeName-unsigned.apk")

            onProgress(Progress.Log("▸ repackage  ${templateApk.name} → ${repackaged.name}"))
            repackageWithProject(templateApk, repackaged, project, appDisplayName, entryFile)

            onProgress(Progress.Log("▸ prepare signing identity  AndroidKeyStore RSA-2048"))
            val identity = RinSigningIdentity.getOrCreate(context, commonName = appDisplayName)
            val certSha256 = RinSigningIdentity.sha256Hex(identity.certificate.encoded)
            onProgress(Progress.Log("  cert.sha256 = ${chunk(certSha256)}"))

            val signed = File(workDir, "$safeName.apk")
            onProgress(Progress.Log("▸ sign  APK Signature Scheme v1 + v2 + v3"))
            signApk(repackaged, signed, identity)

            onProgress(Progress.Log("✓ build complete  ${signed.length() / 1024} KB", ok = true))
            onProgress(Progress.Done(ExportResult(signed, certSha256)))
        } catch (t: Throwable) {
            onProgress(Progress.Failed(t.message ?: t.toString()))
        }
    }

    // ---- step 1: repackage ----

    private fun repackageWithProject(
        templateApk: File,
        outApk: File,
        project: Project,
        appDisplayName: String,
        entryFile: String
    ) {
        val manifestJson = JSONObject().apply {
            put("project_name", project.name)
            put("display_name", appDisplayName)
            put("entry", entryFile)
            put("exported_at", System.currentTimeMillis())
        }.toString(2)

        ZipOutputStream(FileOutputStream(outApk).buffered()).use { out ->
            // 1) انسخ كل مُدخَل من القالب باستثناء توقيعات META-INF القديمة و assets/rin_export_*
            //    الموروثة من تصدير سابق (لو أُعيد التصدير فوق نفس الحزمة أكثر من مرة).
            ZipInputStream(templateApk.inputStream().buffered()).use { input ->
                var entry: ZipEntry? = input.nextEntry
                while (entry != null) {
                    val name = entry.name
                    if (!isSignatureEntry(name) && !name.startsWith(PROJECT_ASSET_PREFIX) &&
                        name != MANIFEST_ASSET_PATH
                    ) {
                        out.putNextEntry(ZipEntry(name))
                        input.copyTo(out)
                        out.closeEntry()
                    }
                    input.closeEntry()
                    entry = input.nextEntry
                }
            }

            // 2) بيان المشروع
            out.putNextEntry(ZipEntry(MANIFEST_ASSET_PATH))
            out.write(manifestJson.toByteArray(Charsets.UTF_8))
            out.closeEntry()

            // 3) ملفات المشروع نفسها (بنية شجرية كاملة تحت assets/rin_export_project/)
            addDirectoryRecursively(out, project.dir, PROJECT_ASSET_PREFIX)
        }
    }

    private fun addDirectoryRecursively(out: ZipOutputStream, dir: File, prefix: String) {
        val children = dir.listFiles() ?: return
        for (child in children.sortedBy { it.name }) {
            val entryPath = prefix + child.name
            if (child.isDirectory) {
                addDirectoryRecursively(out, child, "$entryPath/")
            } else {
                out.putNextEntry(ZipEntry(entryPath))
                child.inputStream().use { it.copyTo(out) }
                out.closeEntry()
            }
        }
    }

    private fun isSignatureEntry(name: String): Boolean =
        name == "META-INF/MANIFEST.MF" ||
            (name.startsWith("META-INF/") && (name.endsWith(".SF") || name.endsWith(".RSA") || name.endsWith(".DSA") || name.endsWith(".EC")))

    // ---- step 2: sign ----

    private fun signApk(inputApk: File, outputApk: File, identity: RinSigningIdentity.Identity) {
        val signerConfig = ApkSigner.SignerConfig.Builder(
            "rin-export",
            identity.privateKey,
            listOf<X509Certificate>(identity.certificate)
        ).build()

        ApkSigner.Builder(listOf(signerConfig))
            .setInputApk(inputApk)
            .setOutputApk(outputApk)
            .setV1SigningEnabled(true)
            .setV2SigningEnabled(true)
            .setV3SigningEnabled(true)
            .setMinSdkVersion(24)
            .build()
            .sign()
    }

    private fun sanitizeFileName(name: String): String =
        name.trim().ifBlank { "rin_project" }.replace(Regex("[^A-Za-z0-9_\\-]"), "_")

    private fun chunk(hex: String): String =
        hex.chunked(2).joinToString(":")
}
