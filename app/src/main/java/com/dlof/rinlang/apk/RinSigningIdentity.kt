package com.dlof.rinlang.apk

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.math.BigInteger
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.cert.X509Certificate
import java.util.Calendar
import javax.security.auth.x500.X500Principal

/**
 * هوية توقيع APK حقيقية، مبنية بالكامل على واجهات أندرويد الرسمية:
 *
 * - المفتاح الخاص (RSA-2048) يُولَّد ويُخزَّن داخل **AndroidKeyStore** نفسه (نفس المخزن الآمن
 *   الذي تستخدمه بصمة الإصبع/المفاتيح البنكية على الجهاز) — لا يُصدَّر أبداً كملف `.jks`/`.p12`،
 *   ولا يمرّ عبر الشبكة أو أي خادم بعيد.
 * - عند توليد المفتاح، تُنشئ منصّة أندرويد تلقائياً **شهادة X.509 ذاتية التوقيع** مرتبطة به عبر
 *   `KeyGenParameterSpec.setCertificateSubject/...` — هذه هي نفس الآلية التي تعتمد عليها أدوات مثل
 *   `keytool`/`apksigner` لإنشاء هوية توقيع تطبيق؛ ذاتية التوقيع هنا لا تعني "مزيّفة"، بل هي المعيار
 *   الفعلي لتوقيع تطبيقات أندرويد خارج متجر Play (تماماً كما تفعل F-Droid ومعظم أدوات البناء).
 * - هذه الهوية تبقى ثابتة على الجهاز عبر جميع عمليات التصدير القادمة (نفس alias)، حتى يمكن لاحقاً
 *   تحديث نفس تطبيق APK المُصدَّر بإصدار أحدث بلا تعارض توقيع، تماماً كأي تطبيق أندرويد حقيقي.
 */
object RinSigningIdentity {

    private const val KEYSTORE_PROVIDER = "AndroidKeyStore"
    private const val KEY_ALIAS = "rin_export_signing_key"
    private const val VALIDITY_YEARS = 30

    data class Identity(
        val privateKey: PrivateKey,
        val certificate: X509Certificate
    )

    /** يعيد الهوية الحالية إن وُجدت، أو يُولِّد واحدة جديدة (مرة واحدة فقط لكل جهاز/تثبيت). */
    @Synchronized
    fun getOrCreate(context: Context, commonName: String = "RinLang Export"): Identity {
        val keyStore = KeyStore.getInstance(KEYSTORE_PROVIDER).apply { load(null) }

        val existingKey = keyStore.getKey(KEY_ALIAS, null) as? PrivateKey
        val existingCert = keyStore.getCertificate(KEY_ALIAS) as? X509Certificate
        if (existingKey != null && existingCert != null) {
            return Identity(existingKey, existingCert)
        }

        val notBefore = Calendar.getInstance()
        val notAfter = Calendar.getInstance().apply { add(Calendar.YEAR, VALIDITY_YEARS) }

        val generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, KEYSTORE_PROVIDER)
        val spec = KeyGenParameterSpec.Builder(
            KEY_ALIAS,
            KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY
        )
            .setKeySize(2048)
            .setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_SHA1)
            .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1)
            .setCertificateSubject(X500Principal("CN=$commonName"))
            .setCertificateSerialNumber(BigInteger.valueOf(1))
            .setCertificateNotBefore(notBefore.time)
            .setCertificateNotAfter(notAfter.time)
            .build()

        generator.initialize(spec)
        generator.generateKeyPair()

        val privateKey = keyStore.getKey(KEY_ALIAS, null) as PrivateKey
        val certificate = keyStore.getCertificate(KEY_ALIAS) as X509Certificate
        return Identity(privateKey, certificate)
    }

    /** بصمة SHA-256 للشهادة الحالية بصيغة HEX مقروءة (للتحقق/العرض للمستخدم)، أو null إن لم تُولَّد بعد. */
    fun currentFingerprintOrNull(context: Context): String? {
        val keyStore = KeyStore.getInstance(KEYSTORE_PROVIDER).apply { load(null) }
        val cert = keyStore.getCertificate(KEY_ALIAS) as? X509Certificate ?: return null
        return sha256Hex(cert.encoded)
    }

    fun sha256Hex(bytes: ByteArray): String {
        val digest = java.security.MessageDigest.getInstance("SHA-256").digest(bytes)
        val sb = StringBuilder(digest.size * 2)
        for (b in digest) sb.append(String.format("%02X", b))
        return sb.toString()
    }
}
