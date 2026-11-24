package com.dlof.rinlang.notify

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * يستقبل ضغطة «إلغاء» من إشعار تقدّم التنزيل. إن كانت العملية قد قُتلت بعد بدء التنزيل فلا يوجد ما يُلغى،
 * ويكتفي [RinNotifier.handleCancel] بإزالة الإشعار العالق.
 */
class RinNotificationReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == RinNotifier.ACTION_CANCEL) {
            RinNotifier.handleCancel(context, intent.getIntExtra(RinNotifier.EXTRA_ID, -1))
        }
    }
}
