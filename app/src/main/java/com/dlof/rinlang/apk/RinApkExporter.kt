package com.dlof.rinlang.apk

import android.content.Context
import android.graphics.Bitmap
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
 *      معرّف حزمة فريد جديد + اسم تطبيق معروض هو اسم المشروع + minSdkVersion/targetSdkVersion
 *      المختارَين (كسمتين عدديتين حقيقيتين داخل &lt;uses-sdk&gt;، لا كنص).
 *   3. حقن ملفات المشروع (.rin وكل ما يرافقها) كأصول assets/rin_export_project/ (كل الملفات)
 *      + بيان JSON صغير يقرأه ExportedRunActivity (يشمل الآن أيضاً بيانات شاشة البداية).
 *   4. إعادة تجميع الحزمة (نسخ كل مُدخلات zip الأصلية بنفس أسلوب الضغط) مع محاذاة zipalign
 *      حقيقية لمكتبات .so (4096) وبقية مُدخلات STORED (4) — [ApkRepackager]. إن زوَّد
 *      المستخدم أيقونة مخصَّصة، تُستبدَل بايتات كل مُدخل أيقونة إطلاق (مسطّحة + adaptive في
 *      كل الكثافات) بنسخة مُعاد توليدها منها بالمقاس الصحيح — [IconInjector] — بلا لمس
 *      resources.arsc إطلاقاً (نفس أسماء المُدخلات، محتوى مختلف فقط).
 *   5. توليد/جلب هوية توقيع RSA-2048 حقيقية من AndroidKeyStore ([RinSigningIdentity])
 *      وتوقيع الحزمة فعلياً عبر مكتبة Google الرسمية apksig ([RealApkSigner] — v1+v2+v3
 *      معاً، نفس شيفرة أداة `apksigner`)، مع تراجع للتوقيع اليدوي v1 ([ApkV1Signer]) فقط إن
 *      تعذّر تحميل تلك المكتبة لأي سبب وقت التشغيل (احتياط أخير، مع سطر تحذير واضح).
 *   6. تسجيل التصدير في [RinAppsRegistry] (بيانات وصفية + نقل الحزمة لتخزين دائم بدل
 *      cacheDir) لتظهر لاحقاً في شاشة "تطبيقات Rin" ([com.dlof.rinlang.RinAppsActivity]).
 *
 * الناتج: ملف .apk حقيقي، قابل للتثبيت مباشرة عبر "تثبيت" أو "مشاركة"، بمعرّف حزمة مستقل
 * عن RinStudio نفسها (فلا يتعارض التثبيت معها ولا بين تصديرين مختلفين).
 */
object RinApkExporter {

    data class ExportResult(val apkFile: File, val applicationId: String, val signedWithV2: Boolean = false)

    sealed class Progress {
        data class Log(val text: String, val ok: Boolean = true) : Progress()
        data class Done(val result: ExportResult) : Progress()
        data class Failed(val message: String) : Progress()
    }

    /** إعدادات شاشة البداية الاختيارية للحزمة المُصدَّرة (انظر SplashActivity). */
    data class SplashConfig(
        val tagline: String? = null,
        val durationMs: Long = 1300L
    )

    fun export(
        context: Context,
        project: Project,
        appDisplayName: String,
        entryFile: String = "main.rin",
        minSdkVersion: Int = 24,
        targetSdkVersion: Int = 34,
        customIcon: Bitmap? = null,
        splash: SplashConfig = SplashConfig(),
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

                if (minSdkVersion > targetSdkVersion) {
                    throw IllegalArgumentException("الحد الأدنى لإصدار أندرويد ($minSdkVersion) أكبر من الهدف ($targetSdkVersion)")
                }

                log("… قراءة AndroidManifest.xml المُصرَّف من الحزمة المضيفة")
                val rawManifest = readZipEntry(hostApk, "AndroidManifest.xml")
                    ?: throw IllegalStateException("لم يُعثر على AndroidManifest.xml داخل الحزمة المضيفة")

                log("… تعديل بيان الحزمة (package + label + إصدارات أندرويد + كل سلطات provider) على مستوى البايت")
                // كل سلطة "authorities" داخل أي <provider> (FileProvider، وأي مزوِّد تُضيفه مكتبات
                // AndroidX/Firebase المدمَجة كـ WorkManager/Firebase Auth عبر دمج البيانات وقت بناء
                // RinStudio نفسها) كانت قد استُبدلت بقيمة حرفية مبنية على "com.dlof.rinlang..." وقت
                // ذلك البناء. تغيير معرّف الحزمة وحده لا يُغيّرها، وتركها كما هي يسبّب تعارض سلطة
                // موفِّر (provider authority) مع RinStudio نفسها إن كانت مثبَّتة على نفس الجهاز،
                // فيفشل تثبيت الحزمة المُصدَّرة تماماً — لذا نُفرِّدها كلها دفعة واحدة.
                //
                // minSdkVersion/targetSdkVersion سمتان عدديتان (TYPE_INT_DEC) لا نصيتان — بحث
                // "متساهل" في AxmlManifestPatcher (لا يفشل التصدير إن غاب <uses-sdk> لأي سبب،
                // فقط يُبقي قيم RinStudio الافتراضية).
                val patched = AxmlManifestPatcher.patch(
                    rawManifest,
                    listOf(
                        AxmlManifestPatcher.AttrPatch("manifest", "package", applicationId),
                        AxmlManifestPatcher.AttrPatch("application", "label", appDisplayName)
                    ),
                    authorityRewrite = { currentValue ->
                        val suffix = currentValue.substringAfterLast('.', currentValue)
                        "$applicationId.$suffix"
                    },
                    intPatches = listOf(
                        AxmlManifestPatcher.IntAttrPatch("uses-sdk", "minSdkVersion", minSdkVersion),
                        AxmlManifestPatcher.IntAttrPatch("uses-sdk", "targetSdkVersion", targetSdkVersion)
                    )
                )
                val sdkNote = if (patched.intAppliedCount > 0) "minSdk=$minSdkVersion targetSdk=$targetSdkVersion✓" else "uses-sdk غير موجود، أُبقيت قيم RinStudio الافتراضية"
                log("✓ تم تعديل البيان: package✓ label✓ providers✓ ($sdkNote) — ${patched.appliedCount} سمة إجمالاً")

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
                    put("min_sdk", minSdkVersion)
                    put("target_sdk", targetSdkVersion)
                    splash.tagline?.let { put("splash_tagline", it) }
                    put("splash_duration_ms", splash.durationMs)
                }.toString(2)
                extraEntries.add(ApkRepackager.ExtraEntry("assets/rin_export_manifest.json", manifestJson.toByteArray(Charsets.UTF_8)))
                log("✓ $fileCount ملف مُجهَّز (${totalBytes / 1024} كِلوبايت) + بيان تشغيل JSON")

                // 4) إعادة تجميع الحزمة + محاذاة zipalign (+ استبدال أيقونة الإطلاق إن زُوِّدت)
                log("… إعادة تجميع الحزمة (نسخ classes.dex والمكتبات الأصلية وresources.arsc كما هي)")
                if (customIcon != null) log("… استبدال أيقونة الإطلاق في كل الكثافات (مسطّحة + adaptive) من الصورة المختارة")
                val unsignedApk = File(work, "unsigned.apk")
                ApkRepackager.build(hostApk, patched.bytes, extraEntries, unsignedApk, customIcon)
                log("✓ تم البناء ومحاذاة مُدخلات zip (zipalign: 4096 بايت لمكتبات .so، 4 بايت للباقي)")

                // 5) هوية التوقيع الحقيقية + التوقيع الفعلي (مكتبة apksig الرسمية أولاً)
                log("… توليد/جلب هوية توقيع RSA-2048 من AndroidKeyStore")
                val identity = RinSigningIdentity.getOrCreate(context, commonName = "RinLang Export — ${project.name}")
                val fingerprint = RinSigningIdentity.sha256Hex(identity.certificate.encoded)
                log("✓ شهادة التوقيع (SHA-256): ${fingerprint.take(32)}…")

                log("… توقيع الحزمة عبر مكتبة Google الرسمية apksig (v1 + v2 + v3 معاً، نفس شيفرة apksigner)")
                val signedApk = File(work, "signed.apk")
                var signedWithRealSigner: Boolean
                try {
                    RealApkSigner.sign(unsignedApk, signedApk, identity.privateKey, identity.certificate, minSdkVersion)
                    signedWithRealSigner = true
                    log("✓ تم التوقيع فعلياً عبر apksig الرسمية (v1+v2+v3) — أقصى ثقة تثبيت ممكنة")
                } catch (e: Exception) {
                    signedWithRealSigner = false
                    log("⚠ تعذّر التوقيع عبر apksig الرسمية (${e.message ?: e.toString()}) — تراجع لتوقيع v1 اليدوي كحل أخير")
                    ApkV1Signer.sign(unsignedApk, signedApk, identity.privateKey, identity.certificate)
                    log("✓ تم توقيع v1 (احتياطي)")
                }

                val finalHash = MessageDigest.getInstance("SHA-256").digest(signedApk.readBytes())
                log("✓ بصمة الحزمة النهائية (SHA-256): ${finalHash.joinToString("") { "%02x".format(it) }.take(24)}…")

                // نقل الناتج إلى تخزين دائم (filesDir/rin_apps/ عبر RinAppsRegistry) بدل
                // cacheDir الذي قد يُطهَّر تلقائياً — يبقى متاحاً لاحقاً في شاشة "تطبيقات Rin".
                val exportDir = RinAppsRegistry.appsDir(context)
                val out = File(exportDir, "$applicationId.apk")
                if (out.exists()) out.delete()
                signedApk.copyTo(out, overwrite = true)

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
                        signedWithV2 = signedWithRealSigner
                    )
                )

                log("✓ الحزمة النهائية: ${out.name} (${out.length() / 1024} كِلوبايت)")
                onProgress(Progress.Done(ExportResult(out, applicationId, signedWithRealSigner)))
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
