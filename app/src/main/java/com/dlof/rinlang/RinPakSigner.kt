package com.dlof.rinlang

import android.content.Context
import java.io.File
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.Signature
import java.security.spec.PKCS8EncodedKeySpec

/**
 * مفتاح توقيع Ed25519 الخاص بهذا الجهاز لحزم RinPak. يُولَّد عند أول تصدير ويُحفظ داخل التخزين
 * الخاص بالتطبيق (filesDir/rinpak_signing/ed25519.pk8) — لا يغادر الجهاز أبداً؛ يُنشر المفتاح العام
 * فقط داخل كل حزمة موقّعة (META-INF/signature.json). معرّف المفتاح (keyId) هو ما يقارنه المستلم.
 * minSdk = 35 فخوارزمية Ed25519 متاحة في java.security.
 */
object RinPakSigner {
    private const val DIR = "rinpak_signing"
    private const val FILE = "ed25519.pk8"
    private val SPKI_PREFIX_LEN = 12

    /** يعيد موقِّع الجهاز (ينشئ المفتاح عند أول استخدام)، أو null إن تعذّر (فيُصدَّر بلا توقيع). */
    fun forDevice(context: Context): RinPak.Signer? = try {
        val file = File(File(context.filesDir, DIR).apply { mkdirs() }, FILE)
        val priv: PrivateKey
        val pub: ByteArray
        if (file.isFile) {
            priv = KeyFactory.getInstance("Ed25519").generatePrivate(PKCS8EncodedKeySpec(file.readBytes()))
            pub = file.resolveSibling("$FILE.pub").readBytes()
        } else {
            val kp = KeyPairGenerator.getInstance("Ed25519").generateKeyPair()
            priv = kp.private
            pub = kp.public.encoded.copyOfRange(SPKI_PREFIX_LEN, kp.public.encoded.size)
            file.writeBytes(priv.encoded)
            file.resolveSibling("$FILE.pub").writeBytes(pub)
            file.setReadable(false, false); file.setReadable(true, true)
        }
        require(pub.size == 32)
        object : RinPak.Signer {
            override val publicKey: ByteArray = pub
            override fun sign(message: ByteArray): ByteArray =
                Signature.getInstance("Ed25519").run { initSign(priv); update(message); sign() }
        }
    } catch (t: Throwable) {
        null
    }

    /** معرّف مفتاح هذا الجهاز (للعرض في الإعدادات/المعاينة)، أو null. */
    fun deviceKeyId(context: Context): String? = forDevice(context)?.let { RinPak.keyId(it.publicKey) }
}
