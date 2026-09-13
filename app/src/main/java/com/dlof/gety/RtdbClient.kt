package com.dlof.gety

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/**
 * عميل REST بسيط لـ Realtime Database — بلا Firebase SDK، بنفس أسلوب EmailJsSender.kt
 * في RinStudio. يُستخدم فقط لقراءة/تحديث عقدة pairing_codes/{token}.
 */
object RtdbClient {

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    /** يجلب /pairing_codes/{token}.json — يعيد null إن لم يكن الطلب موجوداً. */
    fun getPairing(token: String, onResult: (JSONObject?, error: String?) -> Unit) {
        executor.execute {
            var connection: HttpURLConnection? = null
            try {
                val url = URL("${FirebaseConfig.DATABASE_URL}/pairing_codes/${encode(token)}.json")
                connection = (url.openConnection() as HttpURLConnection).apply {
                    requestMethod = "GET"
                    connectTimeout = 15_000
                    readTimeout = 15_000
                }
                val code = connection.responseCode
                val text = connection.inputStream.bufferedReader().use { it.readText() }
                if (code in 200..299) {
                    val trimmed = text.trim()
                    if (trimmed == "null" || trimmed.isEmpty()) {
                        post(onResult, null, null)
                    } else {
                        post(onResult, JSONObject(trimmed), null)
                    }
                } else {
                    post(onResult, null, "HTTP $code")
                }
            } catch (e: Exception) {
                post(onResult, null, e.message ?: "فشل الاتصال بالشبكة")
            } finally {
                connection?.disconnect()
            }
        }
    }

    /**
     * يحدّث فقط confirmed/confirmedAt/confirmedDevice على /pairing_codes/{token}.json عبر PATCH.
     * قواعد Realtime Database ترفض هذا التحديث تلقائياً إن كان الطلب منتهياً أو مؤكَّداً مسبقاً.
     */
    fun confirmPairing(token: String, deviceName: String, onResult: (success: Boolean, error: String?) -> Unit) {
        executor.execute {
            var connection: HttpURLConnection? = null
            try {
                val body = JSONObject().apply {
                    put("confirmed", true)
                    put("confirmedAt", System.currentTimeMillis())
                    put("confirmedDevice", deviceName)
                }
                val url = URL("${FirebaseConfig.DATABASE_URL}/pairing_codes/${encode(token)}.json")
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
                    post2(onResult, true, null)
                } else {
                    val err = connection.errorStream?.bufferedReader()?.use { it.readText() }
                    post2(onResult, false, "رفض الخادم التأكيد ($code): ${err ?: "قد يكون الرمز منتهياً أو مؤكَّداً بالفعل"}")
                }
            } catch (e: Exception) {
                post2(onResult, false, e.message ?: "فشل الاتصال بالشبكة")
            } finally {
                connection?.disconnect()
            }
        }
    }

    private fun encode(token: String) = java.net.URLEncoder.encode(token, "UTF-8")

    private fun post(onResult: (JSONObject?, String?) -> Unit, a: JSONObject?, b: String?) {
        mainHandler.post { onResult(a, b) }
    }

    private fun post2(onResult: (Boolean, String?) -> Unit, a: Boolean, b: String?) {
        mainHandler.post { onResult(a, b) }
    }
}
