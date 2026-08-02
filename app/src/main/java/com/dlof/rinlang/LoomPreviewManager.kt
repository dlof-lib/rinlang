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

        /**
         * Optional: a tap/edit is taking long enough to be worth showing a loading indicator for
         * (debounced by [BUSY_DEBOUNCE_MS] so a normal near-instant render never flashes it), or
         * has just finished. Default no-op so existing implementers keep compiling unchanged.
         */
        fun onBusyChanged(busy: Boolean) {}

        /**
         * Optional: fired once if the in-flight operation is still running past
         * [SLOW_OPERATION_MS]. In practice only a real network call (apiCall/httpGet/... — see
         * rin_http.cpp) is slow enough to hit this; everything else the engine does (layout,
         * a Warp mutation, a non-network onTap handler) is effectively instant. Default no-op.
         */
        fun onSlowOperation() {}
    }

    /** Below this, a busy indicator would only flicker for a normal render — not shown. */
    private const val BUSY_DEBOUNCE_MS = 150L

    /** Past this, an in-flight op is almost certainly a real network call, not local computation. */
    private const val SLOW_OPERATION_MS = 4000L

    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "loom-preview-session") }
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile private var session: RinEngine.LoomSession? = null
    @Volatile private var listener: Listener? = null
    @Volatile private var lastResultJson: String? = null
    @Volatile private var lastElapsedMs: Long = 0

    // ---- busy-indicator bookkeeping (see beginTrackedOperation/endTrackedOperation below) ----
    @Volatile private var pendingBusyRunnable: Runnable? = null
    @Volatile private var pendingSlowRunnable: Runnable? = null
    @Volatile private var isBusyNow = false

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
        // Rare but possible (e.g. the preview Activity is recreated by a rotation while an
        // apiCall/httpGet is still in flight): sync the new listener to the current busy state
        // instead of leaving it thinking nothing is happening until the op eventually finishes.
        if (isBusyNow) {
            mainHandler.post { if (listener === l) l.onBusyChanged(true) }
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
            beginTrackedOperation()
            val json = try { fresh.currentJson() } catch (t: Throwable) { errorJson(t) }
            endTrackedOperation()
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
            beginTrackedOperation()
            val t0 = System.nanoTime()
            val json = try { current.updateSource(source) } catch (t: Throwable) { errorJson(t) }
            val ms = (System.nanoTime() - t0) / 1_000_000
            endTrackedOperation()
            deliver(json, ms)
        }
    }

    /** Dispatches a tap at ([x], [y]) in the session's root pixel space; result carries the new Fabric. */
    fun tap(x: Double, y: Double) {
        val current = session ?: return
        worker.execute {
            beginTrackedOperation()
            val t0 = System.nanoTime()
            val json = try { current.tap(x, y) } catch (t: Throwable) { errorJson(t) }
            val ms = (System.nanoTime() - t0) / 1_000_000
            endTrackedOperation()
            deliver(json, ms)
        }
    }

    /** Ends the live preview session entirely (closes the native handle, frees Warp state). */
    fun stop() {
        listener = null
        lastResultJson = null
        cancelPendingBusyIndicators()
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

    // ---- busy indicator: wraps a single native call (session-create/updateSource/tap) so a slow
    // one (almost always a real network call — apiCall/httpGet/... — see rin_http.cpp) surfaces a
    // loading state to the UI instead of the preview just looking frozen. Called from the worker
    // thread; the two Runnables it schedules run on the main thread per [Listener]'s contract. ----

    /** Call right before the blocking native call; schedules the debounced busy/slow callbacks. */
    private fun beginTrackedOperation() {
        val busyRunnable = Runnable {
            isBusyNow = true
            listener?.onBusyChanged(true)
        }
        val slowRunnable = Runnable { listener?.onSlowOperation() }
        pendingBusyRunnable = busyRunnable
        pendingSlowRunnable = slowRunnable
        mainHandler.postDelayed(busyRunnable, BUSY_DEBOUNCE_MS)
        mainHandler.postDelayed(slowRunnable, SLOW_OPERATION_MS)
    }

    /** Call right after the blocking native call returns (success or error alike). */
    private fun endTrackedOperation() {
        cancelPendingBusyIndicators()
        mainHandler.post {
            isBusyNow = false
            listener?.onBusyChanged(false)
        }
    }

    /** Cancels any not-yet-fired busy/slow callbacks (used by both endTrackedOperation and stop()). */
    private fun cancelPendingBusyIndicators() {
        pendingBusyRunnable?.let { mainHandler.removeCallbacks(it) }
        pendingSlowRunnable?.let { mainHandler.removeCallbacks(it) }
        pendingBusyRunnable = null
        pendingSlowRunnable = null
    }
}
