package com.dlof.rinlang.apk

import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.Signature
import java.security.cert.X509Certificate

/**
 * توقيع APK حقيقي بمخطّط v2 (APK Signature Scheme v2 — "APK Signing Block"، ID الكتلة
 * 0x7109871a)، يُضاف فوق حزمة موقَّعة بالفعل بمخطّط v1 ([ApkV1Signer]) بلا تعديل أي مُدخل
 * zip موجود بداخلها — التطبيق [RinApkExporter.export] السابق وثّق أن v1 وحده هو "محدودية
 * معروفة"؛ هذا هو التنفيذ الحقيقي لسدّها: v2 هو ما يمنح أعلى ثقة تثبيت على أندرويد الحديث
 * (7.0+) لأن النظام يتحقّق من الحزمة الكاملة دفعة واحدة (ملخّصات مُقسَّمة إلى أجزاء 1 ميغابايت
 * قابلة للتحقق عبر mmap) بدل كل مُدخل zip منفصل كما في v1.
 *
 * بلا BouncyCastle أو أي مكتبة خارجية — RSASSA-PKCS1-v1_5/SHA-256 (معرّف الخوارزمية 0x0103)
 * تحديداً لأنه التوافق الوحيد الذي يقبله مفتاح [RinSigningIdentity] الحقيقي المخزَّن في
 * AndroidKeyStore (سياسته تحدّد SIGNATURE_PADDING_RSA_PKCS1 فقط، لا PSS)، وهو نفس خوارزمية
 * توقيع CERT.SF في v1 (SHA256withRSA) فلا حاجة لأي منطق توقيع إضافي.
 *
 * ملاحظة صدق حرجة: كتلة v2 فاسدة (بايت واحد خاطئ في أي إزاحة) **تجعل أندرويد يرفض التثبيت
 * بالكامل** بدل التراجع الصامت لـ v1 — وجود الكتلة وحده يُلزم النظام بالتحقق منها. لذا
 * [signAndVerify] لا يُعيد الحزمة أبداً قبل إعادة تحليلها من الصفر من نفس البايتات الناتجة
 * والتحقّق الفعلي من التوقيع بالمفتاح العام (تماماً كما تفعل حزمة أندرويد وقت التثبيت)؛ أي
 * فشل في ذلك يرمي [V2SigningException] بدل حزمة قد تكون فاسدة — والمستدعي (RinApkExporter)
 * يتراجع عندها لـ v1 وحده بدل تعليق الفشل على المستخدم.
 */
object ApkV2Signer {

    class V2SigningException(message: String, cause: Throwable? = null) : Exception(message, cause)

    private const val SIGNING_BLOCK_MAGIC = "APK Sig Block 42" // 16 بايت بالضبط، كما يحدّد الطيف الرسمي
    private const val V2_BLOCK_ID = 0x7109871a
    // RSASSA-PKCS1-v1_5 مع SHA2-256 (بصمة محتوى مُقسَّمة SHA2-256) — الوحيد المتوافق مع سياسة مفتاحنا
    private const val ALGORITHM_ID = 0x0103
    private const val CHUNK_SIZE = 1024 * 1024 // 1 ميغابايت، كما يحدّد الطيف الرسمي بالضبط

    private data class Eocd(val recordStart: Int, val cdOffset: Int)

    /**
     * يبني كتلة v2 حقيقية فوق [v1SignedApk]، يتحقّق منها ذاتياً بالكامل، ثم يكتبها إلى
     * [outFile]. يرمي [V2SigningException] بدل إخراج حزمة قد تفشل عند التثبيت.
     */
    fun signAndVerify(v1SignedApk: File, outFile: File, privateKey: PrivateKey, certificate: X509Certificate): ByteArray {
        val original = v1SignedApk.readBytes()
        val eocd = locateEocd(original)
        if (eocd.cdOffset < 0 || eocd.cdOffset > eocd.recordStart) {
            throw V2SigningException("EOCD غير متّسق: cdOffset=${eocd.cdOffset} eocdStart=${eocd.recordStart}")
        }

        // الأقسام الثلاثة المُوقَّعة وفق الطيف الرسمي. حقل "إزاحة الدليل المركزي" داخل EOCD
        // يساوي هنا بالفعل cdOffset — وهو تحديداً القيمة التي يجب اعتبار الحقل مُشيراً إليها
        // (بداية كتلة التوقيع بدل الدليل المركزي الفعلي) وفق الطيف، فلا حاجة لأي تعديل يدوي
        // على بايتات EOCD في هذه المرحلة (قبل إدراج الكتلة).
        val section1 = original.copyOfRange(0, eocd.cdOffset)          // مُدخلات zip
        val section2 = original.copyOfRange(eocd.cdOffset, eocd.recordStart) // الدليل المركزي كما هو
        val section3 = original.copyOfRange(eocd.recordStart, original.size) // EOCD

        val contentDigest = chunkedSha256(listOf(section1, section2, section3))

        val digestRecord = concat(le32(ALGORITHM_ID), lengthPrefixed(contentDigest))
        val digestsField = lengthPrefixedSequence(listOf(digestRecord))
        val certDer = certificate.encoded
        val certsField = lengthPrefixedSequence(listOf(certDer))
        val attrsField = lengthPrefixedSequence(emptyList())
        val signedData = concat(digestsField, certsField, attrsField)

        val signatureBytes = Signature.getInstance("SHA256withRSA").apply {
            initSign(privateKey)
            update(signedData)
        }.sign()
        val signatureRecord = concat(le32(ALGORITHM_ID), lengthPrefixed(signatureBytes))
        val signaturesField = lengthPrefixedSequence(listOf(signatureRecord))
        val publicKeyField = lengthPrefixed(certificate.publicKey.encoded)

        val signerBytes = concat(lengthPrefixed(signedData), signaturesField, publicKeyField)
        val signersField = lengthPrefixedSequence(listOf(signerBytes))

        val pairValue = signersField
        val pairTotalLen = 4L + pairValue.size
        val pairBytes = concat(le64(pairTotalLen), le32(V2_BLOCK_ID), pairValue)

        val sizeFieldValue = pairBytes.size.toLong() + 8L + 16L
        val magicBytes = SIGNING_BLOCK_MAGIC.toByteArray(Charsets.US_ASCII)
        val fullBlock = concat(le64(sizeFieldValue), pairBytes, le64(sizeFieldValue), magicBytes)

        // إعادة التجميع: [مُدخلات zip كما هي] + [كتلة v2] + [الدليل المركزي كما هو، بلا أي
        // تعديل — إزاحات رؤوس مُدخلاته المحلية لم تتغيّر] + [EOCD بحقل إزاحة جديد يُشير الآن
        // فعلياً لموقع الدليل المركزي الحقيقي بعد إدراج الكتلة].
        val newCdOffset = eocd.cdOffset + fullBlock.size
        val newEocd = section3.copyOf()
        writeLe32Into(newEocd, 16, newCdOffset.toLong())

        val result = concat(section1, fullBlock, section2, newEocd)

        verifySelf(result, certificate)

        outFile.parentFile?.mkdirs()
        outFile.writeBytes(result)
        return result
    }

    /**
     * تحقّق ذاتي كامل: يُعيد تحليل [finalApk] من الصفر (بلا استخدام أي متغيّر محسوب أعلاه)
     * — يُحدّد كتلة v2 عبر EOCD الجديد، يستخرج البصمة/التوقيع/الشهادة/المفتاح العام منها،
     * يتحقّق من التوقيع فعلياً عبر [Signature.verify]، ويُعيد حساب البصمة من نفس الملف
     * النهائي ليقارنها بالمخزَّنة داخل الكتلة. أي اختلاف يعني كتلة فاسدة فيُرمى استثناء.
     */
    private fun verifySelf(finalApk: ByteArray, certificate: X509Certificate) {
        val eocd = locateEocd(finalApk)
        val cdOffsetFinal = eocd.cdOffset
        val magicStart = cdOffsetFinal - 16
        if (magicStart < 8) throw V2SigningException("لا مساحة كافية لكتلة v2 قبل الدليل المركزي")
        val magic = String(finalApk, magicStart, 16, Charsets.US_ASCII)
        if (magic != SIGNING_BLOCK_MAGIC) throw V2SigningException("ماجيك كتلة v2 غير مطابق بعد إعادة التحليل")

        val sizeField2 = readLe64(finalApk, magicStart - 8)
        val blockStart = cdOffsetFinal - (sizeField2 + 8L).toInt()
        if (blockStart < 0) throw V2SigningException("موقع بداية كتلة v2 غير صالح")
        val sizeField1 = readLe64(finalApk, blockStart)
        if (sizeField1 != sizeField2) throw V2SigningException("حقلا حجم كتلة v2 غير متطابقين")

        var pos = blockStart + 8
        val pairsEnd = magicStart - 8
        var v2Value: ByteArray? = null
        while (pos < pairsEnd) {
            val pairLen = readLe64(finalApk, pos)
            val id = readLe32(finalApk, pos + 8).toInt()
            val valueLen = (pairLen - 4L).toInt()
            val valueStart = pos + 12
            if (id == V2_BLOCK_ID) v2Value = finalApk.copyOfRange(valueStart, valueStart + valueLen)
            pos += 8 + pairLen.toInt()
        }
        val value = v2Value ?: throw V2SigningException("لم يُعثر على قيمة كتلة v2 بعد إعادة التحليل")

        val signers = readLpSeqElements(value, 0)
        if (signers.size != 1) throw V2SigningException("عدد signers غير متوقَّع: ${signers.size}")
        val signer = signers[0]

        var sp = 0
        val signedDataLen = readLe32(signer, sp).toInt(); sp += 4
        val signedData = signer.copyOfRange(sp, sp + signedDataLen); sp += signedDataLen
        val signaturesSeqLen = readLe32(signer, sp).toInt()
        val signatureElements = readLpSeqElements(signer, sp); sp += 4 + signaturesSeqLen
        val pubKeyLen = readLe32(signer, sp).toInt(); sp += 4
        val publicKeyDer = signer.copyOfRange(sp, sp + pubKeyLen)

        if (signatureElements.size != 1) throw V2SigningException("عدد التوقيعات غير متوقَّع: ${signatureElements.size}")
        val sigRecord = signatureElements[0]
        val sigAlgId = readLe32(sigRecord, 0).toInt()
        val sigLen = readLe32(sigRecord, 4).toInt()
        val signatureBytes = sigRecord.copyOfRange(8, 8 + sigLen)
        if (sigAlgId != ALGORITHM_ID) throw V2SigningException("معرّف خوارزمية توقيع غير متوقَّع: $sigAlgId")

        val verifier = Signature.getInstance("SHA256withRSA")
        verifier.initVerify(certificate)
        verifier.update(signedData)
        if (!verifier.verify(signatureBytes)) throw V2SigningException("فشل التحقّق الفعلي من توقيع v2 بالمفتاح العام")

        if (!publicKeyDer.contentEquals(certificate.publicKey.encoded)) {
            throw V2SigningException("المفتاح العام داخل الكتلة لا يطابق شهادة التوقيع")
        }

        var dp = 0
        val digestsTotalLen = readLe32(signedData, dp).toInt()
        val digestRecords = readLpSeqElements(signedData, dp)
        dp += 4 + digestsTotalLen
        val certsTotalLen = readLe32(signedData, dp).toInt()
        val certRecords = readLpSeqElements(signedData, dp)
        dp += 4 + certsTotalLen

        if (digestRecords.size != 1) throw V2SigningException("عدد سجلّات البصمة غير متوقَّع: ${digestRecords.size}")
        val digestRecord = digestRecords[0]
        val digestAlgId = readLe32(digestRecord, 0).toInt()
        val digestLen = readLe32(digestRecord, 4).toInt()
        val storedDigest = digestRecord.copyOfRange(8, 8 + digestLen)
        if (digestAlgId != ALGORITHM_ID) throw V2SigningException("معرّف خوارزمية البصمة غير متوقَّع: $digestAlgId")

        if (certRecords.size != 1) throw V2SigningException("عدد الشهادات غير متوقَّع: ${certRecords.size}")
        if (!certRecords[0].contentEquals(certificate.encoded)) throw V2SigningException("شهادة كتلة v2 لا تطابق شهادة التوقيع")

        // إعادة حساب البصمة من نفس ملف APK النهائي (الأقسام الثلاثة الحقيقية بعد إدراج الكتلة):
        // حقل إزاحة الدليل المركزي داخل EOCD يُعتبَر هنا (لأغراض البصمة فقط) مُشيراً لبداية
        // كتلة التوقيع، تماماً كما يحدّد الطيف الرسمي وكما يفعل التحقّق الحقيقي على الجهاز.
        val section1 = finalApk.copyOfRange(0, blockStart)
        val section2 = finalApk.copyOfRange(cdOffsetFinal, eocd.recordStart)
        val digestEocd = finalApk.copyOfRange(eocd.recordStart, finalApk.size)
        writeLe32Into(digestEocd, 16, blockStart.toLong())
        val recomputed = chunkedSha256(listOf(section1, section2, digestEocd))
        if (!recomputed.contentEquals(storedDigest)) {
            throw V2SigningException("البصمة المُعاد حسابها من الملف النهائي لا تطابق البصمة المخزَّنة داخل الكتلة")
        }
    }

    // ============================== أدوات ZIP/EOCD ==============================

    /** يبحث عن سجل EOCD بمسح الملف من نهايته (أقصى تعليق zip مسموح به 65535 بايت)، ويتحقّق
     * من طول التعليق المُعلَن ليضمن أنه السجل الحقيقي لا تطابقاً عرضياً لتوقيعه داخل البيانات. */
    private fun locateEocd(data: ByteArray): Eocd {
        val minEocd = 22
        val maxScan = minOf(data.size, minEocd + 65535)
        val floor = data.size - maxScan
        var i = data.size - minEocd
        while (i >= floor) {
            if (data[i] == 0x50.toByte() && data[i + 1] == 0x4B.toByte() && data[i + 2] == 0x05.toByte() && data[i + 3] == 0x06.toByte()) {
                val cdOffset = readLe32(data, i + 16).toInt()
                val commentLen = (data[i + 20].toInt() and 0xFF) or ((data[i + 21].toInt() and 0xFF) shl 8)
                if (i + minEocd + commentLen == data.size) {
                    return Eocd(recordStart = i, cdOffset = cdOffset)
                }
            }
            i--
        }
        throw V2SigningException("تعذّر تحديد سجل EOCD داخل الحزمة")
    }

    // ============================== ترميز/تحليل TLV بالطيف الرسمي ==============================

    private fun le32(v: Int): ByteArray = le32(v.toLong() and 0xFFFFFFFFL)
    private fun le32(v: Long): ByteArray = byteArrayOf(
        (v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(), ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte()
    )
    private fun le64(v: Long): ByteArray = ByteArray(8) { idx -> ((v shr (idx * 8)) and 0xFF).toByte() }

    private fun readLe32(data: ByteArray, offset: Int): Long =
        (data[offset].toLong() and 0xFF) or
            ((data[offset + 1].toLong() and 0xFF) shl 8) or
            ((data[offset + 2].toLong() and 0xFF) shl 16) or
            ((data[offset + 3].toLong() and 0xFF) shl 24)

    private fun readLe64(data: ByteArray, offset: Int): Long {
        var v = 0L
        for (i in 0 until 8) v = v or ((data[offset + i].toLong() and 0xFF) shl (8 * i))
        return v
    }

    private fun writeLe32Into(data: ByteArray, offset: Int, value: Long) {
        data[offset] = (value and 0xFF).toByte()
        data[offset + 1] = ((value shr 8) and 0xFF).toByte()
        data[offset + 2] = ((value shr 16) and 0xFF).toByte()
        data[offset + 3] = ((value shr 24) and 0xFF).toByte()
    }

    private fun concat(vararg parts: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        for (p in parts) out.write(p)
        return out.toByteArray()
    }

    private fun lengthPrefixed(bytes: ByteArray): ByteArray = concat(le32(bytes.size), bytes)

    /** "تسلسل مُسبَق بطول، من عناصر مُسبَقة بطول" — النمط المتكرر في كل الطيف (البصمات،
     * الشهادات، السمات، التوقيعات، signers). */
    private fun lengthPrefixedSequence(elements: List<ByteArray>): ByteArray {
        val inner = ByteArrayOutputStream()
        for (el in elements) { inner.write(le32(el.size)); inner.write(el) }
        val innerBytes = inner.toByteArray()
        return concat(le32(innerBytes.size), innerBytes)
    }

    /** يُحلِّل تسلسلاً من هذا النمط بدءاً من [offset] (الذي يُشير لحقل الطول الكلي) إلى قائمة عناصره. */
    private fun readLpSeqElements(bytes: ByteArray, offset: Int): List<ByteArray> {
        val totalLen = readLe32(bytes, offset).toInt()
        var pos = offset + 4
        val end = pos + totalLen
        val out = ArrayList<ByteArray>()
        while (pos < end) {
            val elLen = readLe32(bytes, pos).toInt()
            pos += 4
            out.add(bytes.copyOfRange(pos, pos + elLen))
            pos += elLen
        }
        return out
    }

    // ============================== بصمة المحتوى المُقسَّمة (chunked SHA-256) ==============================

    /** يُقسِّم كل قسم (لا يمتد أي جزء عبر حدود قسمين) إلى أجزاء 1 ميغابايت، يبصم كل جزء
     * ببادئة 0xa5 + طوله + محتواه، ثم يبصم تسلسل كل بصمات الأجزاء ببادئة 0x5a + عددها —
     * تماماً كما يحدّد طيف v2 الرسمي (يتيح تحققاً عبر mmap بلا تحميل الملف كاملاً في الذاكرة). */
    private fun chunkedSha256(sections: List<ByteArray>): ByteArray {
        val chunkDigests = ByteArrayOutputStream()
        var chunkCount = 0
        val md = MessageDigest.getInstance("SHA-256")
        for (section in sections) {
            var offset = 0
            while (offset < section.size) {
                val len = minOf(CHUNK_SIZE, section.size - offset)
                md.reset()
                md.update(0xa5.toByte())
                md.update(le32(len))
                md.update(section, offset, len)
                chunkDigests.write(md.digest())
                chunkCount++
                offset += len
            }
        }
        val top = MessageDigest.getInstance("SHA-256")
        top.update(0x5a.toByte())
        top.update(le32(chunkCount))
        top.update(chunkDigests.toByteArray())
        return top.digest()
    }
}
