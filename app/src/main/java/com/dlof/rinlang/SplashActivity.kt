package com.dlof.rinlang

import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.animation.AlphaAnimation
import android.view.animation.AnimationSet
import android.view.animation.DecelerateInterpolator
import android.view.animation.ScaleAnimation
import androidx.appcompat.app.AppCompatActivity

/**
 * شاشة البداية (Splash Screen): أول شاشة تظهر عند فتح التطبيق (نقطة الدخول في
 * AndroidManifest بدلاً من MainActivity). تعرض شعار التطبيق واسمه للحظات قصيرة
 * بحركة دخول احترافية (تكبير خفيف + تلاشي) ثم تنتقل تلقائياً إلى المحرر الرئيسي.
 */
class SplashActivity : AppCompatActivity() {

    private val handler = Handler(Looper.getMainLooper())
    private val goToEditor = Runnable {
        if (isFinishing || isDestroyed) return@Runnable
        // إن كانت هذه حزمة APK ناتجة عن ميزة "تصدير APK" (تحتوي assets/rin_export_manifest.json)،
        // نفتح شاشة تشغيل المشروع مباشرة بدل المحرر — فتبدو الحزمة تطبيقاً مستقلاً بمشروعه.
        val target = if (ExportedRunActivity.hasExportedProject(this)) {
            ExportedRunActivity::class.java
        } else {
            MainActivity::class.java
        }
        startActivity(Intent(this, target))
        finish()
        overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_splash)

        // إن كانت هذه حزمة مُصدَّرة (assets/rin_export_manifest.json)، نُخصِّص عنوان/تعليق/مدة
        // شاشة البداية من بيانات التصدير بدل ترك "RinStudio" الثابتة تظهر داخل تطبيق المستخدم
        // نفسه بعد فتحه — الأيقونة تُخصَّص تلقائياً بالفعل عبر @mipmap/ic_launcher_round نفسها
        // (تُستبدَل بايتاتها وقت التصدير إن اختار المستخدم صورة، بلا حاجة لأي تعديل هنا).
        var splashDurationMs = SPLASH_DURATION_MS
        ExportedRunActivity.readManifestOrNull(this)?.let { manifest ->
            manifest.optString("display_name").takeIf { it.isNotBlank() }?.let { name ->
                findViewById<android.widget.TextView>(R.id.splashAppName).text = name
            }
            val tagline = manifest.optString("splash_tagline")
            val taglineView = findViewById<android.widget.TextView>(R.id.splashTagline)
            if (tagline.isNotBlank()) {
                taglineView.text = tagline
            } else {
                taglineView.visibility = android.view.View.GONE
            }
            val customDuration = manifest.optLong("splash_duration_ms", 0L)
            if (customDuration in 300L..5000L) splashDurationMs = customDuration
        }

        val card = findViewById<android.view.View>(R.id.splashIconFrame).parent as android.view.View

        // حركة دخول البطاقة: تكبير خفيف من 92% إلى 100% + تلاشي، بمنحنى تباطؤ ناعم
        val scale = ScaleAnimation(
            0.92f, 1f, 0.92f, 1f,
            android.view.animation.Animation.RELATIVE_TO_SELF, 0.5f,
            android.view.animation.Animation.RELATIVE_TO_SELF, 0.5f
        )
        val fade = AlphaAnimation(0f, 1f)
        val entrance = AnimationSet(true).apply {
            addAnimation(scale)
            addAnimation(fade)
            duration = 480
            interpolator = DecelerateInterpolator()
        }
        card.startAnimation(entrance)

        // تلاشي خلفية الصفحة كاملة بحركة أهدأ قليلاً لإحساس أكثر احترافية من ظهور مفاجئ
        val bgFade = AlphaAnimation(0f, 1f).apply { duration = 550 }
        findViewById<android.view.View>(android.R.id.content).startAnimation(bgFade)

        handler.postDelayed(goToEditor, splashDurationMs)
    }

    override fun onDestroy() {
        handler.removeCallbacks(goToEditor)
        super.onDestroy()
    }

    companion object {
        private const val SPLASH_DURATION_MS = 1300L
    }
}
