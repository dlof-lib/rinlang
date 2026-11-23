package com.dlof.rinlang

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.Rect
import android.graphics.drawable.Drawable
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

/** استيراد صورة الخلفية: تُصغَّر وتُحفظ داخل التطبيق (لا تعتمد على بقاء الملف الأصلي). */
object TerminalBackground {
    private const val MAX_SIDE = 1600

    fun file(ctx: Context) = File(ctx.filesDir, "terminal_bg.jpg")

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
                bmp.recycle()
                true
            }
        }
    } catch (t: Throwable) { false }

    fun load(ctx: Context): Bitmap? = try {
        val f = file(ctx)
        if (f.isFile) BitmapFactory.decodeFile(f.path) else null
    } catch (t: Throwable) { null }

    fun clear(ctx: Context) { try { file(ctx).delete() } catch (t: Throwable) {} }
}
