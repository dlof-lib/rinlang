package com.dlof.rinlang

import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowInsetsControllerCompat

/**
 * شاشة البداية (Splash Screen): أول شاشة تظهر عند فتح التطبيق (نقطة الدخول في
 * AndroidManifest بدلاً من MainActivity). يرسم [RinSplashView] قنديل RinStudio تدريجياً على
 * خلفية سوداء (أسود · أبيض · أخضر) ثم تنتقل تلقائياً إلى المحرر الرئيسي.
 */
class SplashActivity : RinBaseActivity() {

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

        // أيقونات شريطَي الحالة/التنقّل بيضاء فوق الخلفية السوداء.
        WindowInsetsControllerCompat(window, window.decorView).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }

        val splash = findViewById<RinSplashView>(R.id.splashView)
        splash.appName = getString(R.string.app_name)
        splash.tagline = getString(R.string.splash_tagline)

        // إن كانت هذه حزمة مُصدَّرة (assets/rin_export_manifest.json)، نُخصِّص عنوان/تعليق/مدة
        // شاشة البداية من بيانات التصدير بدل ترك "RinStudio" الثابتة تظهر داخل تطبيق المستخدم.
        var splashDurationMs = SPLASH_DURATION_MS
        ExportedRunActivity.readManifestOrNull(this)?.let { manifest ->
            manifest.optString("display_name").takeIf { it.isNotBlank() }?.let { splash.appName = it }
            splash.tagline = manifest.optString("splash_tagline").takeIf { it.isNotBlank() }
            val customDuration = manifest.optLong("splash_duration_ms", 0L)
            if (customDuration in 300L..5000L) splashDurationMs = customDuration
        }

        splash.durationMs = splashDurationMs
        handler.postDelayed(goToEditor, splashDurationMs)
    }

    override fun onDestroy() {
        handler.removeCallbacks(goToEditor)
        super.onDestroy()
    }

    companion object {
        /** أطول قليلاً من القديمة (1300ms) كي يكتمل رسم القنديل ونبضته الأولى قبل الانتقال. */
        private const val SPLASH_DURATION_MS = 2200L
    }
}
