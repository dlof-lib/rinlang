package com.dlof.rinlang

import android.content.Context

/**
 * Kotlin gateway into the native Rin language engine written in C++.
 * The C++ implementation lives under app/src/main/cpp and is compiled
 * into libRinengine.so via CMake / the Android NDK.
 *
 * منذ إضافة الحفظ/التثبيت الحقيقيَّين على القرص (save / installation / file / writeFile / readFile...
 * داخل لغة الحاويات)، يحتاج المحرّك C++ جذر مسار حقيقي تُبنى فوقه كل هذه العمليات. [init] يمرّر
 * مجلد التطبيق الخاص (filesDir) — وهو تخزين معزول لا يحتاج أذونات — ليُستخدم كجذر لكل ذلك.
 * استدعِ [init] مرة واحدة مبكراً (مثلاً في أول onCreate يُنفَّذ) قبل أي [runSource].
 */
object RinEngine {

    init {
        System.loadLibrary("rinengine")
    }

    @Volatile
    private var baseDir: String = ""

    /** يُستدعى مرة واحدة (عادة من MainActivity.onCreate) لتفعيل تخزين حقيقي فعلي لـ save/installation/file. */
    fun init(context: Context) {
        baseDir = context.filesDir.absolutePath
    }

    /**
     * نفس [init] لكن مع تجاوز الجذر بمسار مشروع محدد (انظر [ProjectManager])، بحيث تعمل
     * save/installation/file لهذا المشروع فقط داخل مجلده الخاص بدل filesDir العام للتطبيق.
     */
    fun init(context: Context, projectBasePath: String) {
        baseDir = projectBasePath
    }

    /**
     * الجذر الحقيقي على القرص الذي تُبنى فوقه كل مسارات save/installation/file النسبية
     * (نفس المسار الممرَّر لـ runSourceNative). يُستخدم لتحديد موقع الملفات الفعلية
     * التي أنتجها آخر تشغيل حتى يمكن تنزيلها إلى مجلد Downloads العام بالجهاز.
     */
    fun currentBaseDir(): String = baseDir

    /** Lexes, parses and interprets [source]; returns everything the program printed. */
    fun runSource(source: String): String = runSourceNative(source, baseDir)

    private external fun runSourceNative(source: String, baseDir: String): String

    /**
     * Structured sibling of [runSource]: same lexer/parser/interpreter pipeline, but the
     * SUCCESS/ERROR outcome comes from the engine's own execution state (see
     * `Interpreter::hadError()` in rin_interpreter.h) instead of being guessed from the text of
     * the output -- so `print ["hello", "world"];` (output starting with '[') is correctly
     * SUCCESS, and a runtime error that happens after some `print`s have already run is
     * correctly ERROR even though "[" isn't the first character of the combined output.
     *
     * [runSource] is untouched and keeps working exactly as before; this is purely additive.
     */
    fun runSourceStructured(source: String): RinExecutionResult =
        RinExecutionResult.parse(runSourceStructuredNative(source, baseDir))

    private external fun runSourceStructuredNative(source: String, baseDir: String): String

    /**
     * Receives live output as [runSourceStructuredStreaming] executes. [onChunk] is invoked
     * synchronously, on the calling thread, once per top-level Rin statement that produced new
     * output — real incremental delivery straight out of `Interpreter::run`'s execution loop
     * (see rin_interpreter.cpp), not a replay or a timed animation of an already-finished run.
     * [sequence] starts at 1 and increases by 1 per call for this run.
     *
     * Implementations must be fast and must not block: this callback runs *inside* the native
     * call, on whatever thread invoked [runSourceStructuredStreaming] — the Rin program's own
     * execution is paused between statements while [onChunk] runs. Do not touch Android UI
     * directly from here; hop back to the main thread first (see how [RinJobScheduler] batches
     * updates before posting to its `mainHandler`).
     */
    fun interface RinStreamListener {
        fun onChunk(sequence: Int, chunk: String)
    }

    /**
     * Same lexer/parser/interpreter pipeline and same final [RinExecutionResult] shape as
     * [runSourceStructured] — [listener] just additionally receives each statement's output the
     * moment it's produced, instead of only seeing the fully assembled [RinExecutionResult.output]
     * once the whole program has finished. [runSource] and [runSourceStructured] are both
     * untouched; this is purely additive.
     */
    fun runSourceStructuredStreaming(source: String, listener: RinStreamListener): RinExecutionResult =
        RinExecutionResult.parse(runSourceStructuredStreamingNative(source, baseDir, listener))

    private external fun runSourceStructuredStreamingNative(
        source: String,
        baseDir: String,
        listener: RinStreamListener
    ): String

    /** Returns a human readable version string for the native engine. */
    external fun engineVersion(): String

    // ---- RinFlow: Execution Flow Engine (see rin_interpreter.h: namespace rin::flow) ----
    // Turns any top-level `a |> b() |> c()` chain in [source] into a real Flow Graph, executed
    // once (not re-run per stage like the older [PipelineTracer] probing technique still used for
    // `@container.pipe` blocks — see that file's kdoc), with live per-node events and a final
    // graph/metrics snapshot. [timeoutMs] <= 0 means no timeout. [listener], if given, receives
    // every node/flow event synchronously as they happen (same calling-thread contract as
    // [RinStreamListener] above — do not touch Android UI directly from it).
    fun interface RinFlowListener {
        fun onFlowEvent(eventJson: String)
    }

    fun runSourceAsFlow(source: String, timeoutMs: Long = 0L, listener: RinFlowListener? = null): RinFlowRunResult =
        RinFlowRunResult.parse(runFlowNative(source, baseDir, timeoutMs, listener))

    private external fun runFlowNative(source: String, baseDir: String, timeoutMs: Long, listener: RinFlowListener?): String

    /** Requests cooperative cancellation of a still-RUNNING flow session (see
     *  `rin::flow::FlowSession::cancelFlag`); the flow only actually stops at the next `|>` stage
     *  boundary — same cooperative model [RinJobScheduler] already documents for whole-program
     *  timeouts. Returns false if the session id is unknown or already finished. */
    fun cancelFlow(sessionId: String): Boolean = cancelFlowNative(sessionId)

    private external fun cancelFlowNative(sessionId: String): Boolean

    /** Re-executes the last `|>` chain [previousSessionId] ran, in a brand-new session — the
     *  original session is never modified (section 11: Replay). Returns a result whose
     *  [RinFlowRunResult.parseError] is set instead of a graph if [previousSessionId] is unknown
     *  (already pruned — the engine only keeps a bounded number of recent flow sessions — or never
     *  ran a `|>` chain at all). */
    fun replayFlow(previousSessionId: String, timeoutMs: Long = 0L, listener: RinFlowListener? = null): RinFlowRunResult =
        RinFlowRunResult.parse(replayFlowNative(previousSessionId, timeoutMs, listener))

    private external fun replayFlowNative(previousSessionId: String, timeoutMs: Long, listener: RinFlowListener?): String


    /**
     * Loomtime rendering engine: parses a `@view.<Kind>=name ... .end/view` root out of [source],
     * lays it out at [rootWidth] px via the native Loom engine, and returns a JSON dump of the
     * Fabric (kind/name/source line/geometry/resolved attributes, recursively) for a Compose/
     * Canvas layer to draw. On a parse/semantic error, returns {"error": "...", "line": N}
     * instead of throwing — callers should keep showing their last-good frame in that case
     * (see the Snag containment model in the Loomtime architecture doc).
     */
    fun renderView(source: String, rootWidth: Int = 390): String = renderViewNative(source, rootWidth)

    private external fun renderViewNative(source: String, rootWidth: Int): String

    /**
     * Container-scoped counterpart of [renderView]: builds the Fabric from the `@view` root that
     * lives *inside* the named `@container` (rather than the top-level program). This is the
     * piece that ties Loomtime to `container`: any `@container` carrying its own `@view`/`warp`/
     * `@theme` becomes an independently renderable screen/component, scoped to that container's
     * own warp state, addressable by the container's name. Returns
     * `{"error": "...", "line": N}` if no container with that name exists, or it has no `@view`
     * root inside it.
     */
    fun renderContainerView(source: String, containerName: String, rootWidth: Int = 390): String =
        renderContainerViewNative(source, containerName, rootWidth)

    private external fun renderContainerViewNative(source: String, containerName: String, rootWidth: Int): String

    // ---- Loomtime session (Needle) ----
    // Unlike [renderView] (a stateless one-shot render), a session keeps its Fabric + Warp state
    // alive natively across calls, so [LoomSession.tap] can run a Strand's `onTap` handler for
    // real -- including a top-level `fun` with an actual `while` loop -- and see the Warp cells it
    // mutated reflected back in the next Fabric snapshot. This is what makes a "Tap to increment"
    // button in the live preview actually do something, instead of the tap being silently ignored.
    //
    // Usage from whatever draws the preview (Canvas/Compose):
    //   val session = RinEngine.LoomSession(source, rootWidthPx)
    //   ...or, scoped to one @container's own @view/warp/@theme...
    //   val session = RinEngine.LoomSession(source, rootWidthPx, containerName = "Home")
    //   ...on each pointer-down at (x, y) in that same pixel space...
    //   val resultJson = session.tap(x, y)   // re-render the canvas from resultJson's "fabric"
    //   ...on each keystroke in the editor...
    //   val resultJson = session.updateSource(newSource) // keeps existing Warp state (e.g. a
    //                                                     // tapped counter) across the hot edit
    //   ...when the preview is closed/backgrounded...
    //   session.close()
    class LoomSession(source: String, rootWidth: Int = 390, containerName: String? = null) {
        private var handle: Long =
            if (containerName == null) loomSessionCreateNative(source, rootWidth)
            else loomSessionCreateForContainerNative(source, containerName, rootWidth)
        private var closed = false

        /** Current Fabric snapshot -- same JSON shape [renderView] returns. */
        fun currentJson(): String = loomSessionRenderJsonNative(handle)

        /**
         * Dispatches a tap at ([x], [y]) in the same pixel space as [rootWidth]. Returns
         * `{"ok":true,"handled":bool,"targetId":N,"changed":[...],"fabric":{...}}` (plus an
         * `"error"` field if a handler was found but failed at runtime).
         */
        fun tap(x: Double, y: Double): String = loomSessionTapNative(handle, x, y)

        /** Re-parses [newSource] and diffs it in place, preserving current Warp state. */
        fun updateSource(newSource: String): String = loomSessionUpdateSourceNative(handle, newSource)

        /**
         * Tells the Overlay Engine (Dialog centering / Tooltip clamping — see
         * `rin_loom_session_set_viewport`'s own doc comment in rin_loom_c_api.h/.cpp) the real
         * on-screen viewport height in the same pixel space as [rootWidth], so an open `@Dialog`
         * re-centers and an anchored `@Tooltip` re-clamps against the *actual* device instead of
         * the native side's 844px fallback. Cheap: re-runs layout's second (Overlay) pass only,
         * not a full re-render -- safe to call on every rotation/resize. No-op if no overlay is
         * currently open (nothing to re-home yet); the next [currentJson]/[tap]/[updateSource]
         * result will simply reflect it once one is.
         */
        fun setViewport(viewportHeight: Int) {
            if (!closed) loomSessionSetViewportNative(handle, viewportHeight)
        }

        /** Releases the native session. Safe to call more than once. */
        fun close() {
            if (!closed) { loomSessionFreeNative(handle); closed = true }
        }

        protected fun finalize() { close() }
    }

    private external fun loomSessionCreateNative(source: String, rootWidth: Int): Long
    private external fun loomSessionCreateForContainerNative(source: String, containerName: String, rootWidth: Int): Long
    private external fun loomSessionRenderJsonNative(handle: Long): String
    private external fun loomSessionTapNative(handle: Long, x: Double, y: Double): String
    private external fun loomSessionUpdateSourceNative(handle: Long, newSource: String): String
    private external fun loomSessionSetViewportNative(handle: Long, viewportHeight: Int)
    private external fun loomSessionFreeNative(handle: Long)
}
