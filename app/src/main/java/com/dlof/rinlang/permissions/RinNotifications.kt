package com.dlof.rinlang.permissions

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.os.Build

/**
 * قنوات إشعارات التطبيق. تُنشأ من [com.dlof.rinlang.RinApplication.onCreate] قبل أول إشعار
 * (أندرويد 8+ يتطلب قناة لكل إشعار). الاستدعاء المتكرر آمن: إعادة إنشاء قناة موجودة لا تغيّر شيئاً.
 *
 * ملاحظة: إنشاء القنوات لا يحتاج إذناً؛ إذن POST_NOTIFICATIONS (أندرويد 13+) مطلوب فقط عند عرض إشعار.
 */
object RinNotifications {
    const val CHANNEL_GENERAL = "rin_general"
    const val CHANNEL_DOWNLOADS = "rin_downloads"
    const val CHANNEL_JOBS = "rin_jobs"

    fun createChannels(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return
        val ar = context.resources.configuration.locales.get(0)?.language == "ar"
        fun ch(id: String, nameAr: String, nameEn: String, importance: Int) =
            NotificationChannel(id, if (ar) nameAr else nameEn, importance)
        runCatching {
            nm.createNotificationChannels(
                listOf(
                    ch(CHANNEL_GENERAL, "إشعارات عامة", "General", NotificationManager.IMPORTANCE_DEFAULT),
                    ch(CHANNEL_DOWNLOADS, "التنزيلات والحزم", "Downloads & packages", NotificationManager.IMPORTANCE_LOW),
                    ch(CHANNEL_JOBS, "المهام المجدولة", "Scheduled jobs", NotificationManager.IMPORTANCE_DEFAULT),
                )
            )
        }
    }
}
