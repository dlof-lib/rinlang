package com.dlof.rinlang.notify

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.app.RemoteInput

/**
 * يستقبل أزرار الإشعارات:
 *  - «إلغاء» لتنزيل جارٍ (إن قُتلت العملية فلا يوجد ما يُلغى، ويُزال الإشعار العالق فقط).
 *  - «إعادة التشغيل» و«نسخ المخرجات» في إشعار نتيجة تشغيل.
 *  - الرد المباشر على سؤال برنامج ينتظر إدخالاً («إرسال»/«إلغاء»).
 */
class RinNotificationReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val id = intent.getIntExtra(RinNotifier.EXTRA_ID, -1)
        when (intent.action) {
            RinNotifier.ACTION_CANCEL -> RinNotifier.handleCancel(context, id)
            RinNotifier.ACTION_JOB_RERUN -> RinNotifier.handleRerun(context, id)
            RinNotifier.ACTION_JOB_COPY -> RinNotifier.handleCopy(context, id)
            RinNotifier.ACTION_INPUT_REPLY -> {
                val text = RemoteInput.getResultsFromIntent(intent)?.getCharSequence(RinNotifier.KEY_REPLY)?.toString()
                RinNotifier.handleInput(context, id, text)
            }
            RinNotifier.ACTION_INPUT_CANCEL -> RinNotifier.handleInput(context, id, null)
        }
    }
}
