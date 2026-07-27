package com.dlof.rinlang.auth

import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.database.DataSnapshot
import com.google.firebase.database.DatabaseError
import com.google.firebase.database.FirebaseDatabase
import com.google.firebase.database.ValueEventListener
import kotlin.random.Random

/** نتيجة عملية تسجيل الدخول: قد تنجح مباشرة، أو تنجح لكن الحساب غير موثّق بعد. */
sealed class LoginResult {
    data class Success(val user: RinUser) : LoginResult()
    data class NeedsVerification(val uid: String, val email: String, val name: String) : LoginResult()
    data class Failure(val message: String) : LoginResult()
}

sealed class RegisterResult {
    data class NeedsVerification(val uid: String, val email: String, val name: String) : RegisterResult()
    data class Failure(val message: String) : RegisterResult()
}

sealed class VerifyResult {
    object Success : VerifyResult()
    data class Failure(val message: String) : VerifyResult()
}

/**
 * كل عمليات "حساب Rin": إنشاء حساب، تحقق بكود من 5 أرقام (عبر EmailJS)، تسجيل دخول.
 * يستخدم فقط Firebase Auth (Email/Password) + Realtime Database — كلاهما ضمن خطة Spark
 * المجانية. لا صلاحيات دفع أو Cloud Functions مطلوبة.
 */
object AuthRepository {

    private const val CODE_VALID_MILLIS = 10 * 60 * 1000L // الكود صالح 10 دقائق

    private val auth: FirebaseAuth get() = FirebaseAuth.getInstance()

    private val db: FirebaseDatabase
        get() = FirebaseDatabase.getInstance(FirebaseDbConfig.DATABASE_URL)

    private fun usersRef() = db.getReference("users")
    private fun codesRef() = db.getReference("verification_codes")

    // ---------------------------------------------------------------------
    // إنشاء حساب
    // ---------------------------------------------------------------------

    fun checkUsernameAvailable(username: String, callback: (available: Boolean) -> Unit) {
        usersRef().orderByChild("username").equalTo(username)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) = callback(!snapshot.exists())
                override fun onCancelled(error: DatabaseError) = callback(true) // لا نمنع المستخدم بسبب خطأ شبكة
            })
    }

    fun register(
        name: String,
        username: String,
        email: String,
        password: String,
        callback: (RegisterResult) -> Unit
    ) {
        auth.createUserWithEmailAndPassword(email, password)
            .addOnSuccessListener { result ->
                val uid = result.user?.uid
                if (uid == null) {
                    callback(RegisterResult.Failure("تعذّر إنشاء الحساب، حاول مرة أخرى"))
                    return@addOnSuccessListener
                }

                val profile = mapOf(
                    "uid" to uid,
                    "name" to name,
                    "username" to username,
                    "email" to email,
                    "verified" to false,
                    "createdAt" to System.currentTimeMillis()
                )

                usersRef().child(uid).setValue(profile)
                    .addOnSuccessListener {
                        sendNewCode(uid, email, name) { _, _ -> }
                        callback(RegisterResult.NeedsVerification(uid, email, name))
                    }
                    .addOnFailureListener { e ->
                        callback(RegisterResult.Failure(e.message ?: "فشل حفظ بيانات الحساب"))
                    }
            }
            .addOnFailureListener { e ->
                callback(RegisterResult.Failure(mapFirebaseError(e.message)))
            }
    }

    // ---------------------------------------------------------------------
    // كود التحقق (5 أرقام)
    // ---------------------------------------------------------------------

    /** يولّد كوداً جديداً، يخزّنه في Realtime Database، ويرسله عبر EmailJS. */
    fun sendNewCode(
        uid: String,
        email: String,
        name: String,
        onEmailResult: (success: Boolean, error: String?) -> Unit
    ) {
        val code = Random.nextInt(10000, 99999).toString()
        val payload = mapOf(
            "code" to code,
            "expiresAt" to (System.currentTimeMillis() + CODE_VALID_MILLIS)
        )
        codesRef().child(uid).setValue(payload)
            .addOnSuccessListener {
                EmailJsSender.sendVerificationCode(email, name, code, onEmailResult)
            }
            .addOnFailureListener { e -> onEmailResult(false, e.message) }
    }

    fun verifyCode(uid: String, enteredCode: String, callback: (VerifyResult) -> Unit) {
        codesRef().child(uid).addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val savedCode = snapshot.child("code").getValue(String::class.java)
                val expiresAt = snapshot.child("expiresAt").getValue(Long::class.java) ?: 0L

                when {
                    savedCode == null -> callback(VerifyResult.Failure("لا يوجد كود مُرسَل، اطلب كوداً جديداً"))
                    System.currentTimeMillis() > expiresAt ->
                        callback(VerifyResult.Failure("انتهت صلاحية الكود، اطلب كوداً جديداً"))
                    savedCode != enteredCode -> callback(VerifyResult.Failure("الكود غير صحيح"))
                    else -> {
                        usersRef().child(uid).child("verified").setValue(true)
                        codesRef().child(uid).removeValue()
                        callback(VerifyResult.Success)
                    }
                }
            }

            override fun onCancelled(error: DatabaseError) {
                callback(VerifyResult.Failure(error.message))
            }
        })
    }

    // ---------------------------------------------------------------------
    // تسجيل الدخول
    // ---------------------------------------------------------------------

    fun login(email: String, password: String, callback: (LoginResult) -> Unit) {
        auth.signInWithEmailAndPassword(email, password)
            .addOnSuccessListener { result ->
                val uid = result.user?.uid
                if (uid == null) {
                    callback(LoginResult.Failure("تعذّر تسجيل الدخول"))
                    return@addOnSuccessListener
                }
                usersRef().child(uid).addListenerForSingleValueEvent(object : ValueEventListener {
                    override fun onDataChange(snapshot: DataSnapshot) {
                        val user = snapshot.getValue(RinUser::class.java)
                        when {
                            user == null -> callback(LoginResult.Failure("لم يتم العثور على بيانات الحساب"))
                            user.verified -> callback(LoginResult.Success(user))
                            else -> callback(LoginResult.NeedsVerification(uid, user.email, user.name))
                        }
                    }

                    override fun onCancelled(error: DatabaseError) {
                        callback(LoginResult.Failure(error.message))
                    }
                })
            }
            .addOnFailureListener { e -> callback(LoginResult.Failure(mapFirebaseError(e.message))) }
    }

    fun currentUid(): String? = auth.currentUser?.uid

    fun fetchProfile(uid: String, callback: (RinUser?) -> Unit) {
        usersRef().child(uid).addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) = callback(snapshot.getValue(RinUser::class.java))
            override fun onCancelled(error: DatabaseError) = callback(null)
        })
    }

    fun logout() = auth.signOut()

    /** يحدّث صورة الملف الشخصي (base64 جاهزة مسبقاً بعد التصغير والضغط) لحساب [uid]. */
    fun updateAvatar(uid: String, avatarBase64: String, callback: (Boolean) -> Unit) {
        usersRef().child(uid).child("avatarBase64").setValue(avatarBase64)
            .addOnSuccessListener { callback(true) }
            .addOnFailureListener { callback(false) }
    }

    /** يحدّث الاسم الظاهر والنبذة القصيرة لحساب [uid]. */
    fun updateProfile(uid: String, name: String, bio: String, callback: (Boolean) -> Unit) {
        val updates = mapOf<String, Any>("name" to name, "bio" to bio)
        usersRef().child(uid).updateChildren(updates)
            .addOnSuccessListener { callback(true) }
            .addOnFailureListener { callback(false) }
    }

    private fun mapFirebaseError(raw: String?): String {
        val text = raw ?: return "حدث خطأ غير متوقع"
        return when {
            text.contains("email address is already in use", true) -> "هذا البريد مستخدم بالفعل"
            text.contains("password is invalid", true) -> "كلمة السر غير صحيحة"
            text.contains("no user record", true) -> "لا يوجد حساب بهذا البريد"
            text.contains("badly formatted", true) -> "صيغة البريد الإلكتروني غير صحيحة"
            text.contains("at least 6 characters", true) -> "كلمة السر يجب أن تكون 6 أحرف على الأقل"
            else -> text
        }
    }
}
