package com.dlof.rinlang.network

import android.animation.ObjectAnimator
import android.content.Context
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import android.widget.FrameLayout
import androidx.core.content.ContextCompat
import com.dlof.rinlang.R

/**
 * طبقة تراكب كاملة الشاشة تعرض شاشة "لا يوجد اتصال بالإنترنت" الاحترافية، مع زر
 * إعادة محاولة. تُضاف تلقائياً فوق أي Activity ترث [BaseConnectivityActivity].
 */
class NoConnectionOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : FrameLayout(context, attrs) {

    private val btnRetry: View
    private val cardRoot: View

    init {
        LayoutInflater.from(context).inflate(R.layout.view_no_connection, this, true)
        setBackgroundColor(ContextCompat.getColor(context, R.color.rin_background))
        isClickable = true
        isFocusable = true
        elevation = 24f
        btnRetry = findViewById(R.id.btnNoConnectionRetry)
        cardRoot = findViewById(R.id.cardNoConnection)
    }

    fun setOnRetry(action: () -> Unit) {
        btnRetry.setOnClickListener { action() }
    }

    /** اهتزاز خفيف للبطاقة عند ضغط "إعادة المحاولة" والاتصال ما يزال مقطوعاً فعلياً. */
    fun shake() {
        cardRoot.animate().cancel()
        ObjectAnimator.ofFloat(cardRoot, "translationX", 0f, -16f, 16f, -10f, 10f, -4f, 4f, 0f).apply {
            duration = 380
            start()
        }
    }
}
