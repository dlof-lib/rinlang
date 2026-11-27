package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.widget.Toast
import java.io.FileNotFoundException
import java.nio.file.AccessDeniedException
import java.nio.file.FileAlreadyExistsException
import java.nio.file.NoSuchFileException

/** فشل عنصر واحد في عملية ملفات: الاسم + السبب المفهوم + الاستثناء الأصلي. */
class FileFailure(val name: String, val reason: String, val cause: Throwable? = null)

object FileErrors {

    /** يحوّل أي استثناء إلى جملة مفهومة بلغة المستخدم. */
    fun explain(ctx: Context, t: Throwable?): String {
        if (t == null) return ctx.getString(R.string.err_unknown)
        val msg = (t.message ?: "").lowercase()
        val id = when {
            t is FileAlreadyExistsException -> R.string.err_exists
            t is NoSuchFileException || t is FileNotFoundException && msg.contains("no such") -> R.string.err_not_found
            t is AccessDeniedException || msg.contains("permission denied") || msg.contains("eacces") || msg.contains("eperm") -> R.string.err_permission
            msg.contains("no space") || msg.contains("enospc") -> R.string.err_no_space
            msg.contains("read-only") || msg.contains("erofs") -> R.string.err_read_only
            msg.contains("name too long") || msg.contains("enametoolong") -> R.string.err_name_too_long
            msg.contains("not a directory") || msg.contains("enotdir") -> R.string.err_not_dir
            msg.contains("is a directory") || msg.contains("eisdir") -> R.string.err_is_dir
            msg.contains("directory not empty") || msg.contains("enotempty") -> R.string.err_not_empty
            msg.contains("exists") -> R.string.err_exists
            t is FileNotFoundException -> R.string.err_not_found
            t is OutOfMemoryError -> R.string.err_memory
            t is SecurityException -> R.string.err_permission
            t is java.util.zip.ZipException -> R.string.err_bad_zip
            t is java.io.IOException -> R.string.err_io
            else -> R.string.err_unknown
        }
        return ctx.getString(id)
    }

    fun failure(ctx: Context, name: String, t: Throwable?) = FileFailure(name, explain(ctx, t), t)

    /** نص كامل للنسخ/المشاركة مع المطوّر. */
    fun details(ctx: Context, title: String, failures: List<FileFailure>): String {
        val sb = StringBuilder(title).append('\n')
        failures.forEachIndexed { i, f ->
            sb.append(i + 1).append(". ").append(f.name).append(" — ").append(f.reason)
            f.cause?.let { sb.append("  [").append(it.javaClass.simpleName).append(": ").append(it.message ?: "").append(']') }
            sb.append('\n')
        }
        return sb.toString().trimEnd()
    }

    /**
     * يعرض تقرير الأخطاء: قائمة بالعناصر الفاشلة وأسبابها،
     * مع «إعادة المحاولة» (إن وُجدت) و«نسخ التفاصيل».
     */
    fun showReport(
        activity: Context,
        title: String,
        okCount: Int,
        failures: List<FileFailure>,
        onRetry: (() -> Unit)? = null,
    ) {
        if (failures.isEmpty()) return
        val shown = failures.take(40)
        val body = StringBuilder()
        body.append(activity.getString(R.string.err_summary_format, okCount, failures.size)).append("\n\n")
        shown.forEach { body.append("• ").append(it.name).append("\n   ").append(it.reason).append("\n") }
        if (failures.size > shown.size) body.append("\n").append(activity.getString(R.string.err_more_format, failures.size - shown.size))
        val b = AlertDialog.Builder(activity)
            .setTitle(title)
            .setMessage(body.toString().trimEnd())
            .setPositiveButton(R.string.err_close, null)
            .setNeutralButton(R.string.err_copy) { _, _ ->
                val cm = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("errors", details(activity, title, failures)))
                Toast.makeText(activity, R.string.err_copied, Toast.LENGTH_SHORT).show()
            }
        if (onRetry != null) b.setNegativeButton(R.string.err_retry) { _, _ -> onRetry() }
        b.show()
    }

    /** اختصار لخطأ عنصر واحد (إنشاء/حفظ…) مع زر إعادة المحاولة. */
    fun showSingle(activity: Context, title: String, name: String, t: Throwable?, onRetry: (() -> Unit)? = null) {
        showReport(activity, title, 0, listOf(failure(activity, name, t)), onRetry)
    }
}
