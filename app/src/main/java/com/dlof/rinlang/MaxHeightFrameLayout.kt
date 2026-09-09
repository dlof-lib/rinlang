package com.dlof.rinlang

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.FrameLayout

/**
 * FrameLayout عادي يفرض حداً أقصى لارتفاعه المقاس (بالبكسل) عبر [maxHeightPx]. يُستخدَم لتطويق
 * RecyclerView عند تمريره إلى `AlertDialog.Builder().setView(...)`: خلافاً لِـ setItems() الجاهزة
 * التي تُقيَّد تلقائياً بارتفاع الشاشة، setView لا يفرض أي حدّ — فقائمة "بنية الملف" طويلة (ملف
 * فيه عشرات الحاويات) قد تحاول قياس كل صفوفها دفعة واحدة وتتجاوز الشاشة. RecyclerView الداخلي
 * نفسه يبقى قابلاً للتمرير بشكل طبيعي بعد هذا التطويق.
 */
class MaxHeightFrameLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : FrameLayout(context, attrs) {

    /** لا حدّ افتراضياً؛ يُضبط برمجياً بعد الـ inflate حسب ارتفاع الشاشة الفعلي. */
    var maxHeightPx: Int = Int.MAX_VALUE

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val cap = maxHeightPx
        val effectiveSpec = if (cap < Int.MAX_VALUE) {
            val mode = View.MeasureSpec.getMode(heightMeasureSpec)
            val size = View.MeasureSpec.getSize(heightMeasureSpec)
            if (mode == View.MeasureSpec.UNSPECIFIED || size > cap) {
                View.MeasureSpec.makeMeasureSpec(cap, View.MeasureSpec.AT_MOST)
            } else {
                heightMeasureSpec
            }
        } else {
            heightMeasureSpec
        }
        super.onMeasure(widthMeasureSpec, effectiveSpec)
    }
}
