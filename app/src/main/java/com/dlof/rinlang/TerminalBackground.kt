package com.dlof.rinlang

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.Rect
import android.graphics.drawable.AnimatedImageDrawable
import android.graphics.drawable.Drawable
import android.graphics.ImageDecoder
import android.net.Uri
import java.io.File

/** خلفية صورة بقصّ مركزي وطبقة تعتيم [dim] (0..90 %) لضمان قراءة النص فوقها. */
class CropDimDrawable(private val bitmap: Bitmap, private val base: Int, dim: Int) : Drawable() {
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
    private val dimPaint = Paint().apply { color = base; alpha = (dim.coerceIn(0, 90) * 255 / 100) }
    private val src = Rect()

    override fun draw(c: Canvas) {
        val b = bounds
        if (b.isEmpty) return
        val r = CropMath.centerCrop(bitmap.width, bitmap.height, b.width(), b.height())
        src.set(r[0], r[1], r[2], r[3])
        c.drawBitmap(bitmap, src, b, paint)
        c.drawRect(b, dimPaint)
    }

    override fun setAlpha(alpha: Int) { paint.alpha = alpha }
    override fun setColorFilter(cf: ColorFilter?) { paint.colorFilter = cf }
    @Deprecated("Deprecated in Java") override fun getOpacity(): Int = PixelFormat.OPAQUE
}

/**
 * خلفية متحركة (GIF / WebP متحرك / APNG) بقصّ مركزي «تغطية» وتعتيم [dim].
 * تُشغَّل وتُوقف عبر [LiveBackground]؛ عند الإيقاف تبقى آخر لقطة.
 */
class AnimatedCropDrawable(private val context: Context, private val anim: AnimatedImageDrawable, private val base: Int, dim: Int) :
    Drawable(), Drawable.Callback, LiveBackground {
    private val dimPaint = Paint().apply { color = base; alpha = (dim.coerceIn(0, 90) * 255 / 100) }
    private var allowed = true
    private var attached = false

    init {
        anim.repeatCount = AnimatedImageDrawable.REPEAT_INFINITE
        anim.callback = this
    }

    override fun draw(c: Canvas) {
        val b = bounds
        if (b.isEmpty) return
        val iw = anim.intrinsicWidth; val ih = anim.intrinsicHeight
        val t = CropMath.coverTransform(iw, ih, b.width(), b.height())
        c.save()
        c.clipRect(b)
        c.translate(b.left + t[1], b.top + t[2])
        c.scale(t[0], t[0])
        anim.setBounds(0, 0, iw, ih)
        anim.draw(c)
        c.restore()
        c.drawRect(b, dimPaint)
    }

    private fun sync() {
        if (allowed && attached && !reduceMotion(context)) { if (!anim.isRunning) anim.start() }
        else if (anim.isRunning) anim.stop()
    }

    override fun setAnimated(on: Boolean) { allowed = on; sync() }
    override fun start() { attached = true; sync() }
    override fun stop() { attached = false; sync() }

    override fun invalidateDrawable(who: Drawable) { invalidateSelf() }
    override fun scheduleDrawable(who: Drawable, what: Runnable, `when`: Long) { scheduleSelf(what, `when`) }
    override fun unscheduleDrawable(who: Drawable, what: Runnable) { unscheduleSelf(what) }

    override fun setAlpha(alpha: Int) {}
    override fun setColorFilter(cf: ColorFilter?) {}
    @Deprecated("Deprecated in Java") override fun getOpacity(): Int = PixelFormat.OPAQUE
}

/** استيراد صورة الخلفية: تُصغَّر وتُحفظ داخل التطبيق (لا تعتمد على بقاء الملف الأصلي). */
object TerminalBackground {
    private const val MAX_SIDE = 1600

    private const val MAX_ANIM_BYTES = 20L * 1024 * 1024   // سقف حجم الملف المتحرك (20MB)
    private const val ANIM_MAX_SIDE = 1080

    fun file(ctx: Context) = File(ctx.filesDir, "terminal_bg.jpg")
    fun animFile(ctx: Context) = File(ctx.filesDir, "terminal_bg_anim.bin")

    /** نتيجة محاولة استيراد ملف متحرك. */
    enum class AnimResult { OK, NOT_ANIMATED, TOO_BIG, FAILED }

    /**
     * ينسخ الملف داخل التطبيق ويتحقق أنه متحرك فعلاً (GIF/WebP/APNG). إن لم يكن متحركاً يُحذف
     * ويُرجع [AnimResult.NOT_ANIMATED] ليُعامَل كصورة ثابتة.
     */
    fun importAnimated(ctx: Context, uri: Uri): AnimResult {
        val tmp = File(ctx.filesDir, "terminal_bg_anim.tmp")
        return try {
            var total = 0L
            ctx.contentResolver.openInputStream(uri)?.use { input ->
                tmp.outputStream().use { out ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buf); if (n < 0) break
                        total += n
                        if (total > MAX_ANIM_BYTES) { tmp.delete(); return AnimResult.TOO_BIG }
                        out.write(buf, 0, n)
                    }
                }
            } ?: return AnimResult.FAILED
            val d = decodeAnimated(tmp)
            if (d == null) { tmp.delete(); AnimResult.NOT_ANIMATED }
            else {
                d.stop()
                animFile(ctx).delete()
                if (tmp.renameTo(animFile(ctx))) AnimResult.OK else { tmp.delete(); AnimResult.FAILED }
            }
        } catch (t: Throwable) { try { tmp.delete() } catch (_: Throwable) {}; AnimResult.FAILED }
    }

    private fun decodeAnimated(f: File): AnimatedImageDrawable? = try {
        val d = ImageDecoder.decodeDrawable(ImageDecoder.createSource(f)) { decoder, info, _ ->
            decoder.setTargetSampleSize(CropMath.sampleSize(info.size.width, info.size.height, ANIM_MAX_SIDE))
        }
        d as? AnimatedImageDrawable
    } catch (t: Throwable) { null }

    /** يحمّل الخلفية المتحركة المحفوظة (أو null). */
    fun loadAnimated(ctx: Context): AnimatedImageDrawable? {
        val f = animFile(ctx)
        return if (f.isFile) decodeAnimated(f) else null
    }

    fun importFrom(ctx: Context, uri: Uri): Boolean = try {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        ctx.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) false
        else {
            val opts = BitmapFactory.Options().apply { inSampleSize = CropMath.sampleSize(bounds.outWidth, bounds.outHeight, MAX_SIDE) }
            val bmp = ctx.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
            if (bmp == null) false
            else {
                file(ctx).outputStream().use { bmp.compress(Bitmap.CompressFormat.JPEG, 88, it) }
                animFile(ctx).delete()   // صورة ثابتة جديدة تلغي أي خلفية متحركة سابقة
                bmp.recycle()
                true
            }
        }
    } catch (t: Throwable) { false }

    fun load(ctx: Context): Bitmap? = try {
        val f = file(ctx)
        if (f.isFile) BitmapFactory.decodeFile(f.path) else null
    } catch (t: Throwable) { null }

    fun clear(ctx: Context) { try { file(ctx).delete(); animFile(ctx).delete() } catch (t: Throwable) {} }
}
