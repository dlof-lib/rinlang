package com.dlof.rinlang

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * الطرف الحقيقي (Kotlin) لجسر HTTP الخاص بأندرويد: يُستدعى مباشرة من الكود الأصلي (native) عبر
 * `jni_bridge.cpp` (انظر `JNI_OnLoad` و `callKotlinHttpBridge` هناك)، والذي بدوره يُسجَّل عبر
 * `rin::http::setAndroidBridge(...)` في `rin_http.cpp` ليكون التنفيذ الفعلي لكل natives
 * httpGet/httpPost/.../apiCall في لغة Rin (انظر `rin_interpreter.cpp`).
 *
 * لماذا JVM/HttpURLConnection وليس مكتبة C++ (curl/OpenSSL) هنا؟ NDK القياسي لا يشحن TLS/HTTP
 * client، لكن JVM أندرويد يملك عميل HTTPS كامل وحقيقي جاهزاً (نفس محرّك الشهادات/TLS الذي يستخدمه
 * أي تطبيق أندرويد عادي)، فبدل إعادة شحن مكتبة TLS كاملة داخل .so نستخدم هذا مباشرة.
 *
 * **مهم**: هذه الدالة تُنفِّذ اتصال شبكة *متزامناً* (blocking) — أندرويد يرفض ذلك على الترد الرئيسي
 * (NetworkOnMainThreadException). كل نقاط الدخول الحالية التي تُشغِّل كود Rin فعلياً
 * (`RinJobScheduler` عبر `workerPool`، `IndsinPreviewManager` عبر `worker`، `RinFlowTracer`/
 * `PipelineTracer`) تُشغِّله أصلاً على ترد خلفي، لذا لا حاجة لأي معالجة ترد إضافية هنا — لكن أي
 * استدعاء مستقبلي لـ [RinEngine.runSource] مباشرة من الترد الرئيسي سيفشل بنفس الاستثناء الذي
 * يفشل به أي اتصال شبكة عادي على أندرويد، وهذا سلوك متوقَّع ومقصود (وليس خطأ في هذا الجسر).
 *
 * التوقيع أدناه (خاصة الأنواع البدائية والمصفوفات) مطابق حرفياً لما يبنيه `jni_bridge.cpp`
 * (`GetStaticMethodID` بالتوقيع `(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;
 * [Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;`) — أي تغيير هنا يتطلّب تحديث ذلك
 * التوقيع في jni_bridge.cpp أيضاً وإلا سيفشل الاستدعاء عبر JNI بصمت (GetStaticMethodID يعيد null).
 */
object RinHttpBridge {

    private const val DEFAULT_TIMEOUT_MS = 15_000

    /**
     * ينفّذ طلب HTTP حقيقي فعلي ويُعيد النتيجة كمصفوفة من 4 نصوص بترتيب ثابت يقرأه الطرف C++:
     *  [0] "1" إن تم الاتصال فعلياً وحصلنا على رد (بغض النظر عن status)، وإلا "0"
     *  [1] رمز حالة HTTP كنص (أو "0" إن فشل الاتصال قبل وصول أي رد)
     *  [2] جسم الرد الخام (نجاحاً أو فشلاً - نص خطأ الخادوم مفيد أيضاً للمبرمج)
     *  [3] رسالة خطأ بشرية إن فشل الاتصال نفسه (فارغة عند النجاح)
     *
     * لماذا مصفوفة نصوص بسيطة بدل كائن Kotlin مُخصَّص؟ الوصول من JNI إلى حقول/getters كائن Kotlin
     * مُخصَّص يتطلّب GetFieldID/CallObjectMethod إضافية لكل حقل؛ مصفوفة نصوص بترتيب ثابت أبسط
     * وأكثر مقاومة لأخطاء التوقيع الصامتة من JNI.
     */
    @JvmStatic
    fun request(
        method: String,
        url: String,
        headerKeys: Array<String>,
        headerValues: Array<String>,
        body: String,
        timeoutMs: Int
    ): Array<String> {
        var connection: HttpURLConnection? = null
        return try {
            val effectiveTimeout = if (timeoutMs > 0) timeoutMs else DEFAULT_TIMEOUT_MS
            val conn = URL(url).openConnection() as HttpURLConnection
            connection = conn

            val httpMethod = method.ifBlank { "GET" }.uppercase()
            conn.requestMethod = httpMethod
            conn.connectTimeout = effectiveTimeout
            conn.readTimeout = effectiveTimeout
            conn.instanceFollowRedirects = true
            conn.doInput = true

            val n = minOf(headerKeys.size, headerValues.size)
            for (i in 0 until n) {
                conn.setRequestProperty(headerKeys[i], headerValues[i])
            }

            // جسم فعلي فقط إن كان غير فارغ، أو للطرق التي تحمل جسماً عادةً حتى لو فارغاً (لا نفرض
            // doOutput على GET/DELETE بلا جسم حتى لا نُفسد خوادم لا تتوقّع Content-Length: 0 منها).
            val methodsThatCarryBody = httpMethod == "POST" || httpMethod == "PUT" || httpMethod == "PATCH"
            if (body.isNotEmpty() || methodsThatCarryBody) {
                conn.doOutput = true
                val bytes = body.toByteArray(Charsets.UTF_8)
                conn.setFixedLengthStreamingMode(bytes.size)
                conn.outputStream.use { it.write(bytes) }
            }

            val status = conn.responseCode
            // status >= 400: جسم الرد الحقيقي (رسالة خطأ الخادوم) يكون في errorStream وليس inputStream.
            val stream = if (status in 200..399) conn.inputStream else (conn.errorStream ?: conn.inputStream)
            val responseBody = stream?.use { readAllUtf8(it) } ?: ""

            arrayOf("1", status.toString(), responseBody, "")
        } catch (t: Throwable) {
            arrayOf("0", "0", "", t.message ?: t.toString())
        } finally {
            connection?.disconnect()
        }
    }

    private fun readAllUtf8(input: java.io.InputStream): String {
        val out = ByteArrayOutputStream()
        val buf = ByteArray(8192)
        while (true) {
            val read = input.read(buf)
            if (read < 0) break
            out.write(buf, 0, read)
        }
        return out.toString("UTF-8")
    }

    private fun readAllBytes(input: java.io.InputStream): ByteArray {
        val out = ByteArrayOutputStream()
        val buf = ByteArray(8192)
        while (true) {
            val read = input.read(buf)
            if (read < 0) break
            out.write(buf, 0, read)
        }
        return out.toByteArray()
    }

    /**
     * تنزيل GET ثنائي حقيقي وآمن للبايتات (صور/أيقونات، fetchImage/fetchIcon في لغة Rin) — نظير
     * [request] أعلاه لكن الرد يُعاد كـ ByteArray خام بدل String، لأن [readAllUtf8]/UTF-8 يُفسدان
     * أي بايت ليس نصاً صالحاً (أي صورة PNG/JPEG حقيقية تقريباً). يُستدعى من jni_bridge.cpp عبر
     * rin::http::setAndroidBinaryGetBridge (وليس setAndroidBridge العادي) لنفس السبب.
     *
     * القيمة المُعادة: مصفوفة من 4 عناصر بترتيب ثابت يقرأه الطرف C++:
     *  [0] "1" إن تم الاتصال فعلياً وحصلنا على رد بحالة 2xx، وإلا "0"
     *  [1] رمز حالة HTTP كنص (أو "0" إن فشل الاتصال قبل وصول أي رد)
     *  [2] رسالة خطأ بشرية إن فشل الاتصال/الحالة ليست 2xx (فارغة عند النجاح)
     *  [3] بايتات الجسم الخام (ByteArray) عند النجاح، أو null
     */
    @JvmStatic
    fun requestBinaryGet(url: String, timeoutMs: Int): Array<Any?> {
        var connection: HttpURLConnection? = null
        return try {
            val effectiveTimeout = if (timeoutMs > 0) timeoutMs else DEFAULT_TIMEOUT_MS
            val conn = URL(url).openConnection() as HttpURLConnection
            connection = conn
            conn.requestMethod = "GET"
            conn.connectTimeout = effectiveTimeout
            conn.readTimeout = effectiveTimeout
            conn.instanceFollowRedirects = true
            conn.doInput = true

            val status = conn.responseCode
            if (status in 200..299) {
                val bytes = conn.inputStream.use { readAllBytes(it) }
                arrayOf<Any?>("1", status.toString(), "", bytes)
            } else {
                // جسم رسالة الخطأ (إن وُجد) نصّي غالباً، لكنه غير مطلوب هنا (fetchImage/fetchIcon
                // يهتمان بالبايتات الناجحة فقط) — رمز الحالة وحده كافٍ في رسالة الخطأ للمبرمج.
                arrayOf<Any?>("0", status.toString(), "الخادوم ردّ بحالة $status", null)
            }
        } catch (t: Throwable) {
            arrayOf<Any?>("0", "0", t.message ?: t.toString(), null)
        } finally {
            connection?.disconnect()
        }
    }

    /**
     * تنزيل متدفّق إلى ملف (فيديو/صور/أي ملف كبير): الجسم يُكتب مباشرة إلى [destPath] على دفعات 64KB فلا
     * يمرّ عبر ذاكرة JVM ولا JNI (نظير rin::http::performDownload على سطح المكتب — انظر rin_http.h).
     * يُستدعى من jni_bridge.cpp عبر rin::http::setAndroidDownloadBridge.
     *
     *  - التحويلات تُتبَّع يدوياً (حتى 10) وبـ http/https فقط؛ ترويسة Authorization لا تُرسَل لمضيف مختلف.
     *  - [resume]: إن كان الملف موجوداً بحجم > 0 نطلب Range؛ 206 = إكمال (append)، 200 = الخادوم تجاهل Range
     *    فنبدأ من الصفر، 416 = الجزء قديم/كامل فنبدأ من الصفر مرة واحدة.
     *  - [idleTimeoutMs] مهلة قراءة بلا بايتات (جمود)، [totalTimeoutMs] سقف كلي (<= 0 = 6 ساعات)،
     *    [maxBytes] حدّ حجم الملف (<= 0 = بلا حدّ) يُفرَض أثناء الكتابة.
     *
     * القيمة المُعادة (String[8] بترتيب ثابت يقرأه C++):
     *  [0] "1" إن اكتمل التنزيل بحالة 2xx  [1] رمز الحالة  [2] رسالة خطأ (فارغة عند النجاح؛ فارغة أيضاً عند 4xx/5xx
     *  فالطرف C++ يصيغ الرسالة)  [3] حجم الملف النهائي بالبايت  [4] "1" إن كان إكمالاً فعلياً  [5] Content-Type (بلا
     *  بارامترات، حروف صغيرة)  [6] Content-Disposition  [7] الرابط النهائي بعد التحويلات
     */
    @JvmStatic
    fun downloadToFile(
        url: String,
        destPath: String,
        headerKeys: Array<String>,
        headerValues: Array<String>,
        idleTimeoutMs: Int,
        totalTimeoutMs: Int,
        maxBytes: Long,
        resume: Boolean
    ): Array<String> {
        val dest = File(destPath)
        val idle = if (idleTimeoutMs > 0) idleTimeoutMs else 30_000
        val totalNs = (if (totalTimeoutMs > 0) totalTimeoutMs.toLong() else 21_600_000L) * 1_000_000L
        val t0 = System.nanoTime()
        var wantResume = resume && dest.exists() && dest.length() > 0
        var attempt = 0
        while (attempt < 2) {
            attempt++
            var conn: HttpURLConnection? = null
            try {
                var current = url
                var hops = 0
                val origHost = URL(url).host
                var respConn: HttpURLConnection? = null
                while (true) {
                    val u = URL(current)
                    if (u.protocol != "http" && u.protocol != "https")
                        return arrayOf("0", "0", "بروتوكول غير مدعوم — المسموح http:// أو https:// فقط", "0", "0", "", "", current)
                    val c = u.openConnection() as HttpURLConnection
                    conn = c
                    c.requestMethod = "GET"
                    c.connectTimeout = minOf(idle, 20_000)
                    c.readTimeout = idle
                    c.instanceFollowRedirects = false
                    c.doInput = true
                    val n = minOf(headerKeys.size, headerValues.size)
                    for (i in 0 until n) {
                        if (headerKeys[i].equals("Authorization", ignoreCase = true) && !u.host.equals(origHost, ignoreCase = true)) continue
                        c.setRequestProperty(headerKeys[i], headerValues[i])
                    }
                    if (wantResume) c.setRequestProperty("Range", "bytes=${dest.length()}-")
                    val st = c.responseCode
                    if (st in 300..399 && st != 304) {
                        val loc = c.getHeaderField("Location")
                        c.disconnect()
                        if (loc.isNullOrBlank() || ++hops > 10)
                            return arrayOf("0", st.toString(), "تحويلات كثيرة جداً أو بلا وجهة", "0", "0", "", "", current)
                        current = URL(u, loc).toString()
                        continue
                    }
                    respConn = c
                    break
                }
                val response = respConn ?: return arrayOf("0", "0", "فشل الاتصال", "0", "0", "", "", url)
                val status = response.responseCode
                val ctype = response.contentType?.substringBefore(';')?.trim()?.lowercase() ?: ""
                val disp = response.getHeaderField("Content-Disposition") ?: ""
                if (status == 416 && wantResume) { dest.delete(); wantResume = false; continue }
                if (status !in 200..299) {
                    return arrayOf("0", status.toString(), "", "0", "0", ctype, disp, current)
                }
                val append = wantResume && status == 206
                var written = if (append) dest.length() else 0L
                if (maxBytes > 0) {
                    val declared = response.contentLengthLong
                    if (declared > 0 && written + declared > maxBytes)
                        return arrayOf("0", status.toString(), "حجم الملف يتجاوز maxBytes", "0", "0", ctype, disp, current)
                }
                dest.parentFile?.mkdirs()
                response.inputStream.use { input ->
                    FileOutputStream(dest, append).use { out ->
                        val buf = ByteArray(64 * 1024)
                        while (true) {
                            val r = input.read(buf)
                            if (r < 0) break
                            out.write(buf, 0, r)
                            written += r
                            if (maxBytes > 0 && written > maxBytes) {
                                return arrayOf("0", status.toString(), "حجم الملف يتجاوز maxBytes", written.toString(), "0", ctype, disp, current)
                            }
                            if (System.nanoTime() - t0 > totalNs) {
                                return arrayOf("0", status.toString(), "انتهت المهلة الكلية للتنزيل", written.toString(), "0", ctype, disp, current)
                            }
                        }
                    }
                }
                return arrayOf("1", status.toString(), "", written.toString(), if (append) "1" else "0", ctype, disp, current)
            } catch (t: Throwable) {
                val size = if (dest.exists()) dest.length().toString() else "0"
                return arrayOf("0", "0", t.message ?: t.toString(), size, "0", "", "", url)
            } finally {
                conn?.disconnect()
            }
        }
        return arrayOf("0", "416", "تعذّر استئناف التنزيل", "0", "0", "", "", url)
    }
}
