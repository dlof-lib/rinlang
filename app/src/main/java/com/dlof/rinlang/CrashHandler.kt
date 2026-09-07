package com.dlof.rinlang

import android.content.Context
import android.os.Build
import android.os.Process
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.system.exitProcess

/**
 * يلتقط أي استثناء غير ممسوك (Uncaught Exception) في أي Thread من التطبيق، يكتب "لوج" كامل
 * (تاريخ/معلومات الجهاز والنسخة/الـ stack trace الكامل) إلى ملف داخل [Context.filesDir]،
 * ثم يفتح [CrashReportActivity] لعرضه في Dialog بدل أن يختفي التطبيق فجأة بلا أي تفسير.
 *
 * يُثبَّت مرة واحدة من [RinApplication.onCreate] عبر [install]. يلتف حول أي
 * defaultUncaughtExceptionHandler كان موجوداً سلفاً (من النظام أو مكتبة أخرى مثل Firebase)
 * ويستدعيه بعد كتابة اللوج وفتح الحوار، حتى لا نكسر أي تسجيل كراشات خارجي موجود مسبقاً.
 */
object CrashHandler {

    private const val CRASH_DIR = "crash_logs"
    private const val CRASH_FILE = "last_crash.txt"
    private const val TAG_FATAL = "FATAL"

    fun install(context: Context) {
        val appContext = context.applicationContext
        val previousHandler = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val file = writeCrashLog(appContext, throwable, TAG_FATAL, thread.name)
                CrashReportActivity.launchFromCrash(appContext, file.absolutePath)
            } catch (_: Throwable) {
                // لا نسمح لخطأ داخل معالج الكراش نفسه بمنع إغلاق العملية بشكل نظيف.
            }
            // نمهل النظام لحظة قصيرة كي يُطلَق CrashReportActivity فعلياً قبل قتل العملية،
            // بدل الاعتماد فقط على استدعاء previousHandler الذي قد يقتل العملية فوراً.
            Thread.sleep(200)
            if (previousHandler != null) {
                previousHandler.uncaughtException(thread, throwable)
            } else {
                Process.killProcess(Process.myPid())
                exitProcess(10)
            }
        }
    }

    /**
     * يكتب لوج خطأ يدوياً (ليس بالضرورة كراش قاتل — قد يكون استثناء مُمسوك داخل try/catch
     * لكن نريد عرضه للمستخدم/المطوّر) إلى نفس ملف [CRASH_FILE]، ثم يرجع الملف الناتج.
     * تستخدمه [ErrorReporter] لعرض حوار خطأ فوري دون قتل التطبيق.
     */
    fun writeCrashLog(context: Context, throwable: Throwable, tag: String, threadName: String = "main"): File {
        val dir = File(context.filesDir, CRASH_DIR).apply { mkdirs() }
        val file = File(dir, CRASH_FILE)
        file.writeText(buildLogText(context, throwable, tag, threadName))
        return file
    }

    fun readLastCrashLog(context: Context): String? {
        val file = File(File(context.filesDir, CRASH_DIR), CRASH_FILE)
        return if (file.isFile) file.readText() else null
    }

    private fun buildLogText(context: Context, throwable: Throwable, tag: String, threadName: String): String {
        val sw = StringWriter()
        throwable.printStackTrace(PrintWriter(sw))
        val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
        val versionLabel = try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            val code = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) info.longVersionCode else @Suppress("DEPRECATION") info.versionCode.toLong()
            "${info.versionName} ($code)"
        } catch (_: Exception) {
            "?"
        }
        return buildString {
            appendLine("=== RinLang $tag ===")
            appendLine("الوقت: $timestamp")
            appendLine("Thread: $threadName")
            appendLine("الجهاز: ${Build.MANUFACTURER} ${Build.MODEL} (Android ${Build.VERSION.RELEASE}, API ${Build.VERSION.SDK_INT})")
            appendLine("إصدار التطبيق: $versionLabel")
            appendLine()
            append(sw.toString())
        }
    }
}
