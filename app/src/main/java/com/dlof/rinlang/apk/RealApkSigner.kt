package com.dlof.rinlang.apk

import com.android.apksig.ApkSigner
import java.io.File
import java.security.PrivateKey
import java.security.cert.X509Certificate

/**
 * غلاف رقيق فوق مكتبة Google الرسمية لتوقيع APK (`com.android.tools.build:apksig`، مُعلَنة
 * أصلاً في build.gradle — انظر تعليقها هناك) — **نفس الشيفرة** التي تستخدمها أداة
 * `apksigner` في Android SDK وAndroid Studio نفسها لتوقيع أي تطبيق أندرويد حقيقي حالياً.
 *
 * هذا يستبدل [ApkV1Signer] و[ApkV2Signer] اليدويَّين كمسار التوقيع الافتراضي: تنفيذهما
 * اليدوي لمواصفة ثنائية معقّدة (بلا أي جهاز حقيقي أو أداة `apksigner` للاختبار وقت كتابته)
 * ثبت عملياً أنه غير موثوق — فشل تثبيت حقيقي على جهاز رغم اجتياز تحقّق ذاتي كامل، لأن
 * التحقّق الذاتي لا يكشف خطأ فهمٍ منهجياً للمواصفة (يتفق التوقيع والتحقّق مع بعضهما رغم
 * خطئهما معاً). الحل الصحيح ليس تصحيح التخمين، بل استخدام تنفيذ حقيقي مُختبَر فعلياً —
 * وهو متاح هنا بالفعل كاعتماد Gradle.
 *
 * يوقّع بمخططات v1 + v2 + v3 معاً (نفس افتراضي أداة apksigner الرسمية)؛ v3 يضيف دعم تدوير
 * المفتاح المستقبلي بلا كلفة إضافية إن لم يُستخدَم.
 */
object RealApkSigner {

    class RealSigningException(message: String, cause: Throwable? = null) : Exception(message, cause)

    /**
     * يرمي [RealSigningException] (بدل ترك أي `Throwable` خام يتسرّب) عند أي فشل — يشمل هذا
     * عمداً أخطاء `Error` (مثل `NoSuchMethodError`/`NoClassDefFoundError`) لا الاستثناءات
     * فقط، لأن غياب توافق إصدار وقت التشغيل مع المكتبة يظهر غالباً كأخطاء من هذا النوع
     * لا كاستثناءات عادية؛ يلتقطها [RinApkExporter] ويتراجع للتوقيع اليدوي كحل أخير.
     */
    fun sign(unsignedApk: File, outFile: File, privateKey: PrivateKey, certificate: X509Certificate, minSdkVersion: Int) {
        try {
            val signerConfig = ApkSigner.SignerConfig.Builder(
                "rin-export",
                privateKey,
                listOf(certificate)
            ).build()

            outFile.parentFile?.mkdirs()
            val signer = ApkSigner.Builder(listOf(signerConfig))
                .setInputApk(unsignedApk)
                .setOutputApk(outFile)
                .setMinSdkVersion(minSdkVersion)
                .setV1SigningEnabled(true)
                .setV2SigningEnabled(true)
                .setV3SigningEnabled(true)
                .build()
            signer.sign()

            if (!outFile.exists() || outFile.length() <= 0L) {
                throw RealSigningException("لم تُنتج apksig ملفاً صالحاً")
            }
        } catch (e: RealSigningException) {
            throw e
        } catch (t: Throwable) {
            throw RealSigningException("فشل التوقيع عبر مكتبة apksig الرسمية: ${t.message ?: t.toString()}", t)
        }
    }
}
