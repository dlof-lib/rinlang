package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.core.content.ContextCompat

/**
 * حوار بسيط غير قابل للإلغاء يعرض ثلاث مراحل نصية متتالية أثناء إنشاء مشروع جديد:
 * "جاري التحميل.." ثم "يتم التجهيز.." ثم "تم.."، تماماً كما يظهر عند إنشاء أي مشروع من
 * [ProjectsActivity]. العمل الفعلي (إنشاء الملفات عبر [ProjectManager.createProject]) يحدث
 * بين مرحلتَي "يتم التجهيز.." و"تم.."، فالمراحل النصية تعكس تقدماً حقيقياً لا تجميلاً فارغاً.
 */
class ProjectCreationProgressDialog(activity: Activity) {

    private val handler = Handler(Looper.getMainLooper())
    private val txtStage: TextView
    private val dialog: AlertDialog

    init {
        val dp = activity.resources.displayMetrics.density
        val container = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            gravity = android.view.Gravity.CENTER_HORIZONTAL
            setPadding((28 * dp).toInt(), (26 * dp).toInt(), (28 * dp).toInt(), (26 * dp).toInt())
        }

        val title = TextView(activity).apply {
            text = activity.getString(R.string.project_creating_title)
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
        }
        container.addView(title)

        val progressBar = ProgressBar(activity).apply {
            isIndeterminate = true
            indeterminateTintList = android.content.res.ColorStateList.valueOf(
                ContextCompat.getColor(context, R.color.rin_accent)
            )
        }
        container.addView(
            progressBar,
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                topMargin = (18 * dp).toInt()
                bottomMargin = (14 * dp).toInt()
            }
        )

        txtStage = TextView(activity).apply {
            text = activity.getString(R.string.project_stage_loading)
            textSize = 13.5f
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar_dim))
        }
        container.addView(txtStage)

        dialog = AlertDialog.Builder(activity)
            .setView(container)
            .setCancelable(false)
            .create()
    }

    fun show() {
        dialog.show()
    }

    /**
     * يشغّل مراحل "جاري التحميل.." و"يتم التجهيز.." بترتيب زمني قصير للإحساس بتقدّم حقيقي،
     * ثم ينفّذ [work] فعلياً (إنشاء ملفات المشروع)، ثم يعرض "تم.." لحظة قصيرة قبل استدعاء
     * [onDone] بنتيجة [work] (أو null لو فشل بـ IllegalArgumentException، مع تمرير الرسالة).
     */
    fun run(work: () -> Project, onDone: (project: Project?, errorMessage: String?) -> Unit) {
        show()
        txtStage.text = dialog.context.getString(R.string.project_stage_loading)
        handler.postDelayed({
            txtStage.text = dialog.context.getString(R.string.project_stage_preparing)
            handler.postDelayed({
                try {
                    val project = work()
                    txtStage.text = dialog.context.getString(R.string.project_stage_done)
                    handler.postDelayed({
                        dismiss()
                        onDone(project, null)
                    }, DONE_STAGE_DELAY_MS)
                } catch (e: IllegalArgumentException) {
                    dismiss()
                    onDone(null, e.message)
                }
            }, PREPARING_STAGE_DELAY_MS)
        }, LOADING_STAGE_DELAY_MS)
    }

    fun dismiss() {
        handler.removeCallbacksAndMessages(null)
        if (dialog.isShowing) dialog.dismiss()
    }

    private companion object {
        const val LOADING_STAGE_DELAY_MS = 350L
        const val PREPARING_STAGE_DELAY_MS = 350L
        const val DONE_STAGE_DELAY_MS = 300L
    }
}
