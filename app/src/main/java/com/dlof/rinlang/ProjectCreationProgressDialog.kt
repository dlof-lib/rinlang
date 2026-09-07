package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.core.content.ContextCompat

/**
 * حوار غير قابل للإلغاء يعرض "مُدرَّج" (stepper) احترافياً بثلاث مراحل متتالية أثناء إنشاء
 * مشروع جديد: "جاري التحميل.." ثم "يتم التجهيز.." ثم "تم.."، تماماً كما يظهر عند إنشاء أي
 * مشروع من [ProjectsActivity]. كل مرحلة دائرة برقم/دوّار تحميل/علامة صح حسب حالتها
 * (قادمة/نشطة/مكتملة)، بدل نص وحيد متغيّر كما كان سابقاً. العمل الفعلي (إنشاء الملفات عبر
 * [ProjectManager.createProject]) يحدث بين مرحلتَي "يتم التجهيز.." و"تم.."، فالمراحل تعكس
 * تقدماً حقيقياً لا تجميلاً فارغاً.
 */
class ProjectCreationProgressDialog(activity: Activity) {

    private val handler = Handler(Looper.getMainLooper())
    private val dialog: AlertDialog
    private val steps: List<StepRow>

    private class StepRow(
        val circle: FrameLayout,
        val progress: ProgressBar,
        val check: ImageView,
        val label: TextView
    )

    init {
        val dp = activity.resources.displayMetrics.density
        fun px(v: Int) = (v * dp).toInt()

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(26), px(24), px(26), px(24))
        }

        val title = TextView(activity).apply {
            text = activity.getString(R.string.project_creating_title)
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
        }
        root.addView(title)

        val stageLabels = listOf(
            activity.getString(R.string.project_stage_loading),
            activity.getString(R.string.project_stage_preparing),
            activity.getString(R.string.project_stage_done)
        )

        val builtSteps = mutableListOf<StepRow>()
        stageLabels.forEachIndexed { index, label ->
            val row = LinearLayout(activity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val rowParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = px(if (index == 0) 20 else 4) }

            val circleSize = px(24)
            val circle = FrameLayout(activity).apply {
                layoutParams = LinearLayout.LayoutParams(circleSize, circleSize)
                setBackgroundResource(R.drawable.bg_step_circle_pending)
            }
            val progress = ProgressBar(activity).apply {
                isIndeterminate = true
                indeterminateTintList = android.content.res.ColorStateList.valueOf(
                    ContextCompat.getColor(context, R.color.rin_accent)
                )
                visibility = android.view.View.GONE
                layoutParams = FrameLayout.LayoutParams(px(14), px(14), Gravity.CENTER)
            }
            val check = ImageView(activity).apply {
                setImageResource(R.drawable.ic_check_white)
                visibility = android.view.View.GONE
                layoutParams = FrameLayout.LayoutParams(px(12), px(12), Gravity.CENTER)
            }
            circle.addView(progress)
            circle.addView(check)
            row.addView(circle)

            val labelView = TextView(activity).apply {
                text = label
                textSize = 13.5f
                setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar_dim))
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { marginStart = px(12) }
            }
            row.addView(labelView)

            root.addView(row, rowParams)
            builtSteps.add(StepRow(circle, progress, check, labelView))
        }
        steps = builtSteps

        dialog = AlertDialog.Builder(activity)
            .setView(root)
            .setCancelable(false)
            .create()
    }

    fun show() {
        dialog.show()
    }

    /** ينشّط مرحلة [index]: دائرة بنفسجية بدوّار تحميل + نص مضيء. */
    private fun activateStep(index: Int) {
        val step = steps[index]
        step.circle.setBackgroundResource(R.drawable.bg_step_circle_active)
        step.progress.visibility = android.view.View.VISIBLE
        step.check.visibility = android.view.View.GONE
        step.label.setTextColor(ContextCompat.getColor(step.label.context, R.color.rin_on_toolbar))
    }

    /** يكمل مرحلة [index]: دائرة خضراء بعلامة صح، تمهيداً للمرحلة التالية. */
    private fun completeStep(index: Int) {
        val step = steps[index]
        step.circle.setBackgroundResource(R.drawable.bg_step_circle_done)
        step.progress.visibility = android.view.View.GONE
        step.check.visibility = android.view.View.VISIBLE
    }

    /**
     * يشغّل مراحل "جاري التحميل.." و"يتم التجهيز.." بترتيب زمني قصير للإحساس بتقدّم حقيقي
     * عبر المُدرَّج (كل مرحلة تُنشَّط ثم تُستكمَل قبل الانتقال للتالية)، ثم ينفّذ [work] فعلياً
     * (إنشاء ملفات المشروع)، ثم يستكمل مرحلة "تم.." لحظة قصيرة قبل استدعاء [onDone] بنتيجة
     * [work] (أو null لو فشل بـ IllegalArgumentException، مع تمرير الرسالة).
     */
    fun run(work: () -> Project, onDone: (project: Project?, errorMessage: String?) -> Unit) {
        show()
        activateStep(0)
        handler.postDelayed({
            completeStep(0)
            activateStep(1)
            handler.postDelayed({
                try {
                    val project = work()
                    completeStep(1)
                    activateStep(2)
                    handler.postDelayed({
                        completeStep(2)
                        handler.postDelayed({
                            dismiss()
                            onDone(project, null)
                        }, DONE_STAGE_HOLD_MS)
                    }, DONE_STAGE_DELAY_MS)
                } catch (e: IllegalArgumentException) {
                    dismiss()
                    onDone(null, e.message)
                } catch (e: Exception) {
                    // أي خطأ آخر غير متوقع (مساحة تخزين ممتلئة، فشل كتابة ملف، إلخ) كان
                    // يهرب من هنا فيتسبب بإغلاق التطبيق بالكامل بدل عرض رسالة. الآن يُعامَل
                    // كفشل عادي في الإنشاء بدل كروت (Crash).
                    dismiss()
                    onDone(null, e.message ?: e.javaClass.simpleName)
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
        const val DONE_STAGE_DELAY_MS = 250L
        const val DONE_STAGE_HOLD_MS = 300L
    }
}
