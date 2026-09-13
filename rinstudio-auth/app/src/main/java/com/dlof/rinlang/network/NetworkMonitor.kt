package com.dlof.rinlang.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Handler
import android.os.Looper

/**
 * غلاف بسيط فوق [ConnectivityManager] لفحص ومراقبة الاتصال الفعلي بالإنترنت (وليس فقط
 * الاتصال بشبكة واي فاي/بيانات بلا إنترنت فعلي، بفضل NET_CAPABILITY_VALIDATED).
 */
object NetworkMonitor {

    private val mainHandler = Handler(Looper.getMainLooper())

    /** فحص فوري ومتزامن: هل يوجد اتصال إنترنت صالح الآن؟ */
    fun isOnline(context: Context): Boolean {
        val cm = context.applicationContext
            .getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return false
        val network = cm.activeNetwork ?: return false
        val capabilities = cm.getNetworkCapabilities(network) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    }

    /**
     * يبدأ مراقبة حيّة لتغيّر حالة الاتصال. الاستدعاءات تصل دائماً على الـ main thread.
     * يجب استدعاء [unregister] لاحقاً (مثلاً في onStop) لتفادي تسريب الذاكرة.
     */
    fun register(context: Context, onAvailable: () -> Unit, onLost: () -> Unit): ConnectivityManager.NetworkCallback {
        val cm = context.applicationContext
            .getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val request = NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()

        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                mainHandler.post(onAvailable)
            }

            override fun onLost(network: Network) {
                mainHandler.post(onLost)
            }

            override fun onCapabilitiesChanged(network: Network, capabilities: NetworkCapabilities) {
                mainHandler.post {
                    if (capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)) {
                        onAvailable()
                    } else {
                        onLost()
                    }
                }
            }
        }

        cm.registerNetworkCallback(request, callback)
        return callback
    }

    fun unregister(context: Context, callback: ConnectivityManager.NetworkCallback) {
        try {
            val cm = context.applicationContext
                .getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            cm.unregisterNetworkCallback(callback)
        } catch (_: Exception) {
            // النداء قد يفشل إن كانت المراقبة متوقفة مسبقاً أو الـ Activity في طريقها للإغلاق.
        }
    }
}
