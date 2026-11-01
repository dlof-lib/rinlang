package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import kotlin.system.exitProcess

/**
 * نشاط شفاف (انظر Theme.RinLang.Transparent) لا يعرض أي واجهة خاصة به — مهمته الوحيدة عرض
 * [AlertDialog] فيه محتوى ملف اللوج كاملاً (كراش قاتل أو خطأ مُبلَّغ يدوياً عبر [ErrorReporter]).
 *
 * لماذا نشاط منفصل بدل عرض Dialog مباشرة من مكان الكراش؟ لأن الكراش قد يحدث في أي مكان
 * (أي Activity، أو حتى Thread ثانوي بلا نافذة أصلاً)، والطريقة الوحيدة الموثوقة لعرض واجهة
 * جديدة من [CrashHandler] هي إطلاق نشاط جديد بعلم NEW_TASK.
 */
class CrashReportActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val logPath = intent.getStringExtra(EXTRA_LOG_PATH)
        val isFatal = intent.getBooleanExtra(EXTRA_FATAL, false)
        val logText = (logPath?.let { java.io.File(it).takeIf { f -> f.isFile }?.readText() })
            ?: getString(R.string.crash_dialog_no_log)

        val scroll = ScrollView(this)
        val padding = (16 * resources.displayMetrics.density).toInt()
        val textView = TextView(this).apply {
            text = logText
            setTextIsSelectable(true)
            typeface = Typeface.MONOSPACE
            textSize = 12f
            setTextColor(ContextCompat.getColor(context, R.color.rin_console_text))
            setBackgroundColor(ContextCompat.getColor(context, R.color.rin_console_bg))
            setPadding(padding, padding, padding, padding)
        }
        scroll.addView(textView)

        val title = if (isFatal) getString(R.string.crash_dialog_title_fatal) else getString(R.string.crash_dialog_title_error)

        val builder = AlertDialog.Builder(this)
            .setTitle(title)
            .setView(scroll)
            .setCancelable(!isFatal)
            .setPositiveButton(R.string.crash_dialog_copy) { _, _ ->
                copyToClipboard(logText)
                Toast.makeText(this, R.string.crash_dialog_copied, Toast.LENGTH_SHORT).show()
            }

        if (isFatal) {
            // كراش قاتل: الزر الوحيد لإغلاق الحوار ينهي التطبيق فعلياً بدل الرجوع لشاشة
            // كانت في حالة غير مكتملة قد تسبب كراش آخر فوراً.
            builder.setNegativeButton(R.string.crash_dialog_close_app) { _, _ ->
                finishAffinity()
                Process.killProcess(Process.myPid())
                exitProcess(0)
            }
        } else {
            builder.setNegativeButton(R.string.crash_dialog_dismiss) { _, _ -> finish() }
        }

        val dialog = builder.create()
        dialog.setOnDismissListener { if (isFatal) finish() }
        dialog.show()
    }

    private fun copyToClipboard(text: String) {
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText("RinLang log", text))
    }

    companion object {
        private const val EXTRA_LOG_PATH = "log_path"
        private const val EXTRA_FATAL = "fatal"

        /** يُستدعى من [CrashHandler] بعد كتابة اللوج على كراش قاتل غير ممسوك. */
        fun launchFromCrash(context: Context, logPath: String) {
            val intent = Intent(context, CrashReportActivity::class.java).apply {
                putExtra(EXTRA_LOG_PATH, logPath)
                putExtra(EXTRA_FATAL, true)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP_MR1) {
                    addFlags(Intent.FLAG_ACTIVITY_MULTIPLE_TASK)
                }
            }
            context.startActivity(intent)
        }

        /** يُستدعى من [ErrorReporter] لعرض خطأ مُمسوك (غير قاتل) دون إنهاء التطبيق. */
        fun launchFromCaughtError(context: Context, logPath: String) {
            val intent = Intent(context, CrashReportActivity::class.java).apply {
                putExtra(EXTRA_LOG_PATH, logPath)
                putExtra(EXTRA_FATAL, false)
                if (context !is Activity) addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        }
    }
}
