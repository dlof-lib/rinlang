package android.app
import android.content.*
open class Activity : Context()
open class Service : Context() {
  open fun onCreate() {}; open fun onDestroy() {}
  open fun onBind(i: Intent?): android.os.IBinder? = null
  open fun onStartCommand(i: Intent?, f: Int, id: Int): Int = 0
  fun stopSelf() {}; fun startForeground(id:Int, n:Notification, type:Int) {}
  companion object { const val START_NOT_STICKY = 2 } }
class Notification { companion object { const val CATEGORY_SERVICE="service"; const val FOREGROUND_SERVICE_IMMEDIATE=1 }
  class Action(val x:Int) { class Builder(i: android.graphics.drawable.Icon, t:String, p:PendingIntent) { fun build()=Action(0) } }
  class Builder(c: Context, ch:String) {
    fun setSmallIcon(i: android.graphics.drawable.Icon)=this; fun setContentTitle(s:String)=this; fun setContentText(s:String)=this
    fun setContentIntent(p:PendingIntent)=this; fun setOngoing(b:Boolean)=this; fun setOnlyAlertOnce(b:Boolean)=this; fun setShowWhen(b:Boolean)=this
    fun setCategory(s:String)=this; fun setForegroundServiceBehavior(i:Int)=this; fun addAction(a:Action)=this; fun build()=Notification() } }
class NotificationChannel(id:String, name:CharSequence, imp:Int) { fun setShowBadge(b:Boolean) {} }
class NotificationManager { fun createNotificationChannel(c:NotificationChannel) {}; fun notify(id:Int, n:Notification) {}
  companion object { const val IMPORTANCE_LOW = 2 } }
class PendingIntent { companion object {
  const val FLAG_IMMUTABLE=1; const val FLAG_UPDATE_CURRENT=2
  fun getActivity(c:Context,r:Int,i:Intent,f:Int)=PendingIntent(); fun getService(c:Context,r:Int,i:Intent,f:Int)=PendingIntent() } }
