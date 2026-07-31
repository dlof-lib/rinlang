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

    /** Returns a human readable version string for the native engine. */
    external fun engineVersion(): String

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

    // ---- Loomtime session (Needle) ----
    // Unlike [renderView] (a stateless one-shot render), a session keeps its Fabric + Warp state
    // alive natively across calls, so [LoomSession.tap] can run a Strand's `onTap` handler for
    // real -- including a top-level `fun` with an actual `while` loop -- and see the Warp cells it
    // mutated reflected back in the next Fabric snapshot. This is what makes a "Tap to increment"
    // button in the live preview actually do something, instead of the tap being silently ignored.
    //
    // Usage from whatever draws the preview (Canvas/Compose):
    //   val session = RinEngine.LoomSession(source, rootWidthPx)
    //   ...on each pointer-down at (x, y) in that same pixel space...
    //   val resultJson = session.tap(x, y)   // re-render the canvas from resultJson's "fabric"
    //   ...on each keystroke in the editor...
    //   val resultJson = session.updateSource(newSource) // keeps existing Warp state (e.g. a
    //                                                     // tapped counter) across the hot edit
    //   ...when the preview is closed/backgrounded...
    //   session.close()
    class LoomSession(source: String, rootWidth: Int = 390) {
        private var handle: Long = loomSessionCreateNative(source, rootWidth)
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

        /** Releases the native session. Safe to call more than once. */
        fun close() {
            if (!closed) { loomSessionFreeNative(handle); closed = true }
        }

        protected fun finalize() { close() }
    }

    private external fun loomSessionCreateNative(source: String, rootWidth: Int): Long
    private external fun loomSessionRenderJsonNative(handle: Long): String
    private external fun loomSessionTapNative(handle: Long, x: Double, y: Double): String
    private external fun loomSessionUpdateSourceNative(handle: Long, newSource: String): String
    private external fun loomSessionFreeNative(handle: Long)
}
