package com.dlof.wgom

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.util.concurrent.Executors

data class RinProfile(val uid: String, val name: String, val username: String, val email: String)

/**
 * عميل REST مباشر لـ Realtime Database (نفس قاعدة بيانات RinStudio)، يستخدم idToken
 * الناتج من [AuthRestClient.signIn] كمعامل ?auth= حتى تطبَّق قواعد database.rules.json
 * تماماً كما لو أن الطلب جاء من RinStudio نفسه (auth.uid يصبح مطابقاً لمالك الحساب).
 */
object RtdbRestClient {

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    fun fetchProfile(uid: String, idToken: String, onResult: (RinProfile?, String?) -> Unit) {
        get("/users/${enc(uid)}.json", idToken) { json, error ->
            if (json == null) {
                onResult(null, error)
            } else {
                onResult(
                    RinProfile(
                        uid = json.optString("uid", uid),
                        name = json.optString("name", ""),
                        username = json.optString("username", ""),
                        email = json.optString("email", "")
                    ),
                    null
                )
            }
        }
    }

    /** يتحقق هل [username] مستخدَم من حساب آخر غير [uid] الحالي. */
    fun isUsernameTaken(username: String, uid: String, idToken: String, onResult: (taken: Boolean) -> Unit) {
        val path = "/users.json?orderBy=%22username%22&equalTo=${enc("\"$username\"")}"
        get(path, idToken) { json, _ ->
            if (json == null) {
                onResult(false)
            } else {
                val takenByOther = json.keys().asSequence().any { key -> key != uid }
                onResult(takenByOther)
            }
        }
    }

    /** يحدّث اسم المستخدم/الاسم الكامل لحساب [uid]، بشرط أن [idToken] يخص نفس الحساب (تفرضه قواعد Firebase). */
    fun updateProfile(uid: String, idToken: String, name: String, username: String, onResult: (Boolean, String?) -> Unit) {
        val body = JSONObject().apply {
            put("name", name)
            put("username", username)
        }
        patch("/users/${enc(uid)}.json", idToken, body) { json, error -> onResult(json != null, error) }
    }

    private fun get(path: String, idToken: String, onResult: (JSONObject?, String?) -> Unit) {
        executor.execute {
            var connection: HttpURLConnection? = null
            try {
                val separator = if (path.contains("?")) "&" else "?"
                val url = URL("${FirebaseConfig.DATABASE_URL}$path${separator}auth=$idToken")
                connection = (url.openConnection() as HttpURLConnection).apply {
                    requestMethod = "GET"
                    connectTimeout = 15_000
                    readTimeout = 15_000
                }
                val code = connection.responseCode
                val text = connection.inputStream.bufferedReader().use { it.readText() }
                if (code in 200..299) {
                    val trimmed = text.trim()
                    mainHandler.post {
                        onResult(if (trimmed == "null" || trimmed.isEmpty()) null else JSONObject(trimmed), null)
                    }
                } else {
                    mainHandler.post { onResult(null, "HTTP $code") }
                }
            } catch (e: Exception) {
                mainHandler.post { onResult(null, e.message ?: "فشل الاتصال بالشبكة") }
            } finally {
                connection?.disconnect()
            }
        }
    }

    private fun patch(path: String, idToken: String, body: JSONObject, onResult: (JSONObject?, String?) -> Unit) {
        executor.execute {
            var connection: HttpURLConnection? = null
            try {
                val url = URL("${FirebaseConfig.DATABASE_URL}$path?auth=$idToken")
                connection = (url.openConnection() as HttpURLConnection).apply {
                    requestMethod = "PATCH"
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
                    val err = connection.errorStream?.bufferedReader()?.use { it.readText() }
                    mainHandler.post { onResult(null, err ?: "HTTP $code") }
                }
            } catch (e: Exception) {
                mainHandler.post { onResult(null, e.message ?: "فشل الاتصال بالشبكة") }
            } finally {
                connection?.disconnect()
            }
        }
    }

    private fun enc(value: String) = URLEncoder.encode(value, "UTF-8")
}
