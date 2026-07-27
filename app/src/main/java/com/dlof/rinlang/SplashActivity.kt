package com.dlof.rinlang

import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.animation.AlphaAnimation
import androidx.appcompat.app.AppCompatActivity

/**
 * شاشة البداية (Splash Screen): أول شاشة تظهر عند فتح التطبيق (نقطة الدخول في
 * AndroidManifest بدلاً من MainActivity). تعرض شعار التطبيق واسمه للحظات قصيرة
 * ثم تنتقل تلقائياً إلى المحرر الرئيسي.
 */
class SplashActivity : AppCompatActivity() {

    private val handler = Handler(Looper.getMainLooper())
    private val goToEditor = Runnable {
        if (isFinishing || isDestroyed) return@Runnable
        startActivity(Intent(this, MainActivity::class.java))
        finish()
        overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_splash)

        val fadeIn = AlphaAnimation(0f, 1f).apply { duration = 400 }
        findViewById<android.view.View>(android.R.id.content).startAnimation(fadeIn)

        handler.postDelayed(goToEditor, SPLASH_DURATION_MS)
    }

    override fun onDestroy() {
        handler.removeCallbacks(goToEditor)
        super.onDestroy()
    }

    companion object {
        private const val SPLASH_DURATION_MS = 1200L
    }
}
