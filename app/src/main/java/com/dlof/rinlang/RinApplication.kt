package com.dlof.rinlang

import android.app.Application

/**
 * نقطة انطلاق العملية: أول شيء نفعله هو تثبيت [CrashHandler] عبر [CrashHandler.install]،
 * حتى يلتقط أي كراش قاتل من أي نشاط/Thread لاحقاً ويعرضه في حوار بدل إغلاق التطبيق بصمت.
 * انظر AndroidManifest.xml (android:name=".RinApplication" على وسم application).
 */
class RinApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        CrashHandler.install(this)
        // Android 15/16: edge-to-edge إجباري عند targetSdk >= 35 — حشوات النظام تُطبَّق مركزياً (RinSystemBars).
        RinSystemBars.install(this)
        // قنوات الإشعارات يجب أن توجد قبل أول إشعار (أندرويد 8+)؛ الإنشاء المتكرر آمن.
        com.dlof.rinlang.permissions.RinNotifications.createChannels(this)
        // ينظّف بقايا تنزيلات/تثبيتات حزم انقطعت سابقاً — في خيط خلفي كي لا يؤخر الإقلاع.
        Thread { runCatching { com.dlof.rinlang.packs.PackStore.cleanupLeftovers(this) } }.start()
        RinMediaBridge.init(this) // جسر الوسائط لـ make.video/audio/image/ocr (docs/MAKE_MEDIA.md)
    }
}
