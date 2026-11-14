package android.os
class Looper { companion object { fun getMainLooper() = Looper() } }
class Handler(l: Looper) { fun post(r: Runnable)=true; fun postDelayed(r: Runnable, d: Long)=true; fun removeCallbacks(r: Runnable) {}
  fun post(f: () -> Unit)=true }
interface IBinder
