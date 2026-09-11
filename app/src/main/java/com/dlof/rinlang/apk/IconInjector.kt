package com.dlof.rinlang.apk

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import java.io.ByteArrayOutputStream

/**
 * يستبدل أيقونة الإطلاق داخل حزمة مُصدَّرة بصورة اختارها المستخدم — بلا لمس resources.arsc
 * أو AndroidManifest.xml إطلاقاً: أيقونات أندرويد (المسطّحة لـ API&lt;26 وadaptive لـ 26+)
 * كلها ملفات PNG عادية بأسماء ثابتة معروفة (`ic_launcher.png`, `ic_launcher_round.png`,
 * `ic_launcher_foreground.png` تحت `res/mipmap-<density>[*]/`) يُشار إليها من resources.arsc
 * بمعرّف مورد ثابت — استبدال *محتوى* نفس المُدخل بنفس الاسم في نفس المسار كافٍ تماماً
 * ليعرض النظام الأيقونة الجديدة، بصرف النظر عمّا تحمله resources.arsc من فهرسة.
 *
 * المطابقة بالبحث عن نمط اسم المُدخل الفعلي داخل الحزمة المضيفة وقت التصدير (بدل افتراض
 * مسار مُصرَّف ثابت مثل "res/mipmap-mdpi-v4/...") لأن لاحقة الإصدار التي يُضيفها aapt2 لكل
 * مجلد كثافة (v4/v26/...) تفصيل تنفيذ داخلي غير مضمون الثبات عبر إصدارات أدوات البناء.
 */
object IconInjector {

    private val DENSITY_PX = mapOf("mdpi" to 48, "hdpi" to 72, "xhdpi" to 96, "xxhdpi" to 144, "xxxhdpi" to 192)
    private val ADAPTIVE_DENSITY_PX = mapOf("mdpi" to 108, "hdpi" to 162, "xhdpi" to 216, "xxhdpi" to 324, "xxxhdpi" to 432)

    private val FLAT_ICON_REGEX =
        Regex("""res/mipmap-(mdpi|hdpi|xhdpi|xxhdpi|xxxhdpi)[^/]*/ic_launcher(_round)?\.png""", RegexOption.IGNORE_CASE)
    private val ADAPTIVE_FOREGROUND_REGEX =
        Regex("""res/mipmap-(mdpi|hdpi|xhdpi|xxhdpi|xxxhdpi)[^/]*/ic_launcher_foreground\.png""", RegexOption.IGNORE_CASE)

    /**
     * إن كان [entryName] أحد مسارات أيقونة الإطلاق القياسية، يُرجع بايتات PNG بديلة مبنية من
     * [icon] بالمقاس الصحيح لتلك الكثافة تحديداً؛ غير ذلك يُرجع null فلا يمسّ المُستدعي شيئاً.
     */
    fun replacementFor(entryName: String, icon: Bitmap): ByteArray? {
        ADAPTIVE_FOREGROUND_REGEX.find(entryName)?.let { m ->
            val px = ADAPTIVE_DENSITY_PX[m.groupValues[1].lowercase()] ?: return null
            return renderForeground(icon, px)
        }
        FLAT_ICON_REGEX.find(entryName)?.let { m ->
            val px = DENSITY_PX[m.groupValues[1].lowercase()] ?: return null
            val round = entryName.contains("_round", ignoreCase = true)
            return renderFlat(icon, px, round)
        }
        return null
    }

    private fun squareCropped(src: Bitmap): Bitmap {
        val size = minOf(src.width, src.height)
        if (size <= 0) return src
        val x = (src.width - size) / 2
        val y = (src.height - size) / 2
        return Bitmap.createBitmap(src, x, y, size, size)
    }

    private fun renderFlat(icon: Bitmap, sizePx: Int, round: Boolean): ByteArray {
        val square = squareCropped(icon)
        val out = Bitmap.createBitmap(sizePx, sizePx, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        if (round) {
            val path = Path()
            path.addCircle(sizePx / 2f, sizePx / 2f, sizePx / 2f, Path.Direction.CW)
            canvas.clipPath(path)
        }
        canvas.drawBitmap(square, null, RectF(0f, 0f, sizePx.toFloat(), sizePx.toFloat()), paint)
        return encodePng(out)
    }

    private fun renderForeground(icon: Bitmap, canvasPx: Int): ByteArray {
        // أدوات النظام تعرض من أيقونات adaptive منطقة آمنة ~66% من المنتصف فقط (الباقي قد
        // يُقصّ حسب شكل القناع: دائرة/مربع مُدوَّر/...)؛ نُصغّر صورة المستخدم داخل هذه المنطقة
        // بدل ملء اللوحة كاملة حتى لا تُقصّ تفاصيلها على بعض الأجهزة.
        val square = squareCropped(icon)
        val out = Bitmap.createBitmap(canvasPx, canvasPx, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        val safeZone = canvasPx * 0.66f
        val inset = (canvasPx - safeZone) / 2f
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        canvas.drawBitmap(square, null, RectF(inset, inset, inset + safeZone, inset + safeZone), paint)
        return encodePng(out)
    }

    private fun encodePng(bmp: Bitmap): ByteArray {
        val baos = ByteArrayOutputStream()
        bmp.compress(Bitmap.CompressFormat.PNG, 100, baos)
        return baos.toByteArray()
    }
}
