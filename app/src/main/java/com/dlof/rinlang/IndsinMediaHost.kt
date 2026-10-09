package com.dlof.rinlang

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.webkit.MimeTypeMap
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID
import java.util.concurrent.Executors

/**
 * مضيف الوسائط لـ indsin (docs/indsin_media.md): ينفّذ الطلب `"media"` الذي يُخرجه المحرّك عند
 * `onTap=pickMedia(...)` / `uploadMedia(...)`:
 *
 *  - pick: منتقي النظام (Storage Access Framework — لا يحتاج أي صلاحية)، ثم نسخ كل ملف إلى
 *    `<جذر المشروع>/media/` باسم فريد ومعقَّم، فيصبح المسار نسبياً وصالحاً مباشرة لـ `<Image src=...>`.
 *    الحجم يُفحص قبل النسخ وأثناءه (حدّ maxMb)، ثم تُسلَّم القائمة للمحرّك الذي يعيد التحقق منها.
 *  - upload: طلب multipart/form-data واحد بالبث (لا تحميل للملف في الذاكرة) مع تقدّم حقيقي،
 *    https/http فقط، بلا متابعة إعادة توجيه (لا تسريب لملفات إلى مضيف آخر)، وردّ الخادم مقتطع.
 *
 * يجب إنشاؤه كحقل في الـ Activity (registerForActivityResult قبل STARTED).
 */
class IndsinMediaHost(private val activity: AppCompatActivity) {

    private class PendingPick(val cell: String, val kind: String, val multiple: Boolean, val maxMb: Double, val append: Boolean,
                              val source: String, val maxDim: Int, val quality: Int)

    private var pending: PendingPick? = null
    private val io = Executors.newSingleThreadExecutor { r -> Thread(r, "indsin-media").apply { isDaemon = true } }
    private val main = Handler(Looper.getMainLooper())
    private val uploadingCells = HashSet<String>()

    private val openSingle = activity.registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        onPicked(if (uri != null) listOf(uri) else emptyList())
    }
    private val openMulti = activity.registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        onPicked(uris ?: emptyList())
    }

    // الكاميرا: ACTION_IMAGE_CAPTURE/VIDEO_CAPTURE عبر عقود النظام — لا تحتاج صلاحية CAMERA ما دام التطبيق
    // نفسه لا يعلنها في المانيفست (لا تُعلنها!). الملف يُكتب في cacheDir/indsin_capture (مغطّى بـ file_paths.xml).
    private var captureFile: File? = null
    private val takePicture = activity.registerForActivityResult(ActivityResultContracts.TakePicture()) { ok -> onCaptured(ok) }
    private val captureVideo = activity.registerForActivityResult(ActivityResultContracts.CaptureVideo()) { ok -> onCaptured(ok) }

    /** نقطة الدخول: [media] = كائن "media" من غلاف المحرّك. */
    fun handle(media: JSONObject) {
        when (media.optString("op")) {
            "pick" -> startPick(
                media.optString("cell"), media.optString("kind", "any"),
                media.optBoolean("multiple", false), media.optDouble("maxMb", 0.0),
                media.optBoolean("append", false), media.optString("source", "library"),
                media.optInt("maxDim", 0), media.optInt("quality", 85).coerceIn(1, 100)
            )
            "upload" -> startUpload(
                media.optString("cell"), media.optString("url"),
                media.optString("field", "file").ifBlank { "file" }, media.optJSONArray("items") ?: JSONArray(),
                media.optString("auth", ""), media.optInt("retries", 2).coerceIn(0, 5)
            )
            "cancel" -> cancelUpload(media.optString("cell"))
        }
    }

    // ------------------------------------------------------------------ pick

    private fun mimeTypesFor(kind: String): Array<String> = when (kind) {
        "image" -> arrayOf("image/*")
        "video" -> arrayOf("video/*")
        "audio" -> arrayOf("audio/*")
        "file" -> arrayOf("*/*")
        else -> arrayOf("image/*", "video/*", "audio/*")
    }

    private fun startPick(cell: String, kind: String, multiple: Boolean, maxMb: Double, append: Boolean,
                          source: String, maxDim: Int, quality: Int) {
        if (cell.isBlank()) return
        pending = PendingPick(cell, kind, multiple, maxMb, append, source, maxDim, quality)
        if (source == "camera") { startCapture(); return }
        try {
            if (multiple) openMulti.launch(mimeTypesFor(kind)) else openSingle.launch(mimeTypesFor(kind))
        } catch (t: Throwable) {
            pending = null
            toast(t.message ?: "تعذر فتح منتقي الملفات")
        }
    }

    private fun startCapture() {
        val p = pending ?: return
        try {
            val dir = File(activity.cacheDir, "indsin_capture").apply { mkdirs() }
            val isVideo = p.kind == "video"
            val f = File(dir, "capture_${System.currentTimeMillis()}." + if (isVideo) "mp4" else "jpg")
            captureFile = f
            val uri = FileProvider.getUriForFile(activity, activity.packageName + ".fileprovider", f)
            if (isVideo) captureVideo.launch(uri) else takePicture.launch(uri)
        } catch (t: Throwable) {
            pending = null
            captureFile = null
            toast(t.message ?: "تعذر فتح الكاميرا")
        }
    }

    private fun onCaptured(ok: Boolean) {
        val p = pending
        val f = captureFile
        pending = null
        captureFile = null
        if (p == null || f == null) return
        if (!ok || !f.isFile || f.length() == 0L) { f.delete(); return } // أُلغي الالتقاط: لا تغيير في الحالة
        io.execute {
            try {
                val dir = File(RinEngine.currentBaseDir(), "media").apply { mkdirs() }
                val item = copyIntoProject(Uri.fromFile(f), dir, p.maxMb, p.maxDim, p.quality)
                IndsinPreviewManager.mediaPicked(p.cell, JSONArray().put(item).toString(), p.kind, false, p.maxMb, append = p.append)
            } catch (e: IOException) {
                IndsinPreviewManager.mediaProgress(p.cell, "error", 0.0, e.message ?: "تعذر معالجة الملف الملتقَط")
            } finally {
                f.delete()
            }
        }
    }

    private fun onPicked(uris: List<Uri>) {
        val p = pending ?: return
        pending = null
        if (uris.isEmpty()) return // المستخدم ألغى: لا تغيير في الحالة
        io.execute {
            val items = JSONArray()
            var failure: String? = null
            val dir = File(RinEngine.currentBaseDir(), "media").apply { mkdirs() }
            for (uri in if (p.multiple) uris else uris.take(1)) {
                try {
                    items.put(copyIntoProject(uri, dir, p.maxMb, p.maxDim, p.quality))
                } catch (e: IOException) {
                    failure = e.message ?: "تعذر نسخ الملف"
                    break
                }
            }
            if (failure != null) {
                // نُبلغ المحرّك بالخطأ عبر مسار التقدّم (يضع status=error دون المساس بالاختيار السابق).
                IndsinPreviewManager.mediaProgress(p.cell, "error", 0.0, failure)
            } else {
                IndsinPreviewManager.mediaPicked(p.cell, items.toString(), p.kind, p.multiple, p.maxMb, append = p.append)
            }
        }
    }

    private fun queryNameAndSize(uri: Uri): Pair<String, Long> {
        var name = ""
        var size = -1L
        try {
            activity.contentResolver.query(uri, null, null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    val ni = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    val si = c.getColumnIndex(OpenableColumns.SIZE)
                    if (ni >= 0) name = c.getString(ni) ?: ""
                    if (si >= 0 && !c.isNull(si)) size = c.getLong(si)
                }
            }
        } catch (_: Throwable) {}
        if (name.isBlank()) name = uri.lastPathSegment ?: "file"
        return name to size
    }

    /** يعقّم الاسم: يزيل المسارات والمحارف الخطرة ويحدّ الطول (يحفظ الامتداد). */
    private fun safeName(raw: String): String {
        val base = raw.substringAfterLast('/').substringAfterLast('\\')
        val cleaned = base.replace(Regex("[^\\p{L}\\p{N}._-]"), "_").trim('.', '_').ifBlank { "file" }
        if (cleaned.length <= 80) return cleaned
        val ext = cleaned.substringAfterLast('.', "")
        return if (ext.isNotEmpty() && ext.length <= 8) cleaned.take(70) + "." + ext else cleaned.take(80)
    }

    private fun copyIntoProject(uri: Uri, dir: File, maxMb: Double, maxDim: Int = 0, quality: Int = 85): JSONObject {
        val (originalName, declaredSize) = queryNameAndSize(uri)
        val finalLimit = if (maxMb > 0) (maxMb * 1024 * 1024).toLong() else Long.MAX_VALUE
        val mayShrink = maxDim > 0 // الصور قد تصغر بعد المعالجة: نؤجّل فحص الحد إلى ما بعدها
        val limit = if (mayShrink) Long.MAX_VALUE else finalLimit
        if (declaredSize > limit) throw IOException("'$originalName' أكبر من الحد المسموح (${maxMb.toInt()} MB)")

        var mime = activity.contentResolver.getType(uri).orEmpty()
        if (mime.isBlank() || mime == "application/octet-stream") {
            val ext = originalName.substringAfterLast('.', "").lowercase()
            mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
        }
        val kind = when {
            mime.startsWith("image/") -> "image"
            mime.startsWith("video/") -> "video"
            mime.startsWith("audio/") -> "audio"
            else -> "file"
        }

        val target = File(dir, "${System.currentTimeMillis()}_${UUID.randomUUID().toString().take(6)}_${safeName(originalName)}")
        var total = 0L
        try {
            val input = activity.contentResolver.openInputStream(uri) ?: throw IOException("تعذر قراءة الملف")
            input.use { ins ->
                target.outputStream().use { out ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = ins.read(buf)
                        if (n < 0) break
                        total += n
                        if (total > limit) throw IOException("'$originalName' أكبر من الحد المسموح (${maxMb.toInt()} MB)")
                        out.write(buf, 0, n)
                    }
                }
            }
        } catch (e: IOException) {
            target.delete()
            throw e
        } catch (e: SecurityException) {
            target.delete()
            throw IOException("لا صلاحية لقراءة الملف")
        }
        var outFile = target
        var outName = originalName
        var outMime = mime
        var outSize = total
        if (mayShrink && kind == "image") {
            val shrunk = shrinkImage(target, mime, maxDim, quality)
            if (shrunk != null) {
                outFile = shrunk.first; outMime = shrunk.second; outSize = outFile.length()
                val ext = if (outMime == "image/png") "png" else "jpg"
                outName = originalName.substringBeforeLast('.', originalName) + "." + ext
            }
        }
        if (outSize > finalLimit) {
            outFile.delete()
            throw IOException("'$originalName' أكبر من الحد المسموح (${maxMb.toInt()} MB)")
        }
        return JSONObject()
            .put("path", "media/${outFile.name}")
            .put("name", outName)
            .put("mime", outMime)
            .put("size", outSize)
            .put("kind", kind)
    }

    /**
     * تصغير/ضغط صورة: أطول ضلع <= [maxDim] مع احترام اتجاه EXIF، وإعادة ترميز JPEG بجودة [quality]
     * (PNG ذو الشفافية يبقى PNG). يُعيد (الملف الجديد، mime) أو null إن لم يلزم/لم يُدعم الصنف
     * (GIF/SVG لا تُمَسّ). إعادة الترميز تُسقط بيانات EXIF (ومنها الموقع الجغرافي) — ميزة خصوصية.
     * فك الترميز بـ inSampleSize فلا تُحمَّل الصورة كاملة في الذاكرة. إن لم يكن الناتج أصغر ولا حاجة لتصغير الأبعاد يُبقى الأصل.
     */
    private fun shrinkImage(file: File, mime: String, maxDim: Int, quality: Int): Pair<File, String>? {
        if (mime != "image/jpeg" && mime != "image/png" && mime != "image/webp" && mime != "image/heic" && mime != "image/heif") return null
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        val w = bounds.outWidth; val h = bounds.outHeight
        if (w <= 0 || h <= 0) return null
        var sample = 1
        while (maxOf(w, h) / (sample * 2) >= maxDim) sample *= 2
        val decoded = BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample }) ?: return null
        var bmp: Bitmap = decoded
        try {
            val degrees = try {
                when (ExifInterface(file.path).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)) {
                    ExifInterface.ORIENTATION_ROTATE_90 -> 90f
                    ExifInterface.ORIENTATION_ROTATE_180 -> 180f
                    ExifInterface.ORIENTATION_ROTATE_270 -> 270f
                    else -> 0f
                }
            } catch (_: Throwable) { 0f }
            val longest = maxOf(bmp.width, bmp.height)
            val scale = if (longest > maxDim) maxDim.toFloat() / longest else 1f
            val resized = scale < 1f || sample > 1
            if (degrees != 0f || scale < 1f) {
                val m = Matrix().apply { if (scale < 1f) postScale(scale, scale); if (degrees != 0f) postRotate(degrees) }
                val t = Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, m, true)
                if (t !== bmp) { bmp.recycle(); bmp = t }
            }
            val asPng = mime == "image/png" && bmp.hasAlpha()
            val out = File(file.parentFile, file.nameWithoutExtension + "_s." + if (asPng) "png" else "jpg")
            out.outputStream().use { bmp.compress(if (asPng) Bitmap.CompressFormat.PNG else Bitmap.CompressFormat.JPEG, quality, it) }
            if (!resized && degrees == 0f && out.length() >= file.length()) { out.delete(); return null }
            file.delete()
            return out to (if (asPng) "image/png" else "image/jpeg")
        } finally {
            bmp.recycle()
        }
    }

    // ---------------------------------------------------------------- upload

    private fun startUpload(cell: String, url: String, field: String, items: JSONArray, auth: String, retries: Int) {
        if (cell.isBlank()) return
        synchronized(this) {
            if (!uploadingCells.add(cell)) return
        }
        io.execute {
            try {
                val result = uploadWithRetry(cell, url, field, items, auth, retries)
                if (!cancelled.contains(cell)) IndsinPreviewManager.mediaProgress(cell, "done", 100.0, result)
            } catch (t: Throwable) {
                // إن ألغاه المستخدم فالمحرّك أعاد الحالة إلى picked بالفعل: لا نكتب error فوقها.
                if (!cancelled.remove(cell)) IndsinPreviewManager.mediaProgress(cell, "error", 0.0, t.message ?: t.toString())
            } finally {
                cancelled.remove(cell)
                synchronized(this) { uploadingCells.remove(cell); connections.remove(cell) }
            }
        }
    }

    /** خطأ دائم لا تفيد معه إعادة المحاولة (رابط/ملف/4xx). غيره من IOException و5xx/408/429 يُعاد. */
    private class PermanentUploadException(msg: String) : IOException(msg)

    /** إعادة محاولة تلقائية بتراجع أسّي (1s,2s,4s,8s..) مع احترام الإلغاء؛ رقم المحاولة يظهر في `<cell>_attempt`. */
    private fun uploadWithRetry(cell: String, url: String, field: String, items: JSONArray, auth: String, retries: Int): String {
        val maxAttempts = retries + 1
        var attempt = 1
        while (true) {
            if (cancelled.contains(cell)) throw IOException("cancelled")
            if (attempt > 1) IndsinPreviewManager.mediaProgress(cell, "uploading", 0.0, attempt.toString())
            try {
                return multipartUpload(cell, url, field, items, auth)
            } catch (e: PermanentUploadException) {
                throw e
            } catch (e: IOException) {
                if (cancelled.contains(cell)) throw e
                if (attempt >= maxAttempts) {
                    throw IOException(if (maxAttempts > 1) "فشل بعد $maxAttempts محاولات: ${e.message}" else (e.message ?: "فشل الرفع"))
                }
                var waited = 0L
                val delay = minOf(8000L, 1000L shl (attempt - 1))
                while (waited < delay && !cancelled.contains(cell)) { Thread.sleep(200); waited += 200 }
                attempt++
            }
        }
    }

    private val connections = HashMap<String, HttpURLConnection>()
    private val cancelled = java.util.Collections.synchronizedSet(HashSet<String>())

    /** cancelUpload(cell): يقطع الاتصال الجاري؛ المحرّك أعاد الحالة إلى picked قبل وصول الطلب إلى هنا. */
    private fun cancelUpload(cell: String) {
        var conn: HttpURLConnection? = null
        synchronized(this) {
            if (!uploadingCells.contains(cell)) return
            conn = connections[cell]
        }
        cancelled.add(cell) // يُفحص في كل دورة إعادة محاولة وكل تقرير تقدّم
        try { conn?.disconnect() } catch (_: Throwable) {}
    }

    private fun multipartUpload(cell: String, urlStr: String, field: String, items: JSONArray, auth: String): String {
        val lower = urlStr.lowercase()
        if (!lower.startsWith("https://") && !lower.startsWith("http://")) throw PermanentUploadException("رابط الرفع يجب أن يبدأ بـ http(s)")
        val root = File(RinEngine.currentBaseDir()).canonicalFile

        // 1) حلّ الملفات داخل جذر المشروع فقط (حماية من أي مسار هارب).
        class Part(val file: File, val name: String, val mime: String)
        val parts = ArrayList<Part>()
        for (i in 0 until items.length()) {
            val it = items.getJSONObject(i)
            val f = File(root, it.optString("path")).canonicalFile
            if (!f.path.startsWith(root.path + File.separator) || !f.isFile) throw PermanentUploadException("الملف غير موجود: ${it.optString("name")}")
            parts.add(Part(f, it.optString("name", f.name), it.optString("mime", "application/octet-stream")))
        }
        if (parts.isEmpty()) throw PermanentUploadException("لا ملفات للرفع")

        // 2) نبني الترويسات مسبقاً لنحسب Content-Length بدقة ونبث بلا تحميل في الذاكرة.
        val boundary = "----IndsinMedia" + UUID.randomUUID().toString().replace("-", "")
        val crlf = "\r\n"
        fun esc(s: String) = s.replace("\"", "%22").replace("\r", "").replace("\n", "")
        val heads = parts.map {
            ("--$boundary$crlf" +
                "Content-Disposition: form-data; name=\"${esc(field)}\"; filename=\"${esc(it.name)}\"$crlf" +
                "Content-Type: ${it.mime}$crlf$crlf").toByteArray(Charsets.UTF_8)
        }
        val tail = "--$boundary--$crlf".toByteArray(Charsets.UTF_8)
        val crlfBytes = crlf.toByteArray()
        var totalLen = tail.size.toLong()
        parts.forEachIndexed { i, p -> totalLen += heads[i].size + p.file.length() + crlfBytes.size }

        val conn = URL(urlStr).openConnection() as HttpURLConnection
        synchronized(this) { connections[cell] = conn }
        try {
            conn.requestMethod = "POST"
            conn.doOutput = true
            conn.doInput = true
            conn.instanceFollowRedirects = false
            conn.connectTimeout = 30_000
            conn.readTimeout = 120_000
            conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
            conn.setRequestProperty("Accept", "application/json, text/plain, */*")
            if (auth.isNotBlank()) conn.setRequestProperty("Authorization", auth) // محقَّقة في المحرّك (بلا CR/LF)
            conn.setFixedLengthStreamingMode(totalLen)

            var sent = 0L
            var lastPct = -1
            var lastAt = 0L
            fun report(force: Boolean = false) {
                val pct = if (totalLen > 0) (sent * 100 / totalLen).toInt().coerceIn(0, 99) else 0
                val now = System.currentTimeMillis()
                if (cancelled.contains(cell)) throw IOException("cancelled")
                if (force || (pct != lastPct && now - lastAt >= 120)) {
                    lastPct = pct; lastAt = now
                    IndsinPreviewManager.mediaProgress(cell, "uploading", pct.toDouble(), "")
                }
            }
            conn.outputStream.use { out ->
                val buf = ByteArray(32 * 1024)
                parts.forEachIndexed { i, p ->
                    out.write(heads[i]); sent += heads[i].size
                    FileInputStream(p.file).use { ins ->
                        while (true) {
                            val n = ins.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n); sent += n
                            report()
                        }
                    }
                    out.write(crlfBytes); sent += crlfBytes.size
                }
                out.write(tail); sent += tail.size
            }

            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else (conn.errorStream ?: conn.inputStream)
            val body = stream?.use { s ->
                val bos = java.io.ByteArrayOutputStream()
                val b = ByteArray(4096)
                while (bos.size() < 4096) { val n = s.read(b); if (n < 0) break; bos.write(b, 0, n) }
                bos.toString("UTF-8").take(4096)
            } ?: ""
            if (code !in 200..299) {
                val msg = "الخادم ردّ بالحالة $code" + if (body.isNotBlank()) ": ${body.take(200)}" else ""
                // 5xx/408/429 عابرة فتُعاد؛ بقية 4xx (و3xx: لا نتبع التحويل) دائمة.
                if (code >= 500 || code == 408 || code == 429) throw IOException(msg) else throw PermanentUploadException(msg)
            }
            return body
        } finally {
            conn.disconnect()
        }
    }

    private fun toast(msg: String) = main.post { Toast.makeText(activity, msg, Toast.LENGTH_SHORT).show() }
}
