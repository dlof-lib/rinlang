package com.dlof.rinlang

import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.net.Uri
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.core.content.ContextCompat
import com.dlof.rinlang.permissions.RinNotifications

/**
 * خدمة Foreground تُبقي خادم «فتح في المتصفح» ([IndsinLiveServer]) حيّاً بعد خروج المستخدم إلى المتصفح.
 * بدونها قد يجمّد أندرويد عملية التطبيق (cached-app freezer / phantom process killer) فتتوقف المعاينة بعد دقائق.
 *
 *  - تبدأ من [IndsinLiveServer.openInBrowser] وتعرض إشعاراً دائماً (المنفذ + عدد التبويبات) فيه «فتح» و«إيقاف».
 *  - توقف الخادم وتنهي نفسها عند «إيقاف»، أو إن لم يبقَ تبويب نشط لمدة [IDLE_STOP_MS]، أو إن أُغلق الخادم.
 *  - فشل بدئها (ForegroundServiceStartNotAllowedException، عدم منح إذن الإشعارات…) لا يعطّل الخادم: يعمل كالسابق.
 *  - لا تحتاج WAKE_LOCK: حلقة loopback فقط والمتصفح في الواجهة (الشاشة مضاءة).
 */
class IndsinLiveService : Service() {

    private val handler = Handler(Looper.getMainLooper())
    private var lastText = ""
    private var startedAt = 0L
    private val tick = object : Runnable {
        override fun run() {
            if (!IndsinLiveServer.isRunning) { stopSelf(); return }
            if (IndsinLiveServer.idleMs() > IDLE_STOP_MS) {
                Log.i(TAG, "no active browser tab for ${IDLE_STOP_MS / 60000} min — stopping")
                IndsinLiveServer.stop(); stopSelf(); return
            }
            refresh(false)
            handler.postDelayed(this, TICK_MS)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        // القناة تُنشأ مركزياً (مجموعة «المهام والخدمات»)؛ الإنشاء المتكرر آمن إن بدأت الخدمة قبل التطبيق.
        RinNotifications.createChannels(this)
        startedAt = System.currentTimeMillis()
        IndsinLiveServer.onStopped = { handler.post { stopSelf() } }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            IndsinLiveServer.stop()
            stopSelf()
            return START_NOT_STICKY
        }
        if (!IndsinLiveServer.isRunning) { stopSelf(); return START_NOT_STICKY }
        try {
            startForeground(NOTIF_ID, build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } catch (t: Throwable) {
            // الخادم يبقى شغّالاً دون حماية الخدمة؛ لا نُسقط التطبيق.
            Log.w(TAG, "startForeground failed: ${t.message}")
            stopSelf(); return START_NOT_STICKY
        }
        handler.removeCallbacks(tick)
        handler.postDelayed(tick, TICK_MS)
        return START_NOT_STICKY // الخادم داخل العملية؛ إعادة تشغيل الخدمة وحدها بلا خادم بلا معنى.
    }

    override fun onDestroy() {
        handler.removeCallbacks(tick)
        IndsinLiveServer.onStopped = null
        super.onDestroy()
    }

    /** يحدّث نص الإشعار فقط عند تغيّره. */
    private fun refresh(force: Boolean) {
        val text = text()
        if (!force && text == lastText) return
        getSystemService(NotificationManager::class.java)?.notify(NOTIF_ID, build())
    }

    private fun text(): String =
        getString(R.string.indsin_live_notif_text, "127.0.0.1:${IndsinLiveServer.port}", IndsinLiveServer.activeClients())

    private fun build(): Notification {
        lastText = text()
        val open = PendingIntent.getActivity(
            this, 1, Intent(Intent.ACTION_VIEW, Uri.parse(IndsinLiveServer.currentUrl())).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stop = PendingIntent.getService(
            this, 2, Intent(this, IndsinLiveService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        // النقر على الجسم يعيد المستخدم إلى RinStudio نفسه، و«فتح» يفتح المعاينة في المتصفح.
        val app = packageManager.getLaunchIntentForPackage(packageName)?.let {
            PendingIntent.getActivity(this, 3, it, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        } ?: open
        val title = getString(R.string.indsin_live_notif_title)
        val openIcon = Icon.createWithResource(this, R.drawable.ic_notif_open)
        val stopIcon = Icon.createWithResource(this, R.drawable.ic_notif_stop)
        val publicVersion = Notification.Builder(this, RinNotifications.CHANNEL_LIVE)
            .setSmallIcon(R.drawable.ic_indsin_live_notif)
            .setContentTitle(title)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .build()
        return Notification.Builder(this, RinNotifications.CHANNEL_LIVE)
            .setSmallIcon(R.drawable.ic_indsin_live_notif)
            .setColor(ContextCompat.getColor(this, R.color.rin_accent))
            .setContentTitle(title)
            .setContentText(lastText)
            .setStyle(Notification.BigTextStyle().bigText(lastText + "\n" + getString(R.string.notif_live_hint)))
            .setContentIntent(app)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setWhen(startedAt)
            .setShowWhen(true)
            .setUsesChronometer(true) // مدة تشغيل الخادم
            .setCategory(Notification.CATEGORY_SERVICE)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .setPublicVersion(publicVersion)
            .setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
            .addAction(Notification.Action.Builder(openIcon, getString(R.string.indsin_live_notif_open), open).build())
            .addAction(Notification.Action.Builder(stopIcon, getString(R.string.indsin_live_notif_stop), stop).build())
            .build()
    }

    companion object {
        private const val TAG = "IndsinLiveService"
        private const val NOTIF_ID = 5757
        private const val ACTION_STOP = "com.dlof.rinlang.indsin.STOP_LIVE_SERVER"
        private const val TICK_MS = 15_000L
        private const val IDLE_STOP_MS = 10 * 60 * 1000L

        /** يبدأ الخدمة (من الواجهة فقط). أي فشل يُبتلع: الخادم يعمل بدونها. */
        fun start(context: Context) {
            try { context.applicationContext.startForegroundService(Intent(context.applicationContext, IndsinLiveService::class.java)) }
            catch (t: Throwable) { Log.w(TAG, "cannot start service: ${t.message}") }
        }
    }
}
