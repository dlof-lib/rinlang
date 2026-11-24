package com.dlof.rinlang.notify

import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.util.Log
import java.util.concurrent.atomic.AtomicInteger

/**
 * خدمة Foreground تُبقي عملية RinStudio حيّة بعد أن يغادر المستخدم التطبيق ما دام هناك عمل جارٍ:
 * تنزيل/ضغط ملفات أو برنامج Rin قيد التشغيل. بدونها قد يجمّد أندرويد العملية فينقطع التنزيل أو يتوقف البرنامج.
 *
 * العدّادان في [RinKeepAlive]؛ تبدأ الخدمة عند أول عمل (من الواجهة فقط، وهو الوضع المسموح)، وتنهي نفسها
 * وتزيل إشعارها حين يصل العدّادان إلى صفر. أي فشل في بدئها يُبتلع: العمل يستمر كما كان قبلها.
 */
class RinTransferService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // يجب استدعاء startForeground فوراً بعد startForegroundService حتى لو انتهى العمل في هذه اللحظة.
        try {
            startForeground(
                NOTIF_ID, RinNotifier.backgroundNotification(this, RinKeepAlive.downloads(), RinKeepAlive.jobs()),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            )
        } catch (t: Throwable) {
            Log.w(TAG, "startForeground failed: ${t.message}")
            instance = null
            stopSelf(); return START_NOT_STICKY
        }
        instance = this
        refresh()
        return START_NOT_STICKY
    }

    /** أندرويد 15+: لخدمات dataSync مهلة ~6 ساعات؛ ننهي الخدمة بهدوء عند استدعائها. */
    override fun onTimeout(startId: Int, fgsType: Int) {
        stopSelf()
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    /** يحدّث نص الإشعار، أو ينهي الخدمة إن لم يبقَ عمل. */
    internal fun refresh() {
        val d = RinKeepAlive.downloads()
        val j = RinKeepAlive.jobs()
        if (d + j <= 0) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return
        }
        runCatching {
            getSystemService(android.app.NotificationManager::class.java)
                ?.notify(NOTIF_ID, RinNotifier.backgroundNotification(this, d, j))
        }
    }

    companion object {
        private const val TAG = "RinTransferService"
        internal const val NOTIF_ID = 5760
        @Volatile internal var instance: RinTransferService? = null
    }
}

/** عدّادا العمل الجاري؛ كل تغيير يبدأ الخدمة أو يحدّثها أو ينهيها. آمن من أي خيط. */
object RinKeepAlive {
    private val downloads = AtomicInteger(0)
    private val jobs = AtomicInteger(0)

    fun downloads(): Int = downloads.get()
    fun jobs(): Int = jobs.get()

    fun downloadStarted(ctx: Context) { downloads.incrementAndGet(); sync(ctx) }
    fun downloadFinished(ctx: Context) { downloads.updateAndGet { (it - 1).coerceAtLeast(0) }; sync(ctx) }

    /** عدد البرامج المنتظرة أو الجارية الآن (يُضبط من المجدول عند كل تغيير). */
    fun setJobs(ctx: Context, pending: Int) {
        if (jobs.getAndSet(pending.coerceAtLeast(0)) != pending) sync(ctx)
    }

    private fun sync(ctx: Context) {
        val app = ctx.applicationContext
        val running = RinTransferService.instance
        if (downloads.get() + jobs.get() > 0) {
            if (running != null) running.refresh()
            else runCatching { app.startForegroundService(Intent(app, RinTransferService::class.java)) }
        } else {
            running?.refresh()
        }
    }
}
