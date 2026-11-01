package com.dlof.rinlang

import android.annotation.SuppressLint
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.webkit.JavascriptInterface
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import org.json.JSONObject

/**
 * Android host for the official indsin-object translator & interpreter
 * (tools/indsin-object → assets/indsin/indsin_object.html).
 *
 * Renders an Indsin engine frame (`{"ok":true,"fabric":{...}}`) or a logical tree as live,
 * accessible HTML inside a [WebView] and reports user interaction back as [Event]s:
 *
 * ```
 * val host = IndsinObjectHost(webView, theme = "dark") { ev ->
 *     if (ev.type == "tap") IndsinPreviewManager.tap(ev.x ?: 0f, ev.y ?: 0f)
 * }
 * host.push(frameJson)          // every new frame from the engine session
 * host.setTheme("light")        // any built-in theme, "auto", or a custom theme name
 * ```
 *
 * Security: JavaScript is enabled only for the bundled page; the page cannot navigate away
 * (external http/https/mailto links open in the system handler), content:// and file access
 * outside the app's own assets are disabled, and every event arrives on the main thread as
 * parsed data — never evaluated.
 */
class IndsinObjectHost(
    private val webView: WebView,
    theme: String = "dark",
    rtl: Boolean? = null,
    private val onEvent: (Event) -> Unit = {}
) {
    /** A normalised interpreter event (see tools/indsin-object/types/indsin_object.d.ts). */
    data class Event(
        val type: String,
        val id: String,
        val path: String,
        val kind: String,
        val x: Float?,
        val y: Float?,
        val value: Any?,
        val raw: JSONObject
    )

    private val main = Handler(Looper.getMainLooper())
    private var loaded = false
    private var pendingFrame: String? = null
    private var currentTheme = theme

    init { attach(theme, rtl) }

    @SuppressLint("SetJavaScriptEnabled")
    private fun attach(theme: String, rtl: Boolean?) {
        webView.settings.apply {
            javaScriptEnabled = true            // required by the bundled interpreter only
            domStorageEnabled = false
            allowFileAccess = true              // file:///android_asset/ (bundled page)
            allowContentAccess = false
            @Suppress("DEPRECATION") allowFileAccessFromFileURLs = false
            @Suppress("DEPRECATION") allowUniversalAccessFromFileURLs = false
            mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
            setSupportMultipleWindows(false)
        }
        webView.addJavascriptInterface(Bridge(), "IndsinAndroid")
        webView.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView, url: String) {
                loaded = true
                pendingFrame?.let { eval(applyScript(it)); pendingFrame = null }
            }

            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                val uri = request.url
                if (uri.scheme == "file" && uri.path?.startsWith("/android_asset/indsin/") == true) return false
                if (request.isForMainFrame && uri.scheme in setOf("http", "https", "mailto", "tel")) {
                    try { webView.context.startActivity(Intent(Intent.ACTION_VIEW, uri).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (_: Exception) { }
                }
                return true // never navigate the host page away
            }
        }
        val dir = when (rtl) { true -> "rtl"; false -> "ltr"; null -> "auto" }
        loaded = false
        webView.loadUrl("file:///android_asset/indsin/indsin_object.html?demo=0&theme=${Uri.encode(theme)}&dir=$dir")
    }

    /** Renders a new engine frame (or logical tree). Safe to call before the page has loaded. */
    fun push(frameJson: String) {
        if (!loaded) { pendingFrame = frameJson; return }
        eval(applyScript(frameJson))
    }

    /** Switches theme without reloading: a built-in name (e.g. "Midnight"), "dark", "light" or "auto". */
    fun setTheme(theme: String) {
        currentTheme = theme
        if (loaded) eval("IndsinObject.setTheme(${JSONObject.quote(theme)})")
    }

    fun theme(): String = currentTheme

    /** Opens or closes a named overlay (Dialog / Drawer / Tooltip) declared in the current tree. */
    fun setOpen(name: String, open: Boolean) {
        if (loaded) eval("IndsinObject.setOpen(${JSONObject.quote(name)}, $open)")
    }

    /** Detaches the bridge; call from onDestroy of the owning Activity/Fragment. */
    fun destroy() {
        main.removeCallbacksAndMessages(null)
        webView.removeJavascriptInterface("IndsinAndroid")
    }

    private fun applyScript(frameJson: String) = "IndsinObject.applyFrame(${JSONObject.quote(frameJson)})"

    private fun eval(js: String) {
        if (Looper.myLooper() == Looper.getMainLooper()) webView.evaluateJavascript(js, null)
        else main.post { webView.evaluateJavascript(js, null) }
    }

    private inner class Bridge {
        @JavascriptInterface
        fun onEvent(json: String?) {
            if (json == null || json.length > 262_144) return
            val obj = try { JSONObject(json) } catch (_: Exception) { return }
            val ev = Event(
                type = obj.optString("type"),
                id = obj.optString("id"),
                path = obj.optString("path"),
                kind = obj.optString("kind"),
                x = if (obj.has("x")) obj.optDouble("x").toFloat() else null,
                y = if (obj.has("y")) obj.optDouble("y").toFloat() else null,
                value = obj.opt("value"),
                raw = obj
            )
            main.post { onEvent(ev) }
        }
    }
}
