package com.dlof.rinlang.network

import android.net.ConnectivityManager
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity

/**
 * Activity أساسية لأي شاشة تعتمد على الإنترنت (تسجيل دخول، متجر، حساب، نشر...).
 *
 * تُضيف تلقائياً بعد setContentView طبقة [NoConnectionOverlayView] تغطي الشاشة كاملة،
 * وتراقب حالة الاتصال طوال بقاء الشاشة ظاهرة (onStart..onStop):
 * - إن انقطع الاتصال أثناء وجود المستخدم في الشاشة → تظهر شاشة الانقطاع تلقائياً.
 * - إن عاد الاتصال → تختفي الشاشة تلقائياً ويُستدعى [onConnectionRestored] لإعادة تحميل البيانات.
 *
 * لا حاجة لأي تعديل في XML أو في onCreate الحالي؛ يكفي وراثة هذا الصف بدل AppCompatActivity.
 */
abstract class BaseConnectivityActivity : AppCompatActivity() {

    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var overlay: NoConnectionOverlayView? = null
    private var isShowingOffline = false

    /** يُستدعى تلقائياً عند عودة الاتصال بعد انقطاعه أثناء وجود المستخدم في هذه الشاشة. */
    protected open fun onConnectionRestored() {}

    /** الإجراء الذي ينفّذه زر "إعادة المحاولة". افتراضياً نفس [onConnectionRestored]. */
    protected open fun onRetryClicked() {
        onConnectionRestored()
    }

    override fun setContentView(layoutResID: Int) {
        super.setContentView(layoutResID)
        attachOverlay()
    }

    override fun setContentView(view: View?) {
        super.setContentView(view)
        attachOverlay()
    }

    override fun setContentView(view: View?, params: ViewGroup.LayoutParams?) {
        super.setContentView(view, params)
        attachOverlay()
    }

    private fun attachOverlay() {
        if (overlay != null) return
        val root = findViewById<ViewGroup>(android.R.id.content) ?: return
        val overlayView = NoConnectionOverlayView(this).apply {
            visibility = View.GONE
            setOnRetry {
                if (NetworkMonitor.isOnline(this@BaseConnectivityActivity)) {
                    hideOfflineOverlay()
                    onRetryClicked()
                } else {
                    shake()
                }
            }
        }
        root.addView(
            overlayView,
            ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        )
        overlay = overlayView
        if (!NetworkMonitor.isOnline(this)) showOfflineOverlay()
    }

    /** هل يوجد اتصال إنترنت صالح الآن؟ */
    protected fun isOnline(): Boolean = NetworkMonitor.isOnline(this)

    /** ينفّذ [action] فقط إن كان هناك اتصال، وإلا يعرض شاشة الانقطاع مباشرة بدل محاولة فاشلة صامتة. */
    protected fun runIfOnline(action: () -> Unit) {
        if (NetworkMonitor.isOnline(this)) {
            action()
        } else {
            showOfflineOverlay()
        }
    }

    protected fun showOfflineOverlay() {
        isShowingOffline = true
        overlay?.visibility = View.VISIBLE
    }

    protected fun hideOfflineOverlay() {
        isShowingOffline = false
        overlay?.visibility = View.GONE
    }

    override fun onStart() {
        super.onStart()
        networkCallback = NetworkMonitor.register(
            context = this,
            onAvailable = {
                if (isShowingOffline) {
                    hideOfflineOverlay()
                    onConnectionRestored()
                }
            },
            onLost = { showOfflineOverlay() }
        )
    }

    override fun onStop() {
        super.onStop()
        networkCallback?.let { NetworkMonitor.unregister(this, it) }
        networkCallback = null
    }
}
