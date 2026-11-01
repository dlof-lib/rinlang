package com.dlof.rinlang.packs

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.ConnectException
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownHostException
import java.security.MessageDigest
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * ينزّل حزمة، يتحقق من sha256 ثم يثبّتها عبر [PackStore.install].
 *
 * - يدعم الاستئناف (Range) للملفات الجزئية بعد انقطاع الشبكة.
 * - طلب تنزيل نفس الحزمة مرتين يشترك في نفس العملية (مفيد عند إعادة إنشاء الـ Activity).
 * - كل الـ callbacks تصل على الخيط الرئيسي. تقدّم التنزيل حقيقي (بايتات مستلمة فعلاً).
 */
object PackDownloader {

    enum class Stage { DOWNLOADING, VERIFYING, INSTALLING }

    interface Listener {
        fun onProgress(stage: Stage, doneBytes: Long, totalBytes: Long)
        fun onSuccess(pack: ContentPack)
        fun onError(pack: ContentPack, error: PackException)
    }

    class Handle internal constructor(internal val cancelled: AtomicBoolean) {
        fun cancel() = cancelled.set(true)
    }

    private class Active(val pack: ContentPack) {
        val cancelled = AtomicBoolean(false)
        val listeners = CopyOnWriteArrayList<Listener>()
        @Volatile var lastStage = Stage.DOWNLOADING
        @Volatile var lastDone = 0L
        @Volatile var lastTotal = 0L
        val handle = Handle(cancelled)
    }

    private val main = Handler(Looper.getMainLooper())
    private val executor = Executors.newFixedThreadPool(2)
    private val active = HashMap<String, Active>() // يُقرأ/يُكتب على الخيط الرئيسي فقط

    fun isActive(id: String): Boolean = active.containsKey(id)

    /** يُستدعى على الخيط الرئيسي. */
    fun start(context: Context, pack: ContentPack, listener: Listener): Handle {
        val existing = active[pack.id]
        if (existing != null) {
            existing.listeners.add(listener)
            listener.onProgress(existing.lastStage, existing.lastDone, existing.lastTotal)
            return existing.handle
        }
        val task = Active(pack)
        task.listeners.add(listener)
        active[pack.id] = task
        val app = context.applicationContext
        executor.execute { run(app, task) }
        return task.handle
    }

    /** يلغي التنزيل الجاري للحزمة [id] (إن وُجد). الملف الجزئي يبقى لاستئنافه لاحقاً. */
    fun cancel(id: String) {
        active[id]?.cancelled?.set(true)
    }

    /** يزيل مستمعاً (عند إغلاق الشاشة) دون إلغاء التنزيل نفسه. */
    fun detach(id: String, listener: Listener) {
        active[id]?.listeners?.remove(listener)
    }

    private fun post(task: Active, stage: Stage, done: Long, total: Long) {
        task.lastStage = stage; task.lastDone = done; task.lastTotal = total
        main.post { task.listeners.forEach { it.onProgress(stage, done, total) } }
    }

    private fun run(context: Context, task: Active) {
        val pack = task.pack
        var failure: PackException? = null
        try {
            if (!pack.url.startsWith("https://")) throw PackException(PackException.Kind.NETWORK, "https required")
            val part = File(PackStore.downloadsDir(context), pack.id + "-" + pack.sha256.take(8) + ".part")
            fetch(pack, part, task)
            checkCancelled(task)

            post(task, Stage.VERIFYING, 0, part.length())
            if (pack.sizeBytes > 0 && part.length() != pack.sizeBytes) {
                part.delete()
                throw PackException(PackException.Kind.HASH, "size mismatch")
            }
            if (!sha256Of(part).equals(pack.sha256, ignoreCase = true)) {
                part.delete() // لا نستأنف فوق ملف تالف
                throw PackException(PackException.Kind.HASH, "sha256 mismatch")
            }
            checkCancelled(task)

            post(task, Stage.INSTALLING, 0, 0)
            PackStore.install(context, pack, part)
            part.delete()
        } catch (e: PackException) {
            failure = e
        } catch (e: UnknownHostException) {
            failure = PackException(PackException.Kind.OFFLINE, "offline", e)
        } catch (e: ConnectException) {
            failure = PackException(PackException.Kind.OFFLINE, "offline", e)
        } catch (e: SocketTimeoutException) {
            failure = PackException(PackException.Kind.OFFLINE, "timeout", e)
        } catch (e: IOException) {
            failure = PackException(PackException.Kind.NETWORK, e.message ?: "io error", e)
        } catch (e: Exception) {
            failure = PackException(PackException.Kind.STORAGE, e.message ?: "unexpected error", e)
        }

        main.post {
            active.remove(pack.id)
            val err = failure
            task.listeners.forEach { if (err == null) it.onSuccess(pack) else it.onError(pack, err) }
        }
    }

    private fun checkCancelled(task: Active) {
        if (task.cancelled.get()) throw PackException(PackException.Kind.CANCELLED, "cancelled")
    }

    /** ينزّل إلى [part] مع استئناف إن وُجد جزء سابق. الملف الجزئي يبقى عند الانقطاع/الإلغاء للاستئناف لاحقاً. */
    private fun fetch(pack: ContentPack, part: File, task: Active) {
        var offset = if (part.isFile) part.length() else 0L
        if (pack.sizeBytes > 0 && offset > pack.sizeBytes) { part.delete(); offset = 0 }

        val conn = URL(pack.url).openConnection() as HttpURLConnection
        try {
            conn.connectTimeout = 15_000
            conn.readTimeout = 30_000
            conn.instanceFollowRedirects = true
            conn.setRequestProperty("Accept-Encoding", "identity") // بايتات الملف كما هي، لتطابق الحجم والتجزئة
            if (offset > 0) conn.setRequestProperty("Range", "bytes=$offset-")

            val code = conn.responseCode
            when (code) {
                200 -> { offset = 0; if (part.exists()) part.delete() } // الخادم تجاهل Range: نبدأ من الصفر
                206 -> {}
                416 -> { part.delete(); throw PackException(PackException.Kind.NETWORK, "range rejected, retry") }
                else -> throw PackException(PackException.Kind.NETWORK, "HTTP $code")
            }
            // بعد redirect يجب أن يبقى الرابط https (HttpURLConnection لا يتبع https→http أصلاً، هذا تحقق إضافي).
            if (conn.url.protocol != "https") throw PackException(PackException.Kind.NETWORK, "insecure redirect")

            val remaining = conn.contentLengthLong
            val total = if (pack.sizeBytes > 0) pack.sizeBytes else if (remaining > 0) offset + remaining else 0L

            RandomAccessFile(part, "rw").use { raf ->
                raf.seek(offset)
                var done = offset
                val buf = ByteArray(32 * 1024)
                var lastPost = 0L
                conn.inputStream.use { input ->
                    while (true) {
                        checkCancelled(task)
                        val n = input.read(buf)
                        if (n < 0) break
                        raf.write(buf, 0, n)
                        done += n
                        if (pack.sizeBytes > 0 && done > pack.sizeBytes) {
                            raf.setLength(0)
                            part.delete()
                            throw PackException(PackException.Kind.HASH, "download larger than expected")
                        }
                        val now = System.nanoTime()
                        if (now - lastPost > 80_000_000L) { // ~12 تحديثاً/ثانية كحد أقصى
                            lastPost = now
                            post(task, Stage.DOWNLOADING, done, total)
                        }
                    }
                }
                post(task, Stage.DOWNLOADING, done, total)
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun sha256Of(file: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buf = ByteArray(64 * 1024)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }
}
