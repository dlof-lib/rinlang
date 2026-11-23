package com.dlof.rinlang

import android.content.res.ColorStateList
import android.widget.TextView
import androidx.annotation.DrawableRes
import androidx.core.content.ContextCompat
import androidx.core.widget.TextViewCompat

/**
 * أيقونات vector (rs_ic_*) بدل الإيموجي والرموز النصية داخل [TextView].
 * الأيقونة تُرسَم بلون النص الحالي (أو [tint] إن مُرّر) وبالحجم المطلوب بالـ dp.
 */
object RsIcons {
    /** أيقونة في بداية النص (قبل النص في LTR، وعلى يمينه في RTL). */
    fun start(tv: TextView, @DrawableRes res: Int, sizeDp: Int = 16, gapDp: Int = 6, tint: Int? = null) {
        val d = ContextCompat.getDrawable(tv.context, res)?.mutate()
        val px = (sizeDp * tv.resources.displayMetrics.density).toInt()
        d?.setBounds(0, 0, px, px)
        tv.setCompoundDrawablesRelative(d, null, null, null)
        tv.compoundDrawablePadding = (gapDp * tv.resources.displayMetrics.density).toInt()
        TextViewCompat.setCompoundDrawableTintList(tv, ColorStateList.valueOf(tint ?: tv.currentTextColor))
    }

    /** أيقونة فقط (يُفرَّغ النص). */
    fun only(tv: TextView, @DrawableRes res: Int, sizeDp: Int = 18, tint: Int? = null) {
        tv.text = ""
        start(tv, res, sizeDp, 0, tint)
    }

    /** يزيل أي أيقونة مركّبة من [tv]. */
    fun clear(tv: TextView) = tv.setCompoundDrawablesRelative(null, null, null, null)
}
