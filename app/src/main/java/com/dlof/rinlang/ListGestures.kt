package com.dlof.rinlang

import android.annotation.SuppressLint
import android.content.Context
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.RecyclerView

/**
 * إيماءات مشتركة لقوائم الألبومات ومحتوى الألبوم (نفس سلوك شاشتي المشاريع والملفات):
 *  - سحب البطاقة لليمين = فتح.
 *  - سحب البطاقة لليسار = إجراء ثانٍ اختياري (مثل الحذف، يؤكَّد داخل الإجراء نفسه).
 *  - نقرتان على العنوان = أول القائمة، ضغطة مستمرة = آخرها.
 */
object ListGestures {

    fun attachOpenSwipe(
        ctx: Context,
        rv: RecyclerView,
        onOpen: (position: Int) -> Unit,
        onSecondary: ((position: Int) -> Unit)? = null,
        primaryIcon: Int = R.drawable.ic_open,
        primaryLabel: String = ctx.getString(R.string.swipe_open_label),
        cornerDp: Float = 24f
    ) {
        val open = RowSwipeCallback.Action(
            primaryIcon, ContextCompat.getColor(ctx, R.color.rin_accent), primaryLabel
        ) { pos -> onOpen(pos) }
        val second = onSecondary?.let {
            RowSwipeCallback.Action(
                R.drawable.ic_trash_red, 0xFFE5484D.toInt(), ctx.getString(R.string.swipe_delete_label)
            ) { pos -> it(pos) }
        }
        ItemTouchHelper(
            RowSwipeCallback(ctx = ctx, model = SelectionModel(), rightAction = open, leftAction = second, cornerDp = cornerDp)
        ).attachToRecyclerView(rv)
    }

    @SuppressLint("ClickableViewAccessibility")
    fun attachTitleScroll(ctx: Context, title: View, rv: RecyclerView) {
        val detector = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean {
                if ((rv.adapter?.itemCount ?: 0) > 0) rv.smoothScrollToPosition(0)
                return true
            }
            override fun onLongPress(e: MotionEvent) {
                val n = rv.adapter?.itemCount ?: 0
                if (n > 0) rv.smoothScrollToPosition(n - 1)
            }
        })
        title.setOnTouchListener { _, ev -> detector.onTouchEvent(ev); true }
    }

    /** سحب لأسفل من أعلى القائمة = تحديث. */
    fun attachPullRefresh(ctx: Context, layout: androidx.swiperefreshlayout.widget.SwipeRefreshLayout, refresh: () -> Unit) {
        layout.setColorSchemeColors(ContextCompat.getColor(ctx, R.color.rin_accent))
        layout.setOnRefreshListener { refresh(); layout.isRefreshing = false }
    }
}
