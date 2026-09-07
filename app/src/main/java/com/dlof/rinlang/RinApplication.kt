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
    }
}
