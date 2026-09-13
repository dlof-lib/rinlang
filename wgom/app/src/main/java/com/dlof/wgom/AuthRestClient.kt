package com.dlof.wgom

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/** نتيجة تسجيل الدخول: idToken (لاستخدامه لاحقاً كـ ?auth= مع Realtime Database) و uid. */
data class AuthSession(val idToken: String, val uid: String, val refreshToken: String)

/**
 * عميل REST مباشر لواجهة Identity Toolkit الخاصة بـ Firebase Authentication (نفس الخدمة
 * التي يستخدمها Firebase Auth SDK داخلياً)، بلا أي اعتماد على Firebase SDK نفسه. يكفي
 * مفتاح الـWeb API العام الموجود في [FirebaseConfig.WEB_API_KEY].
 */
object AuthRestClient {

    private const val BASE = "https://identitytoolkit.googleapis.com/v1/accounts"
    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    fun signIn(email: String, password: String, onResult: (AuthSession?, String?) -> Unit) {
        val body = JSONObject().apply {
            put("email", email)
            put("password", password)
            put("returnSecureToken", true)
        }
        post("$BASE:signInWithPassword?key=${FirebaseConfig.WEB_API_KEY}", body) { json, error ->
            if (json == null) {
                onResult(null, error)
            } else {
                onResult(AuthSession(json.getString("idToken"), json.getString("localId"), json.getString("refreshToken")), null)
            }
        }
    }

    /** يحدّث كلمة السر لصاحب [idToken] الحالي (Firebase Auth لا يتطلب كلمة السر القديمة لهذا الاستدعاء). */
    fun changePassword(idToken: String, newPassword: String, onResult: (success: Boolean, error: String?) -> Unit) {
        val body = JSONObject().apply {
            put("idToken", idToken)
            put("password", newPassword)
            put("returnSecureToken", false)
        }
        post("$BASE:update?key=${FirebaseConfig.WEB_API_KEY}", body) { json, error ->
            onResult(json != null, error)
        }
    }

    private fun post(urlString: String, body: JSONObject, onResult: (JSONObject?, String?) -> Unit) {
        executor.execute {
            var connection: HttpURLConnection? = null
            try {
                val url = URL(urlString)
                connection = (url.openConnection() as HttpURLConnection).apply {
                    requestMethod = "POST"
                    doOutput = true
                    setRequestProperty("Content-Type", "application/json; charset=utf-8")
                    connectTimeout = 15_000
                    readTimeout = 15_000
                }
                connection.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
                val code = connection.responseCode
                if (code in 200..299) {
                    val text = connection.inputStream.bufferedReader().use { it.readText() }
                    mainHandler.post { onResult(JSONObject(text), null) }
                } else {
                    val errText = connection.errorStream?.bufferedReader()?.use { it.readText() }
                    mainHandler.post { onResult(null, mapError(errText)) }
                }
            } catch (e: Exception) {
                mainHandler.post { onResult(null, e.message ?: "فشل الاتصال بالشبكة") }
            } finally {
                connection?.disconnect()
            }
        }
    }

    private fun mapError(raw: String?): String {
        val message = try {
            JSONObject(raw ?: "").optJSONObject("error")?.optString("message") ?: raw
        } catch (e: Exception) {
            raw
        } ?: "حدث خطأ غير متوقع"
        return when {
            message.contains("EMAIL_NOT_FOUND") -> "لا يوجد حساب بهذا البريد"
            message.contains("INVALID_PASSWORD") || message.contains("INVALID_LOGIN_CREDENTIALS") -> "كلمة السر غير صحيحة"
            message.contains("USER_DISABLED") -> "تم تعطيل هذا الحساب"
            message.contains("WEAK_PASSWORD") -> "كلمة السر يجب أن تكون 6 أحرف على الأقل"
            message.contains("TOKEN_EXPIRED") -> "انتهت الجلسة، سجّل الدخول مرة أخرى"
            else -> message
        }
    }
}
