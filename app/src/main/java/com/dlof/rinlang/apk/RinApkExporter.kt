package com.dlof.rinlang.apk

import android.content.Context
import com.dlof.rinlang.Project
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import kotlin.concurrent.thread

/**
 * مصدِّر APK حقيقي — بلا محاكاة، بلا ملف فارغ.
 *
 * الفكرة المحورية: RinStudio نفسها تحمل بالفعل (مُصرَّفة داخل ثنائيها الحالي المثبَّت على
 * الجهاز) كل ما يلزم لتشغيل أي مشروع Rin بشكل مستقل: [com.dlof.rinlang.ExportedRunActivity]
 * (نقطة تشغيل بلا واجهة IDE) ومحرّك Rin الأصلي (rinengine.so عبر JNI). فلا حاجة لأي مُصرِّف
 * Dex أو aapt2 على الجهاز — "تحويل كود Rin" الحقيقي يحدث كما هو دائماً عبر تفسير المحرّك
 * الأصلي وقت التشغيل، تماماً كما يعمل زر ▶ داخل IDE، لا فرق جوهرياً سوى أن الحزمة الناتجة
 * حزمة أندرويد مستقلة قابلة للتثبيت والتوزيع بمعرّف حزمة خاص بها.
 *
 * خط الأنابيب الفعلي (كل خطوة أدناه عملية حقيقية، لا نص عرض فقط):
 *   1. تحديد ملف APK للحزمة المضيفة نفسها على القرص (applicationInfo.sourceDir).
 *   2. استخراج AndroidManifest.xml الثنائي منها وتعديله بايتاً بايت عبر [AxmlManifestPatcher]:
 *      معرّف حزمة فريد جديد + اسم تطبيق معروض هو اسم المشروع.
 *   3. حقن ملفات المشروع (.rin وكل ما يرافقها) كأصول assets/rin_export_project/*
 *      + بيان JSON صغير يقرأه ExportedRunActivity.
 *   4. إعادة تجميع الحزمة (نسخ كل مُدخلات zip الأصلية بنفس أسلوب الضغط) مع محاذاة zipalign
 *      حقيقية لمكتبات .so (4096) وبقية مُدخلات STORED (4) — [ApkRepackager].
 *   5. توليد/جلب هوية توقيع RSA-2048 حقيقية من AndroidKeyStore ([RinSigningIdentity])
 *      وتوقيع الحزمة فعلياً بمخطّط v1 (JAR signing، PKCS#7) — [ApkV1Signer].
 *
 * الناتج: ملف .apk حقيقي، قابل للتثبيت مباشرة عبر "تثبيت" أو "مشاركة"، بمعرّف حزمة مستقل
 * عن RinStudio نفسها (فلا يتعارض التثبيت معها ولا بين تصديرين مختلفين).
 *
 * محدوديات معروفة (بلا إخفاء):
 *  - مخطّط توقيع v1 (JAR) فقط، بلا v2/v3 — يكفي للتثبيت والتشغيل على كل إصدارات أندرويد
 *    الحالية، لكن بلا حماية v2 الإضافية (تفصيل التوقيع في [ApkV1Signer]).
 *  - الأيقونة تبقى أيقونة RinStudio نفسها (resources.arsc لا يُعدَّل) — الاسم المعروض فقط
 *    هو المتغيّر؛ هوية المشروع الكاملة تظهر داخل شاشة تشغيله بعد فتحه.
 */
object RinApkExporter {

    data class ExportResult(val apkFile: File, val applicationId: String)

    sealed class Progress {
        data class Log(val text: String, val ok: Boolean = true) : Progress()
        data class Done(val result: ExportResult) : Progress()
        data class Failed(val message: String) : Progress()
    }

    fun export(
        context: Context,
        project: Project,
        appDisplayName: String,
        entryFile: String = "main.rin",
        onProgress: (Progress) -> Unit
    ) {
        thread(name = "rin-apk-export") {
            val work = File(context.cacheDir, "apk_export_work/${project.name}-${System.currentTimeMillis()}")
            try {
                fun log(text: String) = onProgress(Progress.Log(text, ok = true))

                log("▶ بدء تصدير APK للمشروع \"${project.name}\"")
                work.mkdirs()

                // 1) موقع حزمة RinStudio المضيفة نفسها على القرص
                val hostApkPath = context.applicationInfo.sourceDir
                    ?: throw IllegalStateException("تعذّر تحديد مسار الحزمة المضيفة (sourceDir)")
                val hostApk = File(hostApkPath)
                log("✓ الحزمة المضيفة: ${hostApk.name} (${hostApk.length() / 1024} كِلوبايت)")

                // 2) معرّف تطبيق فريد + استخراج وتعديل AndroidManifest.xml الثنائي
                val applicationId = buildApplicationId(context.packageName, project.name)
                log("✓ معرّف الحزمة الجديد: $applicationId")

                log("… قراءة AndroidManifest.xml المُصرَّف من الحزمة المضيفة")
                val rawManifest = readZipEntry(hostApk, "AndroidManifest.xml")
                    ?: throw IllegalStateException("لم يُعثر على AndroidManifest.xml داخل الحزمة المضيفة")

                log("… تعديل بيان الحزمة (package + label + كل سلطات provider) على مستوى البايت")
                // كل سلطة "authorities" داخل أي <provider> (FileProvider، وأي مزوِّد تُضيفه مكتبات
                // AndroidX/Firebase المدمَجة كـ WorkManager/Firebase Auth عبر دمج البيانات وقت بناء
                // RinStudio نفسها) كانت قد استُبدلت بقيمة حرفية مبنية على "com.dlof.rinlang..." وقت
                // ذلك البناء. تغيير معرّف الحزمة وحده لا يُغيّرها، وتركها كما هي يسبّب تعارض سلطة
                // موفِّر (provider authority) مع RinStudio نفسها إن كانت مثبَّتة على نفس الجهاز،
                // فيفشل تثبيت الحزمة المُصدَّرة تماماً — لذا نُفرِّدها كلها دفعة واحدة.
                val patched = AxmlManifestPatcher.patch(
                    rawManifest,
                    listOf(
                        AxmlManifestPatcher.AttrPatch("manifest", "package", applicationId),
                        AxmlManifestPatcher.AttrPatch("application", "label", appDisplayName)
                    ),
                    authorityRewrite = { currentValue ->
                        val suffix = currentValue.substringAfterLast('.', currentValue)
                        "$applicationId.$suffix"
                    }
                )
                log("✓ تم تعديل البيان: package✓ label✓ providers✓ (${patched.appliedCount} سمة)")

                // 3) تجهيز أصول المشروع للحقن
                log("… تجهيز ملفات المشروع للحقن كأصول (assets/rin_export_project)")
                val extraEntries = ArrayList<ApkRepackager.ExtraEntry>()
                var fileCount = 0
                var totalBytes = 0L
                collectProjectFiles(project.dir, project.dir).forEach { (relPath, file) ->
                    extraEntries.add(ApkRepackager.ExtraEntry("assets/rin_export_project/$relPath", file.readBytes()))
                    fileCount++
                    totalBytes += file.length()
                }
                if (fileCount == 0) throw IllegalStateException("مجلد المشروع فارغ — لا يوجد ما يُصدَّر")

                val manifestJson = JSONObject().apply {
                    put("display_name", appDisplayName)
                    put("entry", entryFile)
                    put("project_name", project.name)
                    put("application_id", applicationId)
                    put("exported_at", System.currentTimeMillis())
                }.toString(2)
                extraEntries.add(ApkRepackager.ExtraEntry("assets/rin_export_manifest.json", manifestJson.toByteArray(Charsets.UTF_8)))
                log("✓ $fileCount ملف مُجهَّز (${totalBytes / 1024} كِلوبايت) + بيان تشغيل JSON")

                // 4) إعادة تجميع الحزمة + محاذاة zipalign
                log("… إعادة تجميع الحزمة (نسخ classes.dex والمكتبات الأصلية وresources.arsc كما هي)")
                val unsignedApk = File(work, "unsigned.apk")
                ApkRepackager.build(hostApk, patched.bytes, extraEntries, unsignedApk)
                log("✓ تم البناء ومحاذاة مُدخلات zip (zipalign: 4096 بايت لمكتبات .so، 4 بايت للباقي)")

                // 5) هوية التوقيع الحقيقية + التوقيع الفعلي
                log("… توليد/جلب هوية توقيع RSA-2048 من AndroidKeyStore")
                val identity = RinSigningIdentity.getOrCreate(context, commonName = "RinLang Export — ${project.name}")
                val fingerprint = RinSigningIdentity.sha256Hex(identity.certificate.encoded)
                log("✓ شهادة التوقيع (SHA-256): ${fingerprint.take(32)}…")

                log("… توقيع الحزمة (APK Signature Scheme v1 / JAR signing، SHA256withRSA)")
                val signedApk = File(work, "signed.apk")
                ApkV1Signer.sign(unsignedApk, signedApk, identity.privateKey, identity.certificate)

                val finalHash = MessageDigest.getInstance("SHA-256").digest(signedApk.readBytes())
                log("✓ تم التوقيع — بصمة الحزمة النهائية: ${finalHash.joinToString("") { "%02x".format(it) }.take(24)}…")

                // نقل الناتج إلى مسار apk_export/ المُعلَن في file_paths.xml (مطلوب لـ FileProvider)
                val exportDir = File(context.cacheDir, "apk_export").apply { mkdirs() }
                val out = File(exportDir, "${sanitizeFileToken(project.name)}.apk")
                if (out.exists()) out.delete()
                signedApk.copyTo(out, overwrite = true)

                log("✓ الحزمة النهائية: ${out.name} (${out.length() / 1024} كِلوبايت)")
                onProgress(Progress.Done(ExportResult(out, applicationId)))
            } catch (t: Throwable) {
                onProgress(Progress.Failed(t.message ?: t.toString()))
            } finally {
                work.deleteRecursively()
            }
        }
    }

    /** يقرأ مُدخلاً واحداً فقط من ملف zip. */
    private fun readZipEntry(zipFile: File, entryName: String): ByteArray? {
        java.util.zip.ZipFile(zipFile).use { zf ->
            val entry = zf.getEntry(entryName) ?: return null
            return zf.getInputStream(entry).use { it.readBytes() }
        }
    }

    /** يسرد كل ملفات مشروع (بما فيها مجلد lib/ الخاص بمكتبات @import المثبَّتة) كأزواج (مسار نسبي، ملف). */
    private fun collectProjectFiles(root: File, base: File): List<Pair<String, File>> {
        if (!root.exists()) return emptyList()
        val out = ArrayList<Pair<String, File>>()
        root.listFiles()?.sortedBy { it.name }?.forEach { child ->
            if (child.isDirectory) {
                out.addAll(collectProjectFiles(child, base))
            } else {
                val rel = child.relativeTo(base).path.replace(File.separatorChar, '/')
                out.add(rel to child)
            }
        }
        return out
    }

    /**
     * معرّف تطبيق فريد صالح (حروف/أرقام/نقاط فقط، يبدأ بحرف، لا يتعارض مع RinStudio نفسها
     * ولا بين تصديرين مختلفين لنفس المشروع بفضل لاحقة تجزئة قصيرة من اسم المشروع + الوقت).
     */
    private fun buildApplicationId(hostPackage: String, projectName: String): String {
        var slug = projectName.lowercase()
            .map { c -> if (c.isLetterOrDigit() && c.code < 128) c else '_' }
            .joinToString("")
            .trim('_')
            .ifBlank { "project" }
            .take(24)
        if (slug.first().isDigit()) slug = "p$slug" // كل جزء من applicationId يجب أن يبدأ بحرف
        val seed = "$projectName-${System.nanoTime()}"
        val hash = MessageDigest.getInstance("SHA-256").digest(seed.toByteArray(Charsets.UTF_8))
        val suffix = hash.joinToString("") { "%02x".format(it) }.take(8)
        return "$hostPackage.export.${slug}_$suffix"
    }

    private fun sanitizeFileToken(name: String): String =
        name.map { c -> if (c.isLetterOrDigit()) c else '_' }.joinToString("").ifBlank { "export" }
}
