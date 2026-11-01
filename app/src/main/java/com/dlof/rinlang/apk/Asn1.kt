package com.dlof.rinlang.apk

import java.io.ByteArrayOutputStream
import java.math.BigInteger

/**
 * ترميز DER بسيط ومحدود العمل — يكفي فقط بناء هيكل PKCS#7 SignedData المطلوب لتوقيع
 * APK بمخطّط v1 (JAR signing، ملف CERT.RSA)، بلا الاعتماد على BouncyCastle أو أي مكتبة
 * خارجية (غير متاحة على أندرويد افتراضياً). لا يهدف لتغطية ASN.1 كاملاً.
 */
object Asn1 {
    const val TAG_INTEGER = 0x02
    const val TAG_OCTET_STRING = 0x04
    const val TAG_NULL = 0x05
    const val TAG_OID = 0x06
    const val TAG_SEQUENCE = 0x30
    const val TAG_SET = 0x31
    const val TAG_CONTEXT0 = 0xA0
    const val TAG_CONTEXT_IMPLICIT0 = 0x80

    fun length(len: Int): ByteArray {
        if (len < 0x80) return byteArrayOf(len.toByte())
        var l = len
        val bytes = ArrayList<Byte>()
        while (l > 0) {
            bytes.add(0, (l and 0xFF).toByte())
            l = l ushr 8
        }
        return byteArrayOf((0x80 or bytes.size).toByte()) + bytes.toByteArray()
    }

    fun tlv(tag: Int, content: ByteArray): ByteArray = byteArrayOf(tag.toByte()) + length(content.size) + content

    fun concat(vararg parts: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        for (p in parts) out.write(p)
        return out.toByteArray()
    }

    fun sequence(vararg parts: ByteArray) = tlv(TAG_SEQUENCE, concat(*parts))
    fun set(vararg parts: ByteArray) = tlv(TAG_SET, concat(*parts))
    fun contextConstructed(n: Int, vararg parts: ByteArray) = tlv(0xA0 or n, concat(*parts))
    fun octetString(bytes: ByteArray) = tlv(TAG_OCTET_STRING, bytes)
    fun nullValue() = tlv(TAG_NULL, ByteArray(0))

    fun integer(value: BigInteger): ByteArray {
        var bytes = value.toByteArray() // ثنائي المتمم بالفعل (two's complement) من BigInteger
        if (bytes.isEmpty()) bytes = byteArrayOf(0)
        return tlv(TAG_INTEGER, bytes)
    }

    fun oid(dotted: String): ByteArray {
        val parts = dotted.split(".").map { it.toInt() }
        val out = ByteArrayOutputStream()
        out.write(parts[0] * 40 + parts[1])
        for (i in 2 until parts.size) {
            var v = parts[i]
            if (v == 0) {
                out.write(0)
                continue
            }
            val chunk = ArrayList<Int>()
            while (v > 0) {
                chunk.add(0, v and 0x7F)
                v = v ushr 7
            }
            for (j in chunk.indices) {
                val b = chunk[j]
                out.write(if (j != chunk.size - 1) (b or 0x80) else b)
            }
        }
        return tlv(TAG_OID, out.toByteArray())
    }

    /** AlgorithmIdentifier ::= SEQUENCE { algorithm OID, parameters ANY DEFINED BY algorithm OPTIONAL } — مع NULL params (شائع). */
    fun algorithmIdentifier(oidDotted: String, withNullParams: Boolean = true): ByteArray =
        if (withNullParams) sequence(oid(oidDotted), nullValue()) else sequence(oid(oidDotted))
}
