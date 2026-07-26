package com.dlof.rinlang

import android.content.Context
import android.util.AttributeSet
import androidx.appcompat.widget.AppCompatEditText

/**
 * [android.widget.EditText] لا يعرض أي طريقة لمعرفة متى تحرّك المؤشر (بدون تغيير في النص)،
 * وهو ما نحتاجه لتفعيل ميزات مثل "تظليل القوس المطابق" و"تظليل السطر الحالي" التي يجب أن
 * تتحدّث بمجرد نقر المستخدم في مكان جديد وليس فقط عند الكتابة. لذلك نستخدم هذا الصنف الفرعي
 * البسيط الذي يكشف onSelectionChanged عبر lambda، بدل الاعتماد فقط على TextWatcher.
 */
class RinEditText @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : AppCompatEditText(context, attrs) {

    /** استدعاء عند أي تغيّر في موضع المؤشر أو التحديد: (start, end). */
    var onSelectionChangedListener: ((Int, Int) -> Unit)? = null

    override fun onSelectionChanged(selStart: Int, selEnd: Int) {
        super.onSelectionChanged(selStart, selEnd)
        onSelectionChangedListener?.invoke(selStart, selEnd)
    }
}
