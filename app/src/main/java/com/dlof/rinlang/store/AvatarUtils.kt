package com.dlof.rinlang.store

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.util.Base64
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import java.io.ByteArrayOutputStream

/**
 * كل منطق عرض الصور الدائرية (صور الملف الشخصي، وأيقونات الحزم) في مكان واحد.
 *
 * المشكلة التي يحلّها هذا الملف: كانت كل شاشة (AccountActivity، PackageDetailActivity،
 * PublicProfileActivity، UserListActivity) تكرّر نفس الكود لعرض صورة دائرية عبر
 * `ImageView.outlineProvider` + `clipToOutline`. هذا الأسلوب يعتمد على قياسات الـ View
 * (width/height) وقد يُحسَب مرة واحدة قبل اكتمال تخطيط الشاشة (خصوصاً عند استخدام صور غير
 * مربّعة الشكل مثل صور مكتبة الأفاتار الجاهزة)، فتظهر الصورة أحياناً مقطوعة (نصفها فقط
 * ظاهر فوق الخلفية الدائرية بدل الصورة كاملة) بدل أن تُقصّ بدقة كدائرة كاملة.
 *
 * الحل هنا: بدل الاعتماد على "قصّ الـ View" وقت الرسم، نُنتج نحن أنفسنا Bitmap دائرياً
 * جاهزاً (مربّع القص من المنتصف + قناع دائري بواسطة BitmapShader) قبل تمريره لـ setImageBitmap
 * — بحيث تكون الصورة دائرية دائماً ومضمونة الظهور الكامل، بصرف النظر عن توقيت تخطيط الشاشة
 * أو نسبة أبعاد الصورة الأصلية.
 */
object AvatarUtils {

    /** أقصى بُعد (بالبكسل) لأي صورة ملف شخصي تُرفَع، لتفادي حزم Firebase الضخمة. */
    const val AVATAR_MAX_DIMENSION = 512
    private const val AVATAR_JPEG_QUALITY = 85

    /**
     * يعرض صورة ملف شخصي دائرية داخل [imageView]، أو يعرض حرف الاسم الأول ضمن [initialView]
     * إن لم توجد صورة (أو تعذّر فكّها). هذه هي الدالة الوحيدة التي يجب أن تستدعيها كل الشاشات
     * بدل تكرار نفس منطق فكّ base64 + القصّ الدائري.
     */
    fun renderAvatar(imageView: ImageView, initialView: TextView, avatarBase64: String?, displayName: String) {
        val circularBitmap = decodeCircularAvatar(avatarBase64)
        if (circularBitmap == null) {
            imageView.setImageDrawable(null)
            initialView.text = displayName.take(1).uppercase()
            initialView.visibility = View.VISIBLE
        } else {
            imageView.scaleType = ImageView.ScaleType.FIT_CENTER
            imageView.setImageBitmap(circularBitmap)
            initialView.visibility = View.GONE
        }
    }

    /**
     * يفكّ [base64] إلى Bitmap ثم يقصّه مربّعاً من المنتصف ويقنّعه دائرياً، جاهزاً للعرض
     * المباشر عبر setImageBitmap بلا أي حاجة لقصّ إضافي على مستوى الـ View. يُرجع null إن كان
     * [base64] فارغاً أو تعذّر فكّه.
     */
    fun decodeCircularAvatar(base64: String?): Bitmap? {
        if (base64.isNullOrBlank()) return null
        return try {
            val bytes = Base64.decode(base64, Base64.NO_WRAP)
            val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return null
            toCircularBitmap(cropToSquare(bitmap))
        } catch (t: Throwable) {
            null
        }
    }

    /** يقصّ [source] مربّعاً من منتصفه (يُلغي أي جزء زائد من البُعد الأطول)، محافظاً على النسبة. */
    fun cropToSquare(source: Bitmap): Bitmap {
        val size = minOf(source.width, source.height)
        if (source.width == source.height) return source
        val x = (source.width - size) / 2
        val y = (source.height - size) / 2
        return Bitmap.createBitmap(source, x, y, size, size)
    }

    /**
     * يرسم [source] (يُفترض أنه مربّع مسبقاً) داخل Bitmap جديد مقنَّع بدائرة كاملة عبر
     * BitmapShader + قناع Xfermode، بحواف ناعمة (antialiased). الزوايا خارج الدائرة تصبح
     * شفافة تماماً بدل مقصوصة بشكل خشن.
     */
    fun toCircularBitmap(source: Bitmap): Bitmap {
        val size = minOf(source.width, source.height)
        val output = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(output)

        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        val shader = BitmapShader(source, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
        paint.shader = shader

        // رسم بيضاوي (دائرة هنا لأن العرض = الارتفاع) بتعبئة BitmapShader يرسم فقط الأجزاء
        // الواقعة داخل الدائرة من الصورة المصدر — الزوايا الأربع خارج الدائرة تبقى شفافة
        // تماماً على قماش ARGB_8888 الفارغ، بلا حاجة لأي قناع Xfermode إضافي.
        val rect = RectF(0f, 0f, size.toFloat(), size.toFloat())
        canvas.drawOval(rect, paint)

        return output
    }

    /**
     * قصّ إضافي على مستوى الـ View (خط دفاع ثانٍ فقط، غير ضروري بعد استخدام [decodeCircularAvatar]
     * لأن الصورة نفسها أصبحت دائرية مسبقاً، لكن يُبقي الحواف ناعمة إن كانت الخلفية/الإطار خلف
     * الصورة مربّعة الشكل).
     */
    fun clipToCircle(view: ImageView) {
        view.clipToOutline = true
        view.outlineProvider = object : android.view.ViewOutlineProvider() {
            override fun getOutline(v: View, outline: android.graphics.Outline) {
                if (v.width > 0 && v.height > 0) {
                    outline.setOval(0, 0, v.width, v.height)
                }
            }
        }
        // يعيد حساب المخطط الخارجي فور اكتمال تخطيط الـ View (بدل الاعتماد فقط على استدعاء
        // مبكر قد يحدث قبل أن تُعرف أبعاد الـ View الحقيقية).
        if (view.isLaidOut) {
            view.invalidateOutline()
        } else {
            view.addOnLayoutChangeListener(object : View.OnLayoutChangeListener {
                override fun onLayoutChange(
                    v: View, l: Int, t: Int, r: Int, b: Int,
                    oL: Int, oT: Int, oR: Int, oB: Int
                ) {
                    v.invalidateOutline()
                    v.removeOnLayoutChangeListener(this)
                }
            })
        }
    }

    /** يصغّر [source] بحيث لا يتجاوز أي بُعد [maxDimension]، مع الحفاظ على النسبة. */
    fun resizeBitmap(source: Bitmap, maxDimension: Int): Bitmap {
        val width = source.width
        val height = source.height
        if (width <= maxDimension && height <= maxDimension) return source
        val scale = maxDimension.toFloat() / maxOf(width, height)
        val newWidth = (width * scale).toInt().coerceAtLeast(1)
        val newHeight = (height * scale).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(source, newWidth, newHeight, true)
    }

    /** يصغّر [source] ثم يضغطه JPEG ويرمّزه base64 — مسار مشترك للرفع (صورة شخصية أو أيقونة حزمة). */
    fun resizeAndEncodeToBase64(source: Bitmap, maxDimension: Int = AVATAR_MAX_DIMENSION): String {
        val resized = resizeBitmap(source, maxDimension)
        val outputStream = ByteArrayOutputStream()
        resized.compress(Bitmap.CompressFormat.JPEG, AVATAR_JPEG_QUALITY, outputStream)
        return Base64.encodeToString(outputStream.toByteArray(), Base64.NO_WRAP)
    }
}
