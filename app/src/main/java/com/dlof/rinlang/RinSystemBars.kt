package com.dlof.rinlang

import android.app.Activity
import android.app.Application
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.PixelFormat
import android.graphics.drawable.Drawable
import android.os.Build
import android.os.Bundle
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import androidx.core.graphics.ColorUtils
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat

/**
 * يتعامل مع فرض edge-to-edge على Android 15+ (API 35+، ويشمل Android 16 / API 36).
 *
 * عند targetSdk >= 35 يرسم النظام التطبيق خلف شريط الحالة وشريط التنقل ولا يحترم
 * android:statusBarColor، ولا يُصغّر النافذة للوحة المفاتيح (adjustResize). بدل تعديل عشرات الـlayouts،
 * نُطبّق مرة واحدة على جذر المحتوى (android.R.id.content) حشواً يساوي insets النظام + لوحة المفاتيح،
 * ونرسم خلف الشريطين ألوان الثيم نفسها (statusBarColor / navigationBarColor) فيبقى المظهر كما كان.
 *
 * على Android 14 وأقدم (API 24–34) لا يفعل هذا الكلاس شيئاً: السلوك الأصلي للنظام يكفي.
 */
object RinSystemBars {

    fun install(app: Application) {
        if (Build.VERSION.SDK_INT < 35) return
        app.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
            override fun onActivityStarted(activity: Activity) = applyInsets(activity)
            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {}
            override fun onActivityResumed(activity: Activity) {}
            override fun onActivityPaused(activity: Activity) {}
            override fun onActivityStopped(activity: Activity) {}
            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}
            override fun onActivityDestroyed(activity: Activity) {}
        })
    }

    private fun applyInsets(activity: Activity) {
        val content = activity.findViewById<ViewGroup>(android.R.id.content) ?: return
        if (content.getTag(R.id.rin_insets_installed) == true) return
        content.setTag(R.id.rin_insets_installed, true)

        val statusColor = themeColor(activity, android.R.attr.statusBarColor)
        val navColor = themeColor(activity, android.R.attr.navigationBarColor) ?: statusColor
        val bgColor = themeColor(activity, android.R.attr.windowBackground)

        val bars = BarsDrawable(bgColor ?: 0, statusColor ?: bgColor ?: 0, navColor ?: bgColor ?: 0)
        content.background = bars

        // أيقونات شريط الحالة/التنقل: داكنة فوق خلفية فاتحة وفاتحة فوق خلفية داكنة.
        val controller = WindowCompat.getInsetsController(activity.window, activity.window.decorView)
        statusColor?.let { controller.isAppearanceLightStatusBars = ColorUtils.calculateLuminance(it) > 0.5 }
        navColor?.let { controller.isAppearanceLightNavigationBars = ColorUtils.calculateLuminance(it) > 0.5 }

        ViewCompat.setOnApplyWindowInsetsListener(content) { v, insets ->
            val sys = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()
            )
            val ime = insets.getInsets(WindowInsetsCompat.Type.ime())
            val bottom = maxOf(sys.bottom, ime.bottom)
            v.setPadding(sys.left, sys.top, sys.right, bottom)
            bars.update(sys.top, if (ime.bottom > sys.bottom) 0 else sys.bottom)
            WindowInsetsCompat.CONSUMED
        }
        ViewCompat.requestApplyInsets(content)
    }

    private fun themeColor(activity: Activity, attr: Int): Int? {
        val tv = TypedValue()
        if (!activity.theme.resolveAttribute(attr, tv, true)) return null
        return when {
            tv.type in TypedValue.TYPE_FIRST_COLOR_INT..TypedValue.TYPE_LAST_COLOR_INT -> tv.data
            tv.resourceId != 0 -> runCatching {
                androidx.core.content.ContextCompat.getColor(activity, tv.resourceId)
            }.getOrNull()
            else -> null
        }
    }

    /** يرسم خلفية النافذة ثم شريطاً علوياً/سفلياً بلون الثيم خلف أشرطة النظام. */
    private class BarsDrawable(
        private val bg: Int,
        private val top: Int,
        private val bottom: Int
    ) : Drawable() {
        private val paint = android.graphics.Paint()
        private var topInset = 0
        private var bottomInset = 0

        fun update(t: Int, b: Int) {
            if (t != topInset || b != bottomInset) {
                topInset = t; bottomInset = b; invalidateSelf()
            }
        }

        override fun draw(canvas: Canvas) {
            val r = bounds
            paint.color = bg; canvas.drawRect(r, paint)
            if (topInset > 0) {
                paint.color = top
                canvas.drawRect(r.left.toFloat(), r.top.toFloat(), r.right.toFloat(), (r.top + topInset).toFloat(), paint)
            }
            if (bottomInset > 0) {
                paint.color = bottom
                canvas.drawRect(r.left.toFloat(), (r.bottom - bottomInset).toFloat(), r.right.toFloat(), r.bottom.toFloat(), paint)
            }
        }

        override fun setAlpha(alpha: Int) { paint.alpha = alpha }
        override fun setColorFilter(colorFilter: ColorFilter?) { paint.colorFilter = colorFilter }
        @Deprecated("Deprecated in Java")
        override fun getOpacity(): Int = PixelFormat.OPAQUE
    }

    /** مفيد للشاشات التي تريد إعادة تطبيق الـ insets يدوياً بعد تغيير الـ layout. */
    fun requestApply(view: View) = ViewCompat.requestApplyInsets(view)
}
