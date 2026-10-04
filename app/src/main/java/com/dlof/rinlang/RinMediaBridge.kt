package com.dlof.rinlang

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.AudioAttributes
import android.media.MediaPlayer
import androidx.core.content.FileProvider
import com.google.android.gms.tasks.Tasks
import com.google.mlkit.vision.common.InputImage
import com.google.mlkit.vision.text.TextRecognition
import com.google.mlkit.vision.text.latin.TextRecognizerOptions
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.TimeUnit

/**
 * الطرف الحقيقي (Kotlin) لجسر الوسائط على أندرويد: يُستدعى من `jni_bridge.cpp`
 * (`callKotlinMediaBridge`) ويُسجَّل عبر `rin::media::setBridge(...)` في `rin_media.h`، فتصبح
 * `make.video / make.audio / make.image.removeBg / make.ocr` تعمل على الجهاز بدل أدوات سطر الأوامر
 * (ffmpeg/tesseract...) غير الموجودة على أندرويد. انظر docs/MAKE_MEDIA.md.
 *
 * العقد (يطابق جانب C++ حرفياً — أي تغيير يلزمه تحديث jni_bridge.cpp):
 *   call(op, keys[], values[]) -> String[]  حيث
 *     [0] "1" إن عالج الجسر هذه العملية، "0" ليجرّب C++ التنفيذ المحلي
 *     [1] "1" إن نجحت العملية
 *     [2] رسالة الخطأ (فارغة عند النجاح)
 *     [3..] أزواج مفتاح/قيمة إضافية تُدمَج في القاموس المُرجَع لكود Rin (path/bytes/width/...)
 *
 * العمليات: video.open · audio.play · audio.stop · image.removeBg · image.ocr · ocr.langs
 * يُنفَّذ على ترد الاستدعاء (خلفي عادةً، كالجسر الشبكي) — لا شيء هنا يلمس واجهة المستخدم مباشرة.
 */
object RinMediaBridge {

    @Volatile private var appContext: Context? = null
    @Volatile private var player: MediaPlayer? = null

    /** يُستدعى مرة واحدة من [RinApplication.onCreate]. */
    fun init(context: Context) { appContext = context.applicationContext }

    @JvmStatic
    fun call(op: String, keys: Array<String>, values: Array<String>): Array<String> {
        val args = HashMap<String, String>()
        for (i in keys.indices) if (i < values.size) args[keys[i]] = values[i]
        return try {
            when (op) {
                "video.open" -> openVideo(args)
                "audio.play" -> playAudio(args)
                "audio.stop" -> stopAudio()
                "image.removeBg" -> removeBg(args)
                "image.ocr" -> ocr(args)
                "ocr.langs" -> ocrLangs()
                else -> arrayOf("0", "0", "")
            }
        } catch (t: Throwable) {
            fail("استثناء في جسر الوسائط ($op): ${t.message ?: t.javaClass.simpleName}")
        }
    }

    private fun ok(vararg kv: Pair<String, String>): Array<String> {
        val out = ArrayList<String>()
        out.add("1"); out.add("1"); out.add("")
        for ((k, v) in kv) { out.add(k); out.add(v) }
        return out.toTypedArray()
    }

    private fun fail(msg: String): Array<String> = arrayOf("1", "0", msg)

    private fun isUrl(s: String) = s.startsWith("http://", true) || s.startsWith("https://", true)

    // ---------------------------------------------------------------- فيديو
    private fun openVideo(a: Map<String, String>): Array<String> {
        val ctx = appContext ?: return fail("جسر الوسائط غير مُهيَّأ (RinMediaBridge.init لم يُستدعَ)")
        val src = a["src"] ?: return fail("مصدر الفيديو مفقود")
        val intent = Intent(Intent.ACTION_VIEW).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_GRANT_READ_URI_PERMISSION)
        if (isUrl(src)) {
            intent.setDataAndType(android.net.Uri.parse(src), "video/*")
        } else {
            val f = File(src)
            if (!f.isFile) return fail("الملف غير موجود: $src")
            val uri = FileProvider.getUriForFile(ctx, ctx.packageName + ".fileprovider", f)
            intent.setDataAndType(uri, "video/*")
        }
        ctx.startActivity(intent)
        return ok("backend" to "android.intent", "playing" to "true")
    }

    // ---------------------------------------------------------------- صوت
    @Synchronized
    private fun playAudio(a: Map<String, String>): Array<String> {
        val src = a["src"] ?: return fail("مصدر الصوت مفقود")
        if (!isUrl(src) && !File(src).isFile) return fail("الملف غير موجود: $src")
        stopAudioInternal()
        val mp = MediaPlayer()
        try {
            mp.setAudioAttributes(AudioAttributes.Builder()
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).setUsage(AudioAttributes.USAGE_MEDIA).build())
            mp.setDataSource(src)
            mp.isLooping = a["loop"] == "true"
            val vol = a["volume"]?.toFloatOrNull()
            if (vol != null) { val v = (vol / 100f).coerceIn(0f, 1f); mp.setVolume(v, v) }
            mp.prepare()                       // متزامن عمداً: أخطاء الملف تظهر في النتيجة
            val start = a["start"]?.toDoubleOrNull() ?: 0.0
            if (start > 0) mp.seekTo((start * 1000).toInt())
            val dur = mp.duration / 1000.0
            mp.start()
            player = mp
            if (a["wait"] == "true") {
                while (mp.isPlaying) Thread.sleep(100)
                stopAudioInternal()
            }
            return ok("backend" to "android.mediaplayer", "duration" to dur.toString())
        } catch (t: Throwable) {
            try { mp.release() } catch (_: Throwable) {}
            return fail("تعذّر تشغيل الصوت: ${t.message ?: t.javaClass.simpleName}")
        }
    }

    @Synchronized private fun stopAudio(): Array<String> {
        val had = player != null
        stopAudioInternal()
        return ok("stopped" to (if (had) "1" else "0"))
    }

    private fun stopAudioInternal() {
        val p = player ?: return
        player = null
        try { if (p.isPlaying) p.stop() } catch (_: Throwable) {}
        try { p.release() } catch (_: Throwable) {}
    }

    // ---------------------------------------------------------------- OCR: Tesseract (عربي+إنجليزي) + ML Kit (لاتيني)
    // ML Kit لا يدعم العربية إطلاقاً (لاتيني/صيني/ديفاناغاري/ياباني/كوري فقط)، لذا العربية تمر عبر Tesseract
    // (مكتبة Tesseract4Android) بملفات <code>.traineddata. المحرّك يُختار تلقائياً:
    //   engine=tesseract | mlkit | auto (الافتراضي)
    //   lang غير محدد  -> ara+eng عبر Tesseract إن كان ara.traineddata + eng.traineddata متاحين، وإلا ML Kit (لاتيني) مع تحذير
    //   lang فيه غير eng/latin -> Tesseract حصراً
    //   lang=eng أو latin -> ML Kit (أسرع وبلا ملفات)، إلا إن طُلب engine=tesseract
    // ملفات اللغة تُبحث بالترتيب: filesDir/tessdata ثم assets/tessdata (تُنسخ مرة واحدة) ثم — إن مرّر المستخدم
    // download=true — تُنزَّل من tessdata_fast (https) وتُحفظ. السكربت scripts/fetch_tessdata.sh يضمّنها في assets.
    private const val TESSDATA_BASE = "https://github.com/tesseract-ocr/tessdata_fast/raw/main/"
    private const val MIN_TRAINEDDATA_BYTES = 100_000L        // أصغر ملف صالح ~ 1.4MB؛ ما دون ذلك صفحة خطأ/ملف تالف
    private val ocrLock = Any()

    private fun tessDir(ctx: Context): File = File(ctx.filesDir, "tessdata")

    private fun langOk(code: String): Boolean =
        code.isNotEmpty() && code.length <= 24 && code.all { it.isLetterOrDigit() || it == '_' }

    private fun trainedFile(ctx: Context, code: String): File = File(tessDir(ctx), "$code.traineddata")

    /** يجعل اللغة متاحة في filesDir/tessdata. يُرجع null عند النجاح أو رسالة الخطأ. */
    private fun ensureLang(ctx: Context, code: String, allowDownload: Boolean): String? {
        val dst = trainedFile(ctx, code)
        if (dst.isFile && dst.length() >= MIN_TRAINEDDATA_BYTES) return null
        tessDir(ctx).mkdirs()
        // 1) من assets
        try {
            ctx.assets.open("tessdata/$code.traineddata").use { input ->
                val tmp = File(dst.parentFile, "$code.traineddata.part")
                FileOutputStream(tmp).use { input.copyTo(it) }
                if (tmp.length() >= MIN_TRAINEDDATA_BYTES && tmp.renameTo(dst)) return null
                tmp.delete()
            }
        } catch (_: java.io.FileNotFoundException) { /* غير مضمَّن */ }
        // 2) تنزيل اختياري
        if (!allowDownload)
            return "ملف اللغة '$code.traineddata' غير موجود. ضمّنه في app/src/main/assets/tessdata/ " +
                "(شغّل scripts/fetch_tessdata.sh) أو مرّر download: true ليُنزَّل مرة واحدة من tessdata_fast"
        val tmp = File(dst.parentFile, "$code.traineddata.part")
        try {
            val conn = (java.net.URL(TESSDATA_BASE + code + ".traineddata").openConnection() as java.net.HttpURLConnection).apply {
                connectTimeout = 15000; readTimeout = 60000; instanceFollowRedirects = true
            }
            if (conn.responseCode != 200) return "تعذّر تنزيل '$code.traineddata' (الخادوم أعاد ${conn.responseCode})"
            conn.inputStream.use { input -> FileOutputStream(tmp).use { input.copyTo(it) } }
            if (tmp.length() < MIN_TRAINEDDATA_BYTES) { tmp.delete(); return "الملف المنزَّل لـ '$code' صغير جداً أو تالف" }
            if (!tmp.renameTo(dst)) { tmp.delete(); return "تعذّر حفظ '$code.traineddata'" }
            return null
        } catch (t: Throwable) {
            tmp.delete()
            return "تعذّر تنزيل '$code.traineddata': ${t.message ?: t.javaClass.simpleName}"
        }
    }

    private fun haveLang(ctx: Context, code: String): Boolean {
        val f = trainedFile(ctx, code)
        if (f.isFile && f.length() >= MIN_TRAINEDDATA_BYTES) return true
        return try { ctx.assets.open("tessdata/$code.traineddata").close(); true } catch (_: Throwable) { false }
    }

    /** op=ocr.langs: ما الذي يقرؤه هذا الجهاز الآن (يستعمله make.media.tools). */
    private fun ocrLangs(): Array<String> {
        val ctx = appContext ?: return fail("جسر الوسائط غير مُهيَّأ")
        val have = ArrayList<String>()
        tessDir(ctx).listFiles()?.forEach { if (it.name.endsWith(".traineddata") && it.length() >= MIN_TRAINEDDATA_BYTES) have.add(it.name.removeSuffix(".traineddata")) }
        try { ctx.assets.list("tessdata")?.forEach { if (it.endsWith(".traineddata")) have.add(it.removeSuffix(".traineddata")) } } catch (_: Throwable) {}
        return ok("langs" to have.distinct().sorted().joinToString(","), "mlkit" to "latin")
    }

    private fun decodeForOcr(f: File, enhance: Boolean): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(f.path, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1
        while ((bounds.outWidth / sample).toLong() * (bounds.outHeight / sample) > 16_000_000L) sample *= 2
        val opts = BitmapFactory.Options().apply { inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888 }
        var bmp = BitmapFactory.decodeFile(f.path, opts) ?: return null
        if (enhance) {
            // تدرّج رمادي + تباين أعلى، وتكبير 2× للصور الصغيرة (Tesseract يقرأ الحروف الصغيرة بشكل سيئ)
            val scale = if (bmp.width < 1500) 2 else 1
            val out = Bitmap.createBitmap(bmp.width * scale, bmp.height * scale, Bitmap.Config.ARGB_8888)
            val c = android.graphics.Canvas(out)
            val cm = android.graphics.ColorMatrix().apply { setSaturation(0f) }
            val k = 1.4f; val t = (-0.5f * k + 0.5f) * 255f
            cm.postConcat(android.graphics.ColorMatrix(floatArrayOf(k, 0f, 0f, 0f, t, 0f, k, 0f, 0f, t, 0f, 0f, k, 0f, t, 0f, 0f, 0f, 1f, 0f)))
            val paint = android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG).apply { colorFilter = android.graphics.ColorMatrixColorFilter(cm) }
            c.drawBitmap(bmp, null, android.graphics.Rect(0, 0, out.width, out.height), paint)
            if (out !== bmp) bmp.recycle()
            bmp = out
        }
        return bmp
    }

    private fun ocr(a: Map<String, String>): Array<String> {
        val ctx = appContext ?: return fail("جسر الوسائط غير مُهيَّأ (RinMediaBridge.init لم يُستدعَ)")
        val src = a["src"] ?: return fail("مصدر الصورة مفقود")
        val f = File(src)
        if (!f.isFile) return fail("الملف غير موجود: $src")

        val wantLang = (a["lang"] ?: "").trim()
        val langs = if (wantLang.isEmpty()) emptyList() else wantLang.split('+').map { it.trim() }
        if (langs.any { !langOk(it) }) return fail("اسم لغة غير صالح: '$wantLang'")
        val engine = (a["engine"] ?: "auto").lowercase()
        if (engine != "auto" && engine != "tesseract" && engine != "mlkit") return fail("engine غير معروف: '$engine' — المتاح auto | tesseract | mlkit")
        val download = a["download"] == "true"
        val enhance = a["enhance"] == "true"

        val latinOnly = langs.isNotEmpty() && langs.all { it == "eng" || it == "latin" }
        val useTesseract = when (engine) {
            "tesseract" -> true
            "mlkit" -> false
            else -> when {
                langs.isEmpty() -> haveLang(ctx, "ara") && haveLang(ctx, "eng")    // الافتراضي: عربي+إنجليزي إن أمكن
                latinOnly -> false
                else -> true
            }
        }
        if (!useTesseract && langs.any { it != "eng" && it != "latin" })
            return fail("ML Kit يقرأ الأحرف اللاتينية فقط — للغة '$wantLang' استخدم engine: \"tesseract\" (أو اترك engine تلقائياً)")
        return if (useTesseract) ocrTesseract(ctx, f, if (langs.isEmpty()) listOf("ara", "eng") else langs, a, download, enhance)
        else ocrMlKit(ctx, f, a, enhance, defaulted = langs.isEmpty())
    }

    private fun ocrTesseract(ctx: Context, f: File, langs: List<String>, a: Map<String, String>, download: Boolean, enhance: Boolean): Array<String> {
        for (l in langs) ensureLang(ctx, l, download)?.let { return fail(it) }
        val psm = (a["psm"]?.toDoubleOrNull()?.toInt() ?: 3).coerceIn(0, 13)
        val bmp = decodeForOcr(f, enhance) ?: return fail("الملف ليس صورة مدعومة")
        val joined = langs.joinToString("+")
        synchronized(ocrLock) {
            val api = com.googlecode.tesseract.android.TessBaseAPI()
            try {
                // dataPath = المجلد الأب لـ tessdata/
                if (!api.init(ctx.filesDir.absolutePath, joined)) return fail("تعذّر تهيئة Tesseract للغات '$joined' (ملف تالف؟ احذف filesDir/tessdata وأعد المحاولة)")
                api.setPageSegMode(psm)
                api.setImage(bmp)
                val text = (api.getUTF8Text() ?: "").trim()
                val conf = api.meanConfidence().coerceIn(0, 100)
                val words = text.split(Regex("\\s+")).count { it.isNotEmpty() }
                val out = arrayListOf("text" to text, "confidence" to conf.toString(), "wordCount" to words.toString(),
                    "lang" to joined, "psm" to psm.toString(), "backend" to "android.tesseract", "enhanced" to enhance.toString())
                if (a["minConfidence"] != null) out.add("warning" to "minConfidence لا يُطبَّق على كلمات Tesseract في أندرويد (يُرجع الثقة العامة فقط)")
                if (words == 0) out.add("warning" to "لم يُعثر على نص — جرّب enhance: true أو psm مختلفاً")
                return ok(*out.toTypedArray())
            } finally {
                api.recycle()
                bmp.recycle()
            }
        }
    }

    private fun ocrMlKit(ctx: Context, f: File, a: Map<String, String>, enhance: Boolean, defaulted: Boolean): Array<String> {
        val minConf = (a["minConfidence"]?.toDoubleOrNull() ?: 0.0).coerceIn(0.0, 100.0) / 100.0
        val image = InputImage.fromFilePath(ctx, android.net.Uri.fromFile(f))
        val recognizer = TextRecognition.getClient(TextRecognizerOptions.DEFAULT_OPTIONS)
        try {
            val result = Tasks.await(recognizer.process(image), 60, TimeUnit.SECONDS)
            val lines = ArrayList<String>()
            var confSum = 0.0
            var words = 0
            for (block in result.textBlocks) {
                for (line in block.lines) {
                    val kept = line.elements.filter { it.confidence >= minConf }
                    if (kept.isEmpty()) continue
                    lines.add(kept.joinToString(" ") { it.text })
                    for (e in kept) { confSum += e.confidence; words++ }
                }
            }
            val avg = if (words > 0) Math.round(confSum / words * 10000.0) / 100.0 else 0.0
            val out = arrayListOf("text" to lines.joinToString("\n"), "confidence" to avg.toString(),
                "wordCount" to words.toString(), "lang" to "latin", "backend" to "android.mlkit")
            if (defaulted)
                out.add("warning" to "العربية غير مفعّلة: ضمّن ara.traineddata و eng.traineddata في assets/tessdata (scripts/fetch_tessdata.sh) " +
                    "أو مرّر lang: \"ara+eng\", download: true — الآن قُرئت الأحرف اللاتينية فقط")
            if (words == 0) out.add("warning" to "لم يُعثر على نص")
            return ok(*out.toTypedArray())
        } finally {
            recognizer.close()
        }
    }

    // ---------------------------------------------------------------- إزالة الخلفية (ملء فيضي من الحواف)
    private fun removeBg(a: Map<String, String>): Array<String> {
        val src = a["src"] ?: return fail("مصدر الصورة مفقود")
        val out = a["out"] ?: return fail("مسار الخرج مفقود")
        if (!out.endsWith(".png", true)) return fail("على أندرويد ملف الخرج يجب أن يكون .png")
        val tol = (a["tolerance"]?.toDoubleOrNull() ?: 12.0).coerceIn(0.0, 100.0)
        val opts = BitmapFactory.Options().apply { inMutable = true; inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val bmp = BitmapFactory.decodeFile(src, opts) ?: return fail("الملف ليس صورة مدعومة")
        val w = bmp.width; val h = bmp.height
        if (w.toLong() * h > 40_000_000L) return fail("الصورة كبيرة جداً (> 40 ميغابكسل)")
        val px = IntArray(w * h)
        bmp.getPixels(px, 0, w, 0, 0, w, h)

        // لون الخلفية: اللون الأكثر تكراراً بين الزوايا الأربع، أو color=#rrggbb
        val corners = intArrayOf(px[0], px[w - 1], px[(h - 1) * w], px[h * w - 1])
        var bg = corners.maxByOrNull { c -> corners.count { it == c } } ?: corners[0]
        a["color"]?.let { c -> try { bg = android.graphics.Color.parseColor(c) } catch (_: Throwable) { return fail("لون color غير صالح") } }
        if ((bg ushr 24) == 0) return ok("path" to out, "alreadyTransparent" to "true")

        val maxDist = tol / 100.0 * 255.0 * 1.7320508          // نفس مقياس fuzz (مسافة RGB)
        fun near(c: Int): Boolean {
            val dr = ((c shr 16) and 255) - ((bg shr 16) and 255)
            val dg = ((c shr 8) and 255) - ((bg shr 8) and 255)
            val db = (c and 255) - (bg and 255)
            return Math.sqrt((dr * dr + dg * dg + db * db).toDouble()) <= maxDist
        }
        val seen = BooleanArray(w * h)
        val stack = IntArray(w * h)
        var sp = 0
        fun push(i: Int) { if (!seen[i] && near(px[i])) { seen[i] = true; stack[sp++] = i } }
        for (x in 0 until w) { push(x); push((h - 1) * w + x) }
        for (y in 0 until h) { push(y * w); push(y * w + w - 1) }
        var removed = 0L
        while (sp > 0) {
            val i = stack[--sp]
            px[i] = 0                                           // شفاف تماماً
            removed++
            val x = i % w; val y = i / w
            if (x > 0) push(i - 1)
            if (x < w - 1) push(i + 1)
            if (y > 0) push(i - w)
            if (y < h - 1) push(i + w)
        }
        val res = Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888)
        File(out).parentFile?.mkdirs()
        FileOutputStream(out).use { res.compress(Bitmap.CompressFormat.PNG, 100, it) }
        val pct = Math.round(removed * 10000.0 / (w.toLong() * h)) / 100.0
        return ok("path" to out, "method" to "color", "width" to w.toString(), "height" to h.toString(),
            "removedPercent" to pct.toString(), "bytes" to File(out).length().toString())
    }
}
