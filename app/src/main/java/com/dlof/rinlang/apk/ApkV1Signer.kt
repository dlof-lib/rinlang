package com.dlof.rinlang.apk

import java.io.File
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.Signature
import java.security.cert.X509Certificate
import java.util.Base64
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/**
 * توقيع APK حقيقي بمخطّط v1 (JAR signing — نفس المخطّط الذي يفهمه كل إصدار أندرويد منذ
 * البداية، ولا يزال يُقبل وحده بلا v2/v3 على كل الأجهزة الحالية طالما لا توجد كتلة v2
 * موجودة أصلاً تجبر التحقق الصارم منها): يبني MANIFEST.MF وCERT.SF ثم يوقّع CERT.SF
 * بمفتاح RSA-2048 الحقيقي المخزَّن في AndroidKeyStore (انظر [RinSigningIdentity])
 * عبر SHA256withRSA، ويغلّف التوقيع + الشهادة داخل بنية PKCS#7 SignedData مبنية يدوياً
 * (DER، انظر [Asn1]) — تماماً بنفس الشكل الذي ينتجه jarsigner/apksigner لمخطّط v1،
 * بلا أي مكتبة خارجية.
 *
 * ملاحظة صدق: هذا التطبيق يغطي مخطّط v1 فقط، وليس v2/v3 (APK Signing Block). هذا كافٍ
 * لتثبيت وتشغيل الحزمة على كل إصدارات أندرويد الحالية (النظام يتراجع تلقائياً لمخطّط v1
 * عند غياب كتلة v2)، لكنه لا يوفّر نفس الحماية الإضافية لمخطّط v2 (توقيع كامل الملف بدل كل
 * مُدخل zip على حدة). تحسين لاحق ممكن لو احتُجنا التوافق الصارم مع سياسات v2 الإلزامية.
 */
object ApkV1Signer {

    private const val SIGNER_NAME = "RINCERT" // اسم مُدخلات META-INF (بلا مسافات/رموز خاصة، كما يتوقعه محلّل JAR)

    fun sign(unsignedApk: File, outFile: File, privateKey: PrivateKey, certificate: X509Certificate) {
        val digestOfEntry = LinkedHashMap<String, ByteArray>() // name -> SHA-256(file bytes), بترتيب الظهور

        ZipFile(unsignedApk).use { zf ->
            val entries = zf.entries()
            while (entries.hasMoreElements()) {
                val e = entries.nextElement()
                if (e.isDirectory) continue
                if (e.name.startsWith("META-INF/")) continue
                val digest = MessageDigest.getInstance("SHA-256")
                zf.getInputStream(e).use { input ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        digest.update(buf, 0, n)
                    }
                }
                digestOfEntry[e.name] = digest.digest()
            }
        }

        // ---- MANIFEST.MF ----
        val manifestSections = LinkedHashMap<String, String>() // name -> نص القسم الخام (بما فيه سطر Name وسطر الـ digest ونهاية سطر فارغ)
        val mfBody = StringBuilder()
        mfBody.append("Manifest-Version: 1.0\r\n")
        mfBody.append("Created-By: RinApkExporter (RinStudio)\r\n\r\n")
        for ((name, digest) in digestOfEntry) {
            val section = buildString {
                append(wrap("Name: $name"))
                append(wrap("SHA-256-Digest: ${Base64.getEncoder().encodeToString(digest)}"))
                append("\r\n")
            }
            manifestSections[name] = section
            mfBody.append(section)
        }
        val manifestBytes = mfBody.toString().toByteArray(Charsets.UTF_8)
        val manifestDigest = MessageDigest.getInstance("SHA-256").digest(manifestBytes)

        // ---- CERT.SF ----
        val sfBody = StringBuilder()
        sfBody.append("Signature-Version: 1.0\r\n")
        sfBody.append(wrap("SHA-256-Digest-Manifest: ${Base64.getEncoder().encodeToString(manifestDigest)}"))
        sfBody.append("Created-By: RinApkExporter (RinStudio)\r\n\r\n")
        for ((name, section) in manifestSections) {
            val sectionDigest = MessageDigest.getInstance("SHA-256")
                .digest(section.toByteArray(Charsets.UTF_8))
            sfBody.append(wrap("Name: $name"))
            sfBody.append(wrap("SHA-256-Digest: ${Base64.getEncoder().encodeToString(sectionDigest)}"))
            sfBody.append("\r\n")
        }
        val sfBytes = sfBody.toString().toByteArray(Charsets.UTF_8)

        // ---- توقيع CERT.SF ----
        val signature = Signature.getInstance("SHA256withRSA").apply {
            initSign(privateKey)
            update(sfBytes)
        }.sign()

        val pkcs7 = buildPkcs7SignedData(signature, certificate)

        // ---- إلحاق المُدخلات الثلاثة داخل نسخة جديدة من الحزمة (بلا إعادة ضغط بقية المُدخلات) ----
        outFile.parentFile?.mkdirs()
        ZipFile(unsignedApk).use { zf ->
            ZipOutputStream(outFile.outputStream().buffered()).use { zos ->
                zos.setLevel(9)
                val entries = zf.entries()
                while (entries.hasMoreElements()) {
                    val e = entries.nextElement()
                    if (e.isDirectory) continue
                    val data = zf.getInputStream(e).use { it.readBytes() }
                    val newEntry = ZipEntry(e.name)
                    newEntry.method = e.method
                    if (e.method == ZipEntry.STORED) {
                        newEntry.size = data.size.toLong()
                        newEntry.compressedSize = data.size.toLong()
                        val crc = java.util.zip.CRC32(); crc.update(data)
                        newEntry.crc = crc.value
                    }
                    if (e.extra != null) newEntry.setExtra(e.extra) // نحافظ على محاذاة zipalign الأصلية
                    zos.putNextEntry(newEntry)
                    zos.write(data)
                    zos.closeEntry()
                }
                writeStored(zos, "META-INF/MANIFEST.MF", manifestBytes)
                writeStored(zos, "META-INF/$SIGNER_NAME.SF", sfBytes)
                writeStored(zos, "META-INF/$SIGNER_NAME.RSA", pkcs7)
            }
        }
    }

    private fun writeStored(zos: ZipOutputStream, name: String, data: ByteArray) {
        val entry = ZipEntry(name)
        entry.method = ZipEntry.DEFLATED
        zos.putNextEntry(entry)
        zos.write(data)
        zos.closeEntry()
    }

    /** يلفّ سطراً طوله > 70 بايت بأسلوب JAR القياسي (سطر متابعة يبدأ بمسافة واحدة). */
    private fun wrap(line: String): String {
        val bytes = line.toByteArray(Charsets.UTF_8)
        if (bytes.size <= 70) return "$line\r\n"
        val sb = StringBuilder()
        var i = 0
        var first = true
        while (i < bytes.size) {
            val chunkLen = if (first) 70 else 69
            val end = minOf(i + chunkLen, bytes.size)
            val prefix = if (first) "" else " "
            sb.append(prefix).append(String(bytes, i, end - i, Charsets.UTF_8)).append("\r\n")
            i = end
            first = false
        }
        return sb.toString()
    }

    /**
     * PKCS#7 SignedData (DER) بلا محتوى مضمَّن (detached) — نفس الشكل الذي يضعه jarsigner
     * داخل CERT.RSA: SEQUENCE { OID signedData, [0] SEQUENCE { version, digestAlgorithms,
     * contentInfo(data فارغ), certificates [0] IMPLICIT { شهادتنا }, signerInfos SET { ... } } }
     */
    private fun buildPkcs7SignedData(signature: ByteArray, cert: X509Certificate): ByteArray {
        val sha256 = "2.16.840.1.101.3.4.2.1"
        val rsaEncryption = "1.2.840.113549.1.1.1"
        val pkcs7Data = "1.2.840.113549.1.7.1"
        val pkcs7SignedData = "1.2.840.113549.1.7.2"

        val digestAlgorithms = Asn1.set(Asn1.algorithmIdentifier(sha256))
        val contentInfo = Asn1.sequence(Asn1.oid(pkcs7Data)) // بلا [0] EXPLICIT content -> detached

        val certDer = cert.encoded
        val certificates = Asn1.tlv(0xA0, certDer) // [0] IMPLICIT SET OF Certificate — شهادة واحدة فقط هنا

        val issuerAndSerial = Asn1.sequence(
            cert.issuerX500Principal.encoded,     // Name (DER جاهز من X500Principal)
            Asn1.integer(cert.serialNumber)
        )

        val signerInfo = Asn1.sequence(
            Asn1.integer(java.math.BigInteger.ONE),           // version
            issuerAndSerial,
            Asn1.algorithmIdentifier(sha256),                 // digestAlgorithm
            Asn1.algorithmIdentifier(rsaEncryption),          // digestEncryptionAlgorithm
            Asn1.octetString(signature)                       // encryptedDigest
        )
        val signerInfos = Asn1.set(signerInfo)

        val signedData = Asn1.sequence(
            Asn1.integer(java.math.BigInteger.ONE),           // version
            digestAlgorithms,
            contentInfo,
            certificates,
            signerInfos
        )

        return Asn1.sequence(
            Asn1.oid(pkcs7SignedData),
            Asn1.contextConstructed(0, signedData)
        )
    }
}
