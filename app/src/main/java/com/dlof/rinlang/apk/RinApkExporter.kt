package com.dlof.rinlang.apk

import android.content.Context
import com.dlof.rinlang.Project
import com.dlof.rinlang.RinAppRecord
import com.dlof.rinlang.RinAppsRegistry
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
 *   3. حقن ملفات المشروع (.rin وكل ما يرافقها) كأصول assets/rin_export_project/ (كل الملفات)
 *      + بيان JSON صغير يقرأه ExportedRunActivity.
 *   4. إعادة تجميع الحزمة (نسخ كل مُدخلات zip الأصلية بنفس أسلوب الضغط) مع محاذاة zipalign
 *      حقيقية لمكتبات .so (4096) وبقية مُدخلات STORED (4) — [ApkRepackager].
 *   5. توليد/جلب هوية توقيع RSA-2048 حقيقية من AndroidKeyStore ([RinSigningIdentity])
 *      وتوقيع الحزمة فعلياً بمخطّط v1 (JAR signing، PKCS#7) — [ApkV1Signer] — ثم إضافة
 *      كتلة توقيع v2 حقيقية فوقها ([ApkV2Signer]، تُتحقَّق ذاتياً قبل القبول، وتتراجع
 *      لـ v1 وحده إن تعذّر ذلك — مع سطر تحذير واضح في السجل) لأقصى ثقة تثبيت على أندرويد
 *      الحديث.
 *   6. تسجيل التصدير في [RinAppsRegistry] (بيانات وصفية + نقل الحزمة لتخزين دائم بدل
 *      cacheDir) لتظهر لاحقاً في شاشة "تطبيقات Rin" ([com.dlof.rinlang.RinAppsActivity]).
 *
 * الناتج: ملف .apk حقيقي، قابل للتثبيت مباشرة عبر "تثبيت" أو "مشاركة"، بمعرّف حزمة مستقل
 * عن RinStudio نفسها (فلا يتعارض التثبيت معها ولا بين تصديرين مختلفين).
 *
 * محدوديات معروفة (بلا إخفاء):
 *  - توقيع v1+v2 معاً (بلا v3) — v2 يُضاف فعلياً الآن (كان v1 فقط سابقاً)، وهو ما يقبله
 *    كل أندرويد 7.0+ بثقة تحقّق كاملة؛ v1 يبقى موجوداً أيضاً للتوافق مع أندرويد الأقدم.
 *  - الأيقونة تبقى أيقونة RinStudio نفسها (resources.arsc لا يُعدَّل) — الاسم المعروض فقط
 *    هو المتغيّر؛ هوية المشروع الكاملة تظهر داخل شاشة تشغيله بعد فتحه.
 */
object RinApkExporter {

    data class ExportResult(val apkFile: File, val applicationId: String, val signedWithV2: Boolean = false)

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
                val signedApkV1 = File(work, "signed_v1.apk")
                ApkV1Signer.sign(unsignedApk, signedApkV1, identity.privateKey, identity.certificate)
                log("✓ تم توقيع v1")

                log("… إضافة كتلة توقيع v2 حقيقية (APK Signature Scheme v2) فوق v1 لأقصى ثقة تثبيت")
                var finalSignedFile = signedApkV1
                var signedWithV2 = false
                try {
                    val signedApkV2 = File(work, "signed_v2.apk")
                    ApkV2Signer.signAndVerify(signedApkV1, signedApkV2, identity.privateKey, identity.certificate)
                    finalSignedFile = signedApkV2
                    signedWithV2 = true
                    log("✓ تم توقيع v2 والتحقّق الذاتي الكامل منه بنجاح (v1+v2 معاً)")
                } catch (e: Exception) {
                    log("⚠ تعذّر توقيع v2 (${e.message ?: e.toString()}) — استُخدم توقيع v1 وحده (يبقى قابلاً للتثبيت)")
                }

                val finalHash = MessageDigest.getInstance("SHA-256").digest(finalSignedFile.readBytes())
                log("✓ بصمة الحزمة النهائية (SHA-256): ${finalHash.joinToString("") { "%02x".format(it) }.take(24)}…")

                // نقل الناتج إلى تخزين دائم (filesDir/rin_apps/ عبر RinAppsRegistry) بدل
                // cacheDir الذي قد يُطهَّر تلقائياً — يبقى متاحاً لاحقاً في شاشة "تطبيقات Rin".
                val exportDir = RinAppsRegistry.appsDir(context)
                val out = File(exportDir, "$applicationId.apk")
                if (out.exists()) out.delete()
                finalSignedFile.copyTo(out, overwrite = true)

                RinAppsRegistry.add(
                    context,
                    RinAppRecord(
                        id = applicationId,
                        projectName = project.name,
                        displayName = appDisplayName,
                        applicationId = applicationId,
                        apkFileName = out.name,
                        sizeBytes = out.length(),
                        exportedAt = System.currentTimeMillis(),
                        entryFile = entryFile,
                        signedWithV2 = signedWithV2
                    )
                )

                log("✓ الحزمة النهائية: ${out.name} (${out.length() / 1024} كِلوبايت)")
                onProgress(Progress.Done(ExportResult(out, applicationId, signedWithV2)))
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

}
