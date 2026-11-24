package com.dlof.rinlang.notify

import android.annotation.SuppressLint
import android.app.Activity
import android.app.Application
import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.net.Uri
import android.os.Bundle
import android.os.SystemClock
import android.text.format.Formatter
import androidx.annotation.DrawableRes
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.core.text.BidiFormatter
import com.dlof.rinlang.JobStatus
import com.dlof.rinlang.R
import com.dlof.rinlang.RinJob
import com.dlof.rinlang.permissions.RinNotifications
import com.dlof.rinlang.permissions.RinPermissions
import java.text.NumberFormat
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/**
 * المنظّم الوحيد لإشعارات RinStudio: كل إشعار يمرّ من هنا كي يحمل الهوية نفسها (أيقونة أحادية اللون،
 * لون البنفسجي، أيقونة نوع الملف، أزرار واضحة، نسخة عامة لشاشة القفل) وتُحترم قناته.
 *
 *  - [beginDownload] ← [DownloadTicket]: إشعار تقدّم حقيقي (نسبة + حجم + مؤقّت) بزر «إلغاء»، لا يظهر للعمليات
 *    الخاطفة (مهلة [PROGRESS_GRACE_MS])، ثم [downloadDone] أو [downloadFailed] يستبدله.
 *  - [downloadDone]: اكتمال بأيقونة نوع الملف وزرَّي «فتح» و«مشاركة»، وتجميع تلقائي عند تعدد التنزيلات.
 *  - [fileSavedToProject]: اكتمال التنزيل من رابط إلى المشروع.
 *  - [jobFinished]: نتيجة تشغيل برنامج إن انتهى والتطبيق في الخلفية.
 *
 * كل الدوال آمنة من أي خيط، ولا تفعل شيئاً (دون استثناء) إن لم يُمنح إذن الإشعارات.
 */
object RinNotifier {

    const val ACTION_CANCEL = "com.dlof.rinlang.notify.ACTION_CANCEL"
    const val EXTRA_ID = "com.dlof.rinlang.notify.EXTRA_ID"

    private const val GROUP_DOWNLOADS = "com.dlof.rinlang.group.DOWNLOADS"
    private const val SUMMARY_ID_DOWNLOADS = 0x5100
    private const val JOB_ID_BASE = 0x7000
    private const val PROGRESS_INTERVAL_MS = 450L
    private const val PROGRESS_GRACE_MS = 700L
    private const val JOB_TIMEOUT_MS = 15 * 60 * 1000L
    private const val BACKGROUND_DEBOUNCE_MS = 1500L

    @Volatile private var appContext: Context? = null

    private val nextId = AtomicInteger(0x6000)
    private val cancelHooks = ConcurrentHashMap<Int, () -> Unit>()
    private val fileTiles = ConcurrentHashMap<Int, Bitmap>()

    // ---------------------------------------------------------------- الإقلاع وحالة الواجهة

    private val startedActivities = AtomicInteger(0)
    @Volatile private var lastBackgroundAt = 0L

    /** يُستدعى مرة من [com.dlof.rinlang.RinApplication.onCreate]. */
    fun install(app: Application) {
        appContext = app
        app.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
            override fun onActivityStarted(activity: Activity) { startedActivities.incrementAndGet() }
            override fun onActivityStopped(activity: Activity) {
                if (startedActivities.decrementAndGet() <= 0) {
                    startedActivities.set(0)
                    lastBackgroundAt = SystemClock.elapsedRealtime()
                }
            }
            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {}
            override fun onActivityResumed(activity: Activity) {}
            override fun onActivityPaused(activity: Activity) {}
            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}
            override fun onActivityDestroyed(activity: Activity) {}
        })
    }

    /** true إن كانت واجهة التطبيق ظاهرة (مع تسامح قصير لأن تدوير الشاشة يوقف النشاط لحظة). */
    fun isForeground(): Boolean =
        startedActivities.get() > 0 || SystemClock.elapsedRealtime() - lastBackgroundAt < BACKGROUND_DEBOUNCE_MS

    // ---------------------------------------------------------------- تنزيل: التقدّم

    /**
     * يبدأ تتبّع تنزيل/ضغط. [phase] سطر حالة صغير (مثل «جارٍ ضغط الملفات…»). إن مُرِّر [onCancel] ظهر زر «إلغاء»
     * في الإشعار ويُستدعى [onCancel] على الخيط الرئيسي عند الضغط عليه.
     */
    fun beginDownload(ctx: Context, name: String, phase: String? = null, onCancel: (() -> Unit)? = null): DownloadTicket {
        val app = ctx.applicationContext
        val id = nextId.getAndIncrement()
        if (onCancel != null) cancelHooks[id] = onCancel
        return DownloadTicket(app, id, name, phase, onCancel != null)
    }

    class DownloadTicket internal constructor(
        private val ctx: Context,
        internal val id: Int,
        private val name: String,
        private val phase: String?,
        private val cancelable: Boolean
    ) {
        private val startedWall = System.currentTimeMillis()
        private val startedElapsed = SystemClock.elapsedRealtime()
        private var lastPostElapsed = 0L
        private var finished = false

        /** تحديث التقدّم (آمن من أي خيط؛ يُخنق داخلياً فيمكن استدعاؤه لكل مقطع بيانات). */
        fun update(done: Long, total: Long) {
            val now = SystemClock.elapsedRealtime()
            if (now - startedElapsed < PROGRESS_GRACE_MS) return
            val complete = total > 0 && done >= total
            if (!complete && now - lastPostElapsed < PROGRESS_INTERVAL_MS) return
            synchronized(this) {
                if (finished) return
                lastPostElapsed = now
                val n = buildProgress(done, total)
                post(ctx, id, n)
            }
        }

        private fun buildProgress(done: Long, total: Long): Notification {
            val determinate = total > 0
            val pct = if (determinate) ((done * 100) / total).toInt().coerceIn(0, 100) else 0
            val doneStr = Formatter.formatShortFileSize(ctx, done)
            val text = if (determinate) {
                ctx.getString(
                    R.string.notif_dl_progress_text,
                    NumberFormat.getPercentInstance().format(pct / 100.0),
                    doneStr,
                    Formatter.formatShortFileSize(ctx, total)
                )
            } else if (done > 0) {
                ctx.getString(R.string.notif_dl_progress_unknown, doneStr)
            } else {
                ctx.getString(R.string.notif_dl_preparing)
            }
            val b = base(ctx, RinNotifications.CHANNEL_DOWNLOAD_PROGRESS, R.drawable.ic_notif_download)
                .setContentTitle(bidi(name))
                .setContentText(text)
                .setLargeIcon(fileTile(ctx, name))
                .setProgress(100, pct, !determinate)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setCategory(NotificationCompat.CATEGORY_PROGRESS)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setWhen(startedWall)
                .setShowWhen(true)
                .setUsesChronometer(true)
                .setContentIntent(appIntent(ctx, id))
            if (!phase.isNullOrBlank()) b.setSubText(phase)
            if (cancelable) {
                b.addAction(R.drawable.ic_notif_cancel, ctx.getString(R.string.cancel), cancelIntent(ctx, id))
            }
            return b.build()
        }

        internal fun finish() {
            synchronized(this) { finished = true }
            cancelHooks.remove(id)
            dismiss(ctx, id)
        }

        /** إنهاء بلا إشعار (إلغاء المستخدم نفسه). */
        fun cancel() = finish()
    }

    // ---------------------------------------------------------------- تنزيل: النتيجة

    /**
     * اكتمل حفظ ملف في مجلد التنزيلات العام. [subDir] المجلد الفرعي داخل التنزيلات (أو null)،
     * [sizeBytes] حجم الملف النهائي (≤ 0 إن كان مجهولاً).
     */
    fun downloadDone(
        ctx: Context,
        ticket: DownloadTicket?,
        uri: Uri,
        name: String,
        mime: String,
        sizeBytes: Long,
        subDir: String? = null
    ) {
        val app = ctx.applicationContext
        ticket?.finish()
        if (!canPost(app)) return
        val title = app.getString(R.string.dl_notif_title)
        val sizeStr = if (sizeBytes > 0) Formatter.formatShortFileSize(app, sizeBytes) else null
        val root = app.getString(R.string.notif_dl_folder_root)
        val where = app.getString(R.string.notif_dl_saved_in, if (subDir.isNullOrBlank()) root else "$root/$subDir")
        val detail = listOfNotNull(sizeStr, where).joinToString(" · ")
        val code = uri.hashCode()
        val id = code xor 0x3C00_0000

        val open = openIntent(app, uri, mime, code)
        val n = base(app, RinNotifications.CHANNEL_DOWNLOAD_DONE, R.drawable.ic_notif_done)
            .setContentTitle(title)
            .setContentText(bidi(name))
            .setSubText(sizeStr)
            .setLargeIcon(fileTile(app, name))
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .setBigContentTitle(title)
                    .bigText(bidi(name) + "\n" + detail)
            )
            .setContentIntent(open)
            .addAction(R.drawable.ic_notif_open, app.getString(R.string.dl_open), open)
            .addAction(R.drawable.ic_notif_share, app.getString(R.string.dl_share), shareIntent(app, uri, mime, name, code))
            .setAutoCancel(true)
            .setCategory(NotificationCompat.CATEGORY_STATUS)
            .setWhen(System.currentTimeMillis())
            .setShowWhen(true)
            .setGroup(GROUP_DOWNLOADS)
            .setGroupAlertBehavior(NotificationCompat.GROUP_ALERT_CHILDREN)
            .setPublicVersion(publicVersion(app, RinNotifications.CHANNEL_DOWNLOAD_DONE, R.drawable.ic_notif_done, title))
            .build()
        if (post(app, id, n)) postDownloadsSummary(app, id, name)
    }

    /** اكتمل تنزيل من رابط إلى داخل المشروع (لا Uri عام يُفتح؛ النقر يفتح التطبيق). */
    fun fileSavedToProject(ctx: Context, ticket: DownloadTicket?, name: String) {
        val app = ctx.applicationContext
        ticket?.finish()
        if (!canPost(app)) return
        val title = app.getString(R.string.notif_project_saved_title)
        val n = base(app, RinNotifications.CHANNEL_DOWNLOAD_DONE, R.drawable.ic_notif_done)
            .setContentTitle(title)
            .setContentText(bidi(name))
            .setLargeIcon(fileTile(app, name))
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .setBigContentTitle(title)
                    .bigText(bidi(name) + "\n" + app.getString(R.string.notif_project_saved_hint))
            )
            .setContentIntent(appIntent(app, name.hashCode()))
            .setAutoCancel(true)
            .setCategory(NotificationCompat.CATEGORY_STATUS)
            .setWhen(System.currentTimeMillis())
            .setShowWhen(true)
            .setPublicVersion(publicVersion(app, RinNotifications.CHANNEL_DOWNLOAD_DONE, R.drawable.ic_notif_done, title))
            .build()
        post(app, nextId.getAndIncrement(), n)
    }

    /** فشل تنزيل/حفظ: إشعار عالي الأهمية بسبب الفشل، النقر يفتح التطبيق. */
    fun downloadFailed(ctx: Context, ticket: DownloadTicket?, name: String, reason: String?) {
        val app = ctx.applicationContext
        ticket?.finish()
        if (!canPost(app)) return
        val title = app.getString(R.string.notif_dl_failed_title)
        val why = reason?.takeIf { it.isNotBlank() } ?: "?"
        val n = base(app, RinNotifications.CHANNEL_ALERTS, R.drawable.ic_notif_error)
            .setColor(ContextCompat.getColor(app, R.color.rin_btn_danger))
            .setContentTitle(title)
            .setContentText(bidi(name))
            .setSubText(why.take(60))
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .setBigContentTitle(title)
                    .bigText(bidi(name) + "\n" + why.take(300) + "\n" + app.getString(R.string.notif_dl_failed_hint))
            )
            .setContentIntent(appIntent(app, name.hashCode()))
            .setAutoCancel(true)
            .setCategory(NotificationCompat.CATEGORY_ERROR)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setWhen(System.currentTimeMillis())
            .setShowWhen(true)
            .setPublicVersion(publicVersion(app, RinNotifications.CHANNEL_ALERTS, R.drawable.ic_notif_error, title))
            .build()
        post(app, nextId.getAndIncrement(), n)
    }

    /** إشعار «ملخّص» يجمّع تنزيلات متعددة تحت عنوان واحد قابل للتوسيع (يظهر فقط حين يوجد تنزيلان فأكثر). */
    private fun postDownloadsSummary(ctx: Context, currentId: Int, currentName: String) {
        runCatching {
            val nm = ctx.getSystemService(NotificationManager::class.java) ?: return
            val lines = ArrayList<String>()
            lines += bidi(currentName)
            for (sbn in nm.activeNotifications) {
                if (sbn.id == currentId || sbn.id == SUMMARY_ID_DOWNLOADS) continue
                val n = sbn.notification
                if (n.group != GROUP_DOWNLOADS) continue
                if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) continue
                n.extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()?.let { if (it !in lines) lines += it }
            }
            if (lines.size < 2) return
            val title = ctx.resources.getQuantityString(R.plurals.notif_downloads_done, lines.size, lines.size)
            val style = NotificationCompat.InboxStyle().setBigContentTitle(title)
            lines.take(6).forEach { style.addLine(it) }
            if (lines.size > 6) style.setSummaryText("+${lines.size - 6}")
            val n = base(ctx, RinNotifications.CHANNEL_DOWNLOAD_DONE, R.drawable.ic_notif_done)
                .setContentTitle(title)
                .setContentText(lines.take(3).joinToString("، "))
                .setStyle(style)
                .setGroup(GROUP_DOWNLOADS)
                .setGroupSummary(true)
                .setGroupAlertBehavior(NotificationCompat.GROUP_ALERT_CHILDREN)
                .setAutoCancel(true)
                .setContentIntent(appIntent(ctx, SUMMARY_ID_DOWNLOADS))
                .build()
            post(ctx, SUMMARY_ID_DOWNLOADS, n)
        }
    }

    // ---------------------------------------------------------------- تشغيل البرامج

    /** نتيجة تشغيل منتهٍ — فقط إن كان التطبيق في الخلفية (في الواجهة يرى المستخدم النتيجة أمامه). */
    fun jobFinished(job: RinJob) {
        val app = appContext ?: return
        if (job.repl || isForeground() || !canPost(app)) return
        val status = job.status
        val (titleRes, icon, failed) = when (status) {
            JobStatus.SUCCESS -> Triple(R.string.notif_job_success_title, R.drawable.ic_notif_job, false)
            JobStatus.ERROR -> Triple(R.string.notif_job_error_title, R.drawable.ic_notif_error, true)
            JobStatus.TIMEOUT -> Triple(R.string.notif_job_timeout_title, R.drawable.ic_notif_error, true)
            else -> return
        }
        val title = app.getString(titleRes, job.number)
        val took = app.getString(R.string.notif_job_duration, formatDuration(app, job.durationMs()))
        val output = job.output.trim()
        val firstLine = output.lineSequence().map { it.trim() }.firstOrNull { it.isNotEmpty() }
        val preview = firstLine?.take(140) ?: app.getString(R.string.notif_job_no_output)
        val n = base(app, RinNotifications.CHANNEL_JOBS, icon)
            .apply { if (failed) setColor(ContextCompat.getColor(app, R.color.rin_btn_danger)) }
            .setContentTitle(title)
            .setContentText(preview)
            .setSubText(took)
            .setStyle(
                NotificationCompat.BigTextStyle()
                    .setBigContentTitle(title)
                    .bigText((if (output.isEmpty()) preview else output.take(500)) + "\n" + took)
            )
            .setContentIntent(appIntent(app, JOB_ID_BASE + job.number))
            .setAutoCancel(true)
            .setCategory(if (failed) NotificationCompat.CATEGORY_ERROR else NotificationCompat.CATEGORY_STATUS)
            .setWhen(if (job.finishedAt > 0) job.finishedAt else System.currentTimeMillis())
            .setShowWhen(true)
            .setTimeoutAfter(JOB_TIMEOUT_MS)
            .setPublicVersion(publicVersion(app, RinNotifications.CHANNEL_JOBS, icon, title))
            .build()
        post(app, JOB_ID_BASE + job.number, n)
    }

    private fun formatDuration(ctx: Context, ms: Long): String =
        if (ms < 1000) ctx.getString(R.string.notif_duration_ms, ms.coerceAtLeast(0))
        else ctx.getString(R.string.notif_duration_s, ms / 1000.0)

    // ---------------------------------------------------------------- الإلغاء من الإشعار

    /** يُستدعى من [RinNotificationReceiver] عند الضغط على «إلغاء». */
    internal fun handleCancel(ctx: Context, id: Int) {
        if (id == -1) return
        cancelHooks.remove(id)?.invoke()
        dismiss(ctx.applicationContext, id)
    }

    // ---------------------------------------------------------------- أدوات مشتركة

    private fun canPost(ctx: Context) = RinPermissions.notificationsGranted(ctx)

    @SuppressLint("MissingPermission") // الإذن مفحوص في canPost قبل كل نشر
    private fun post(ctx: Context, id: Int, n: Notification): Boolean {
        if (!canPost(ctx)) return false
        return runCatching {
            NotificationManagerCompat.from(ctx).notify(id, n)
            true
        }.getOrDefault(false)
    }

    private fun dismiss(ctx: Context, id: Int) {
        runCatching { NotificationManagerCompat.from(ctx).cancel(id) }
    }

    /** الهوية المشتركة: أيقونة أحادية اللون + لون التطبيق، بلا تلوين كامل الخلفية. */
    private fun base(ctx: Context, channel: String, @DrawableRes icon: Int): NotificationCompat.Builder =
        NotificationCompat.Builder(ctx, channel)
            .setSmallIcon(icon)
            .setColor(ContextCompat.getColor(ctx, R.color.rin_accent))
            .setColorized(false)
            .setVisibility(NotificationCompat.VISIBILITY_PRIVATE)

    /** النسخة التي تظهر على شاشة القفل (دون اسم الملف أو مخرجات البرنامج). */
    private fun publicVersion(ctx: Context, channel: String, @DrawableRes icon: Int, title: String): Notification =
        base(ctx, channel, icon).setContentTitle(title).setVisibility(NotificationCompat.VISIBILITY_PUBLIC).build()

    /** يمنع تبعثر أسماء الملفات اللاتينية داخل نص عربي (وبالعكس) بلفّها باتجاهها الصحيح. */
    private fun bidi(s: String): String = BidiFormatter.getInstance().unicodeWrap(s)

    private fun appIntent(ctx: Context, requestCode: Int): PendingIntent? {
        val launch = ctx.packageManager.getLaunchIntentForPackage(ctx.packageName) ?: return null
        return PendingIntent.getActivity(
            ctx, requestCode, launch, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
    }

    private fun cancelIntent(ctx: Context, id: Int): PendingIntent {
        val i = Intent(ctx, RinNotificationReceiver::class.java).setAction(ACTION_CANCEL).putExtra(EXTRA_ID, id)
        return PendingIntent.getBroadcast(ctx, id, i, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    }

    private fun openIntent(ctx: Context, uri: Uri, mime: String, requestCode: Int): PendingIntent {
        val i = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, mime)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        return PendingIntent.getActivity(ctx, requestCode, i, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    }

    private fun shareIntent(ctx: Context, uri: Uri, mime: String, name: String, requestCode: Int): PendingIntent {
        val send = Intent(Intent.ACTION_SEND).apply {
            type = mime
            putExtra(Intent.EXTRA_STREAM, uri)
            putExtra(Intent.EXTRA_SUBJECT, name)
            clipData = ClipData.newRawUri(name, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val chooser = Intent.createChooser(send, null).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_GRANT_READ_URI_PERMISSION)
        return PendingIntent.getActivity(ctx, requestCode + 1, chooser, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    }

    /** أيقونة كبيرة بحسب نوع الملف (نفس أيقونات مستكشف الملفات في التطبيق) كي يُعرف الملف من النظرة الأولى. */
    private fun fileTile(ctx: Context, name: String): Bitmap? {
        @DrawableRes val res = when (name.substringAfterLast('.', "").lowercase()) {
            "zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz" -> R.drawable.ic_lang_archive
            "rinpak" -> R.drawable.ic_rinpak_file
            "apk", "aab" -> R.drawable.ic_lang_apk
            "pdf" -> R.drawable.ic_lang_pdf
            "mp3", "wav", "ogg", "m4a", "flac", "aac" -> R.drawable.ic_lang_audio
            "json" -> R.drawable.ic_lang_json
            "md", "markdown" -> R.drawable.ic_lang_markdown
            "txt", "log" -> R.drawable.ic_lang_txt
            "html", "htm" -> R.drawable.ic_html_file
            "js", "mjs" -> R.drawable.ic_js_file
            "css" -> R.drawable.ic_css_file
            "indsin" -> R.drawable.ic_indsin_file
            else -> R.drawable.ic_lang_doc_generic
        }
        fileTiles[res]?.let { return it }
        return runCatching {
            val d = ContextCompat.getDrawable(ctx, res)
            if (d == null) {
                null
            } else {
                val px = (64 * ctx.resources.displayMetrics.density).toInt().coerceAtLeast(96)
                val bmp = Bitmap.createBitmap(px, px, Bitmap.Config.ARGB_8888)
                d.setBounds(0, 0, px, px)
                d.draw(Canvas(bmp))
                fileTiles[res] = bmp
                bmp
            }
        }.getOrNull()
    }
}
