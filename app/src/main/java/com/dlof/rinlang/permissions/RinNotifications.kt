package com.dlof.rinlang.permissions

import android.app.NotificationChannel
import android.app.NotificationChannelGroup
import android.app.NotificationManager
import android.content.Context
import android.media.AudioAttributes
import android.media.RingtoneManager
import com.dlof.rinlang.R

/**
 * قنوات إشعارات التطبيق، مجمّعة في إعدادات النظام تحت مجموعتين («الملفات والتنزيلات» و«المهام والخدمات»).
 * تُنشأ من [com.dlof.rinlang.RinApplication.onCreate] قبل أول إشعار (أندرويد 8+ يتطلب قناة لكل إشعار).
 * الاستدعاء المتكرر آمن: إعادة إنشاء قناة موجودة تحدّث اسمها ووصفها فقط ولا تغيّر أهميتها
 * (لذلك استُبدلت القناة القديمة rin_downloads بقناتين جديدتين بدل تعديل أهميتها).
 *
 * ملاحظة: إنشاء القنوات لا يحتاج إذناً؛ إذن POST_NOTIFICATIONS (أندرويد 13+) مطلوب فقط عند عرض إشعار.
 */
object RinNotifications {

    // ---- المجموعات
    const val GROUP_FILES = "rin_group_files"
    const val GROUP_SYSTEM = "rin_group_system"

    // ---- القنوات الحالية
    /** تقدّم تنزيل/ضغط جارٍ — صامتة بلا شارة. */
    const val CHANNEL_DOWNLOAD_PROGRESS = "rin_dl_progress"
    /** اكتمال تنزيل — بصوت النظام الافتراضي وشارة. */
    const val CHANNEL_DOWNLOAD_DONE = "rin_dl_done"
    /** أخطاء وتنبيهات تحتاج انتباهاً (فشل تنزيل…) — أهمية عالية. */
    const val CHANNEL_ALERTS = "rin_alerts"
    /** نتائج تشغيل البرامج عندما يكون التطبيق في الخلفية. */
    const val CHANNEL_JOBS = "rin_jobs"
    /** إشعار خدمة «فتح في المتصفح» الدائم (المعرّف قديم ومحفوظ كي لا تضيع تفضيلات المستخدم). */
    const val CHANNEL_LIVE = "indsin_live_server"
    const val CHANNEL_GENERAL = "rin_general"

    /** قناة قديمة (أهمية LOW) حلّت محلها [CHANNEL_DOWNLOAD_PROGRESS] و[CHANNEL_DOWNLOAD_DONE]؛ تُحذف عند الإقلاع. */
    private const val LEGACY_CHANNEL_DOWNLOADS = "rin_downloads"

    fun createChannels(context: Context) {
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return
        runCatching {
            nm.createNotificationChannelGroups(
                listOf(
                    NotificationChannelGroup(GROUP_FILES, context.getString(R.string.notif_group_files)),
                    NotificationChannelGroup(GROUP_SYSTEM, context.getString(R.string.notif_group_system)),
                )
            )

            val doneSound = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
            val doneAttrs = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()

            fun channel(
                id: String, name: Int, desc: Int, importance: Int, group: String,
                configure: NotificationChannel.() -> Unit = {}
            ) = NotificationChannel(id, context.getString(name), importance).apply {
                description = context.getString(desc)
                this.group = group
                configure()
            }

            nm.createNotificationChannels(
                listOf(
                    channel(
                        CHANNEL_DOWNLOAD_PROGRESS, R.string.notif_channel_download_progress,
                        R.string.notif_channel_download_progress_desc,
                        NotificationManager.IMPORTANCE_LOW, GROUP_FILES
                    ) { setShowBadge(false); setSound(null, null); enableVibration(false) },
                    channel(
                        CHANNEL_DOWNLOAD_DONE, R.string.notif_channel_download_done,
                        R.string.notif_channel_download_done_desc,
                        NotificationManager.IMPORTANCE_DEFAULT, GROUP_FILES
                    ) { setShowBadge(true); setSound(doneSound, doneAttrs) },
                    channel(
                        CHANNEL_ALERTS, R.string.notif_channel_alerts,
                        R.string.notif_channel_alerts_desc,
                        NotificationManager.IMPORTANCE_HIGH, GROUP_FILES
                    ) { setShowBadge(true); enableVibration(true) },
                    channel(
                        CHANNEL_JOBS, R.string.notif_channel_jobs,
                        R.string.notif_channel_jobs_desc,
                        NotificationManager.IMPORTANCE_DEFAULT, GROUP_SYSTEM
                    ) { setShowBadge(false) },
                    channel(
                        CHANNEL_LIVE, R.string.notif_channel_live,
                        R.string.notif_channel_live_desc,
                        NotificationManager.IMPORTANCE_LOW, GROUP_SYSTEM
                    ) { setShowBadge(false); setSound(null, null); enableVibration(false) },
                    channel(
                        CHANNEL_GENERAL, R.string.notif_channel_general,
                        R.string.notif_channel_general_desc,
                        NotificationManager.IMPORTANCE_DEFAULT, GROUP_SYSTEM
                    ),
                )
            )
            nm.deleteNotificationChannel(LEGACY_CHANNEL_DOWNLOADS)
        }
    }
}
