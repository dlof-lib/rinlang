package com.dlof.rinlang.auth

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/**
 * إرسال بريد كود التحقق عبر REST API الخاص بـ EmailJS مباشرة من التطبيق، دون أي حاجة
 * لسيرفر خاص أو Cloud Functions. راجع [EmailJsConfig] لخطوات الإعداد قبل الاستخدام.
 */
object EmailJsSender {

    private const val ENDPOINT = "https://api.emailjs.com/api/v1.0/email/send"
    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * يرسل كود التحقق إلى [toEmail]. النتيجة تصل على الـ Main Thread عبر [onResult]:
     * true = تم الإرسال بنجاح، false = فشل الإرسال (راجع Logcat وسبب الخطأ ضمن الرسالة).
     */
    fun sendVerificationCode(
        toEmail: String,
        toName: String,
        code: String,
        onResult: (success: Boolean, errorMessage: String?) -> Unit
    ) {
        if (!EmailJsConfig.isConfigured) {
            onResult(false, "لم يتم إعداد EmailJS بعد — املأ القيم في EmailJsConfig.kt")
            return
        }

        executor.execute {
            var connection: HttpURLConnection? = null
            try {
                val body = JSONObject().apply {
                    put("service_id", EmailJsConfig.SERVICE_ID)
                    put("template_id", EmailJsConfig.TEMPLATE_ID)
                    put("user_id", EmailJsConfig.PUBLIC_KEY)
                    put(
                        "template_params",
                        JSONObject().apply {
                            put("to_email", toEmail)
                            put("to_name", toName)
                            put("code", code)
                        }
                    )
                }

                val url = URL(ENDPOINT)
                connection = (url.openConnection() as HttpURLConnection).apply {
                    requestMethod = "POST"
                    doOutput = true
                    setRequestProperty("Content-Type", "application/json; charset=utf-8")
                    connectTimeout = 15_000
                    readTimeout = 15_000
                }

                connection.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }

                val responseCode = connection.responseCode
                if (responseCode in 200..299) {
                    postResult(onResult, true, null)
                } else {
                    val errorText = connection.errorStream?.bufferedReader()?.use { it.readText() }
                    postResult(onResult, false, "EmailJS رفض الطلب ($responseCode): ${errorText ?: ""}")
                }
            } catch (e: Exception) {
                postResult(onResult, false, e.message ?: "فشل الاتصال بالشبكة")
            } finally {
                connection?.disconnect()
            }
        }
    }

    private fun postResult(
        onResult: (Boolean, String?) -> Unit,
        success: Boolean,
        error: String?
    ) {
        mainHandler.post { onResult(success, error) }
    }
}
