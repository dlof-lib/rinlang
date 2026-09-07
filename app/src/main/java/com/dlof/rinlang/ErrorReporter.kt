package com.dlof.rinlang

import android.content.Context

/**
 * نقطة استدعاء موحّدة لعرض حوار "لوج خطأ" لأي استثناء مُمسوك (caught) نريد إظهاره للمستخدم/
 * أثناء التطوير، دون أن يقتل التطبيق (بخلاف [CrashHandler] المخصص للكراشات القاتلة).
 *
 * مثال استخدام داخل أي catch:
 * ```
 * try { ... } catch (e: Exception) { ErrorReporter.report(context, e) }
 * ```
 */
object ErrorReporter {
    fun report(context: Context, throwable: Throwable, tag: String = "ERROR") {
        val file = CrashHandler.writeCrashLog(context, throwable, tag)
        CrashReportActivity.launchFromCaughtError(context, file.absolutePath)
    }
}
