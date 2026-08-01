package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import java.util.concurrent.Executors

/**
 * Bridges the code editor ([MainActivity]) and the Live Preview screen ([LoomPreviewActivity])
 * to a single native [RinEngine.LoomSession] that stays alive for as long as the preview is
 * "on" — independent of which Activity is currently on screen.
 *
 * Why a process-wide singleton instead of just an Activity field: [MainActivity]'s editor is
 * the thing that changes on every keystroke, but the Fabric that needs re-rendering lives in
 * [LoomPreviewActivity]. Rather than shuttling the full source text through Intents/extras (which
 * would also throw away Warp state — e.g. a tapped counter — on every edit), both screens talk to
 * the *same* native session here: the editor pushes [pushLiveEdit] on every debounced keystroke,
 * the preview screen [attach]es to receive the resulting Fabric JSON and renders it, and a tap on
 * the preview's canvas goes through [tap] and back into the same session.
 *
 * All native calls are dispatched on a single background thread (source edits and taps must be
 * applied to the session in the order they happened) and results are delivered back on the main
 * thread via [Listener]. The most recent result is cached so a listener that [attach]es *after*
 * the session already produced a frame (e.g. [LoomPreviewActivity] recreated by a rotation) is
 * synced immediately from the cache instead of racing the background thread for a fresh one.
 */
object LoomPreviewManager {

    interface Listener {
        /** A fresh Fabric snapshot is ready to draw. [elapsedMs] is the real native render time. */
        fun onFabricUpdated(resultJson: String, elapsedMs: Long)
    }

    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "loom-preview-session") }
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile private var session: RinEngine.LoomSession? = null
    @Volatile private var listener: Listener? = null
    @Volatile private var lastResultJson: String? = null
    @Volatile private var lastElapsedMs: Long = 0

    /** Last source text the session was created/updated with (survives Activity recreation). */
    @Volatile var lastSource: String = ""
        private set

    /** Root width (px) the current session was laid out at — see the device picker in the preview. */
    @Volatile var rootWidth: Int = 390
        private set

    /** True once a session exists — the editor only bothers pushing live edits when this is true. */
    val isRunning: Boolean get() = session != null

    /**
     * Registers [l] to receive Fabric updates. If a result is already cached (the session was
     * already producing frames before this listener showed up), [l] is synced immediately on the
     * main thread with that cached frame — no need to wait for the next edit/tap.
     */
    fun attach(l: Listener) {
        listener = l
        val cachedJson = lastResultJson
        if (cachedJson != null) {
            val cachedMs = lastElapsedMs
            mainHandler.post { if (listener === l) l.onFabricUpdated(cachedJson, cachedMs) }
        }
    }

    /** Unregisters [l] if it's still the active listener (no-op if the preview screen was closed/replaced). */
    fun detach(l: Listener) {
        if (listener === l) listener = null
    }

    /**
     * (Re)starts the session against [source] at [rootWidth] px, discarding any previous session
     * and its Warp state (used for the very first "Run", and whenever the device frame changes).
     */
    fun start(source: String, rootWidth: Int = this.rootWidth) {
        lastSource = source
        this.rootWidth = rootWidth
        worker.execute {
            session?.close()
            val fresh = RinEngine.LoomSession(source, rootWidth)
            session = fresh
            val json = try { fresh.currentJson() } catch (t: Throwable) { errorJson(t) }
            deliver(json, 0)
        }
    }

    /**
     * Hot-updates the running session with [source] (called on every debounced editor keystroke).
     * Keeps existing Warp state alive (e.g. a counter the user already tapped up), per
     * [RinEngine.LoomSession.updateSource]. No-ops silently if no session is running yet.
     */
    fun pushLiveEdit(source: String) {
        val current = session ?: return
        lastSource = source
        worker.execute {
            val t0 = System.nanoTime()
            val json = try { current.updateSource(source) } catch (t: Throwable) { errorJson(t) }
            val ms = (System.nanoTime() - t0) / 1_000_000
            deliver(json, ms)
        }
    }

    /** Dispatches a tap at ([x], [y]) in the session's root pixel space; result carries the new Fabric. */
    fun tap(x: Double, y: Double) {
        val current = session ?: return
        worker.execute {
            val t0 = System.nanoTime()
            val json = try { current.tap(x, y) } catch (t: Throwable) { errorJson(t) }
            val ms = (System.nanoTime() - t0) / 1_000_000
            deliver(json, ms)
        }
    }

    /** Ends the live preview session entirely (closes the native handle, frees Warp state). */
    fun stop() {
        listener = null
        lastResultJson = null
        worker.execute {
            session?.close()
            session = null
        }
    }

    /** Runs on the background worker thread: caches the result and, if a listener is attached, posts it. */
    private fun deliver(json: String, ms: Long) {
        lastResultJson = json
        lastElapsedMs = ms
        val l = listener ?: return
        mainHandler.post { if (listener === l) l.onFabricUpdated(json, ms) }
    }

    private fun errorJson(t: Throwable): String =
        "{\"ok\":false,\"error\":\"${(t.message ?: t.toString()).replace("\"", "'")}\",\"line\":0}"
}
