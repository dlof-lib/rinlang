package com.dlof.rinlang

import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.media.MediaMetadataRetriever
import android.os.Handler
import android.os.Looper
import android.util.LruCache
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import java.io.File
import java.util.Locale
import java.util.concurrent.Executors

/**
 * مظهر أيقونة صف الملف في شاشة «الملفات».
 *
 *  - ملف عادي  : أيقونة النوع داخل بلاطة مدوّرة (عبر [FileIconResolver.load] كما كان).
 *  - صورة      : مصغّرة حقيقية تملأ البلاطة (centerCrop + زوايا مقصوصة) مع لصيقة نوع (JPG/PNG...) وأبعاد الصورة في سطر المعلومات.
 *  - فيديو     : إطار حقيقي من الفيديو + زر تشغيل + المدة في سطر المعلومات.
 *
 * التحميل بخيطين خلفيين فقط (لا خيط لكل صف أثناء التمرير السريع) مع ذاكرة مؤقتة LRU للمصغّرات، وتُدوَّر الصورة
 * حسب EXIF فتظهر صور الكاميرا باتجاهها الصحيح. نتيجة متأخرة لصف أُعيد تدويره لملف آخر تُتجاهل عبر وسم المفتاح.
 */
object FileRowStyle {

    private val imageExtensions = setOf("jpg", "jpeg", "png", "webp", "gif", "bmp", "heic", "heif", "avif")
    private val videoExtensions = setOf("mp4", "mkv", "webm", "3gp", "mov", "avi")

    private val main = Handler(Looper.getMainLooper())
    private val pool = Executors.newFixedThreadPool(2)

    private class Thumb(val bitmap: Bitmap, val info: String)

    private val cache = object : LruCache<String, Thumb>((Runtime.getRuntime().maxMemory() / 32L).toInt().coerceAtLeast(2 * 1024 * 1024)) {
        override fun sizeOf(key: String, value: Thumb): Int = value.bitmap.byteCount
    }

    fun isImage(file: File): Boolean = file.extension.lowercase(Locale.ROOT) in imageExtensions
    fun isVideo(file: File): Boolean = file.extension.lowercase(Locale.ROOT) in videoExtensions

    /**
     * يضبط أيقونة الصف. [onInfo] تُستدعى (على الخيط الرئيسي) بنص قصير مثل «1920×1080» أو «0:42»
     * ليُضاف أمام سطر الحجم/التاريخ؛ تُستدعى فقط إن كان الصف ما زال مربوطاً بنفس الملف.
     */
    fun bind(icon: ImageView, typeBadge: TextView, play: View, file: File, onInfo: ((String) -> Unit)?) {
        val d = icon.resources.displayMetrics.density
        val ext = file.extension.lowercase(Locale.ROOT)
        val image = ext in imageExtensions
        val video = ext in videoExtensions

        if (!image && !video) {
            showIconMode(icon, typeBadge, play, d)
            FileIconResolver.load(icon, file)
            return
        }

        // وضع المصغّرة: تملأ البلاطة بلا حشوة.
        val key = file.absolutePath + "|" + file.lastModified() + "|" + file.length()
        icon.tag = key
        icon.imageTintList = null
        icon.setPadding(0, 0, 0, 0)
        icon.scaleType = ImageView.ScaleType.CENTER_CROP
        typeBadge.text = when (ext) { "jpeg" -> "JPG"; "heif" -> "HEIC"; else -> ext.uppercase(Locale.ROOT) }
        typeBadge.visibility = View.VISIBLE
        play.visibility = if (video) View.VISIBLE else View.GONE

        cache.get(key)?.let {
            icon.setImageBitmap(it.bitmap)
            if (it.info.isNotEmpty()) onInfo?.invoke(it.info)
            return
        }
        icon.setImageDrawable(null)

        val targetPx = (84 * d).toInt()
        pool.execute {
            val thumb = try {
                if (video) decodeVideo(file, targetPx) else decodeImage(file, targetPx)
            } catch (t: Throwable) {
                null
            }
            if (thumb != null) cache.put(key, thumb)
            main.post {
                if (icon.tag != key) return@post
                if (thumb != null) {
                    icon.setImageBitmap(thumb.bitmap)
                    if (thumb.info.isNotEmpty()) onInfo?.invoke(thumb.info)
                } else {
                    // تعذّر فكّ الملف (تالف أو صيغة غير مدعومة): نعرض أيقونة صورة هادئة بدل بلاطة فارغة.
                    showIconMode(icon, typeBadge, play, d)
                    icon.setImageResource(if (video) R.drawable.rs_ic_play else R.drawable.rs_ic_image)
                    icon.imageTintList = ColorStateList.valueOf(ContextCompat.getColor(icon.context, R.color.rin_files_dim))
                }
            }
        }
    }

    private fun showIconMode(icon: ImageView, typeBadge: TextView, play: View, d: Float) {
        icon.tag = null
        icon.imageTintList = null
        icon.scaleType = ImageView.ScaleType.FIT_CENTER
        val p = (10 * d).toInt()
        icon.setPadding(p, p, p, p)
        typeBadge.visibility = View.GONE
        play.visibility = View.GONE
    }

    private fun decodeImage(file: File, target: Int): Thumb? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.absolutePath, bounds)
        var w = bounds.outWidth
        var h = bounds.outHeight
        if (w <= 0 || h <= 0) return null

        var sample = 1
        while (w / (sample * 2) >= target && h / (sample * 2) >= target) sample *= 2
        var bmp = BitmapFactory.decodeFile(file.absolutePath, BitmapFactory.Options().apply { inSampleSize = sample })
            ?: return null

        val orientation = try {
            ExifInterface(file.absolutePath).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
        } catch (_: Throwable) {
            ExifInterface.ORIENTATION_NORMAL
        }
        val m = Matrix()
        when (orientation) {
            ExifInterface.ORIENTATION_ROTATE_90 -> m.postRotate(90f)
            ExifInterface.ORIENTATION_ROTATE_180 -> m.postRotate(180f)
            ExifInterface.ORIENTATION_ROTATE_270 -> m.postRotate(270f)
            ExifInterface.ORIENTATION_TRANSPOSE -> { m.postRotate(90f); m.postScale(-1f, 1f) }
            ExifInterface.ORIENTATION_TRANSVERSE -> { m.postRotate(270f); m.postScale(-1f, 1f) }
            ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> m.postScale(-1f, 1f)
            ExifInterface.ORIENTATION_FLIP_VERTICAL -> m.postScale(1f, -1f)
        }
        if (!m.isIdentity) {
            val rotated = try {
                Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, m, true)
            } catch (_: Throwable) {
                null
            }
            if (rotated != null && rotated !== bmp) { bmp.recycle(); bmp = rotated }
            if (orientation == ExifInterface.ORIENTATION_ROTATE_90 || orientation == ExifInterface.ORIENTATION_ROTATE_270 ||
                orientation == ExifInterface.ORIENTATION_TRANSPOSE || orientation == ExifInterface.ORIENTATION_TRANSVERSE) {
                val t = w; w = h; h = t
            }
        }
        return Thumb(bmp, "$w×$h")
    }

    private fun decodeVideo(file: File, target: Int): Thumb? {
        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(file.absolutePath)
            val ms = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            val frame = r.getScaledFrameAtTime(
                (if (ms > 2000L) 1_000_000L else 0L), MediaMetadataRetriever.OPTION_CLOSEST_SYNC, target, target
            ) ?: return null
            Thumb(frame, formatDuration(ms))
        } finally {
            try { r.release() } catch (_: Throwable) {}
        }
    }

    private fun formatDuration(ms: Long): String {
        if (ms <= 0L) return ""
        val total = ms / 1000
        val h = total / 3600
        val m = (total % 3600) / 60
        val s = total % 60
        return if (h > 0) String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s) else String.format(Locale.ROOT, "%d:%02d", m, s)
    }
}
