package com.dlof.rinlang.auth

import com.google.firebase.database.DataSnapshot
import com.google.firebase.database.DatabaseError
import com.google.firebase.database.FirebaseDatabase
import com.google.firebase.database.ValueEventListener
import java.security.SecureRandom

/**
 * ===== المرحلة 2 من نظام الدخول: "إقران الجهاز عبر GETY" =====
 *
 * بعد أن ينجح المستخدم بالمرحلة 1 (دخول/تسجيل + كود بريد من 5 أرقام)، لا يكتمل الدخول
 * فوراً؛ بل يُنشئ هذا الكائن "طلب إقران" (token عشوائي + وقت صلاحية) ويخزّنه في
 * Realtime Database تحت /pairing_codes/{token}. يُعرض الـtoken كـQR ونص، ثم يقرأه تطبيق
 * GETY المستقل (كاميرا أو إدخال يدوي) ويكتب confirmed=true على نفس المسار.
 * RinStudio يستمع (ValueEventListener) لنفس المسار، وبمجرد confirmed=true يكمل الدخول.
 *
 * ملاحظة أمنية: GETY تطبيق منفصل تماماً وليس مسجّلاً دخوله بحساب Firebase Auth لهذا
 * المستخدم، لذا لا يمكن حصر الكتابة بـ"auth.uid === uid" كما في باقي المسارات. بدلاً
 * من ذلك يُستخدم الـtoken نفسه كـ"سرّ الحمل" (bearer capability): طول عشوائي كافٍ
 * (80 بت تقريباً) يجعل تخمينه غير عملي خلال نافذة الصلاحية (5 ساعات كحد أقصى)،
 * وقواعد Realtime Database (راجع firebase/database.rules.json) تسمح فقط بتحديث حقل
 * confirmed/confirmedDevice دون تعديل uid/الأوقات، وترفض أي كتابة بعد انتهاء الصلاحية
 * أو بعد تأكيد سابق. هذا هو نفس أسلوب "تسجيل الدخول عبر QR" في تطبيقات مثل واتساب ويب.
 */
data class PairingRequest(val token: String, val createdAt: Long, val expiresAt: Long)

sealed class PairingState {
    /** ما زال بانتظار أن يمسحه/يدخله المستخدم عبر GETY. */
    object Waiting : PairingState()
    /** تم تأكيده من GETY بنجاح — يمكن إكمال تسجيل الدخول. */
    object Confirmed : PairingState()
    /** انتهت صلاحية الـ 5 ساعات (أو أقل) دون تأكيد. */
    object Expired : PairingState()
}

object PairingRepository {

    /** الحد الأقصى لصلاحية كود/QR الإقران، كما طُلب: "تدوم وتتغير كل 5 ساعات أو أقل". */
    const val VALID_MILLIS = 5 * 60 * 60 * 1000L

    // أحرف بلا تشابه بصري (بدون 0/O، 1/I/L، إلخ) لتسهيل الإدخال اليدوي في GETY.
    private const val ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"
    private const val TOKEN_LENGTH = 16
    private val random = SecureRandom()

    private val db: FirebaseDatabase
        get() = FirebaseDatabase.getInstance(FirebaseDbConfig.DATABASE_URL)

    private fun pairingRef() = db.getReference("pairing_codes")

    /** ينشئ طلب إقران جديداً لحساب [uid] ويخزّنه، ثم يُعيد التوكن الناتج عبر [callback]. */
    fun createPairingRequest(uid: String, callback: (request: PairingRequest?, error: String?) -> Unit) {
        val token = generateToken()
        val now = System.currentTimeMillis()
        val expiresAt = now + VALID_MILLIS
        val payload = mapOf(
            "uid" to uid,
            "createdAt" to now,
            "expiresAt" to expiresAt,
            "confirmed" to false,
            "confirmedDevice" to ""
        )
        pairingRef().child(token).setValue(payload)
            .addOnSuccessListener { callback(PairingRequest(token, now, expiresAt), null) }
            .addOnFailureListener { e -> callback(null, e.message) }
    }

    /** يبدأ الاستماع الفوري لحالة [token]؛ أعد [ValueEventListener] الناتج إلى [stopObserving] عند التوقف. */
    fun observe(token: String, onUpdate: (PairingState) -> Unit): ValueEventListener {
        val listener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                if (!snapshot.exists()) { onUpdate(PairingState.Expired); return }
                val confirmed = snapshot.child("confirmed").getValue(Boolean::class.java) ?: false
                val expiresAt = snapshot.child("expiresAt").getValue(Long::class.java) ?: 0L
                when {
                    confirmed -> onUpdate(PairingState.Confirmed)
                    System.currentTimeMillis() > expiresAt -> onUpdate(PairingState.Expired)
                    else -> onUpdate(PairingState.Waiting)
                }
            }
            override fun onCancelled(error: DatabaseError) = Unit
        }
        pairingRef().child(token).addValueEventListener(listener)
        return listener
    }

    fun stopObserving(token: String, listener: ValueEventListener) {
        pairingRef().child(token).removeEventListener(listener)
    }

    /** يحذف طلب الإقران (عند الإلغاء أو بعد نجاح الدخول، حتى لا يبقى قابلاً لإعادة الاستخدام). */
    fun cancel(token: String) {
        pairingRef().child(token).removeValue()
    }

    private fun generateToken(): String {
        val raw = CharArray(TOKEN_LENGTH) { ALPHABET[random.nextInt(ALPHABET.length)] }
        return raw.toList().chunked(4).joinToString("-") { it.joinToString("") }
    }
}
