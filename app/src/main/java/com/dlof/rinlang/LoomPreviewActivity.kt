package com.dlof.rinlang

import android.content.Intent
import android.os.Bundle
import android.view.ContextThemeWrapper
import android.view.View
import android.widget.Button
import android.widget.ImageButton
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.R as MaterialR
import org.json.JSONObject

/**
 * Rin's Live Preview screen for the Loomtime rendering engine ("Mirror Loom"): shows a
 * `@view.<Kind>=name ... .end/view` tree exactly as the native C++ Loom engine laid it out,
 * inside a phone-shaped device frame, and keeps it live — every keystroke back in [MainActivity]
 * hot-updates the very same [LoomPreviewManager] session (preserving Warp state, e.g. a tapped
 * counter), and a tap on a Button here really runs its `onTap` handler through the engine.
 *
 * Nothing drawn here is simulated: the Fabric tree, its geometry, the strand/cache-hit counters
 * in the footer and every Snag error message all come straight from [RinEngine.LoomSession].
 */
class LoomPreviewActivity : AppCompatActivity(), LoomPreviewManager.Listener {

    companion object {
        /** Rin source to render — a fresh "Run" always restarts the session with this. */
        const val EXTRA_CODE = "loom_preview_code"
        /** Optional: the project-relative file name [EXTRA_CODE] came from (defaults to
         * "main.rin"). Used as the starting point for real page navigation below. */
        const val EXTRA_FILE_NAME = "loom_preview_file_name"
        private val DEVICE_WIDTHS = listOf(360, 390, 414, 428, 768)
        /** القيمة الافتراضية قبل أن نقيس عرض سطح المعاينة الفعلي على الشاشة (أول تخطيط فقط). */
        private const val DEFAULT_DEVICE_WIDTH = 390
    }

    private lateinit var fabricView: LoomFabricView
    private lateinit var txtTitle: TextView
    private lateinit var previewSurface: View
    private lateinit var errorBanner: View
    private lateinit var txtError: TextView
    private lateinit var inspectorPanel: View
    private lateinit var txtInspectorTitle: TextView
    private lateinit var txtInspectorBody: TextView
    private lateinit var txtStats: TextView
    private lateinit var txtZoom: TextView
    private lateinit var btnDevice: Button
    private lateinit var btnGrid: ImageButton

    private var currentDeviceWidth = DEFAULT_DEVICE_WIDTH

    /** Real page-to-page navigation: the file name currently loaded, and the stack of (name,
     * source) pairs to return to on system Back — see [navigateTo]/[navigateBack]. Reading the
     * target straight off disk under [RinEngine.currentBaseDir] (same root save/installation/file
     * already use) is what makes `onTap="navigate:mu.rin"` and a splash's `navigate=`/`duration=`
     * actually switch pages instead of [LoomFabricView.onNavigate] firing into nothing. */
    private var currentPageName: String = "main.rin"
    private val pageStack = ArrayDeque<Pair<String, String>>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_loom_preview)
        // ملاحظة: لا نستدعي RinEngine.init(...) هنا عمداً — هذه الشاشة تُفتح دائماً بعد أن يكون
        // المحرر (MainActivity) قد هيّأ الجذر الصحيح (عام أو خاص بمشروع) لكائن RinEngine الوحيد
        // المشترك في العملية؛ استدعاء init مجدداً هنا قد يعيده خطأً إلى الجذر العام في منتصف الجلسة.

        fabricView = findViewById(R.id.loomFabricView)
        txtTitle = findViewById(R.id.txtLoomTitle)
        previewSurface = findViewById(R.id.loomHScroll)
        errorBanner = findViewById(R.id.loomErrorBanner)
        txtError = findViewById(R.id.txtLoomError)
        inspectorPanel = findViewById(R.id.loomInspectorPanel)
        txtInspectorTitle = findViewById(R.id.txtInspectorTitle)
        txtInspectorBody = findViewById(R.id.txtInspectorBody)
        txtStats = findViewById(R.id.txtLoomStats)
        txtZoom = findViewById(R.id.txtLoomZoom)
        btnDevice = findViewById(R.id.btnLoomDevice)
        btnGrid = findViewById(R.id.btnLoomGrid)

        val btnClose: ImageButton = findViewById(R.id.btnLoomClose)
        val btnZoomOut: ImageButton = findViewById(R.id.btnLoomZoomOut)
        val btnZoomIn: ImageButton = findViewById(R.id.btnLoomZoomIn)
        val btnRefresh: ImageButton = findViewById(R.id.btnLoomRefresh)
        val btnInspectorClose: ImageButton = findViewById(R.id.btnInspectorClose)

        btnClose.setOnClickListener { finish() }

        btnGrid.alpha = 0.55f
        btnGrid.setOnClickListener {
            fabricView.showGrid = !fabricView.showGrid
            btnGrid.alpha = if (fabricView.showGrid) 1f else 0.55f
        }

        btnZoomOut.setOnClickListener { setZoom(fabricView.zoom - 0.1f) }
        btnZoomIn.setOnClickListener { setZoom(fabricView.zoom + 0.1f) }
        btnRefresh.setOnClickListener { restartSession(showToast = true) }
        btnDevice.setOnClickListener { showDeviceMenu(it) }

        btnInspectorClose.setOnClickListener {
            inspectorPanel.visibility = View.GONE
            fabricView.clearInspection()
        }

        fabricView.onTap = { x, y -> LoomPreviewManager.tap(x, y) }
        fabricView.onInspect = { node -> showInspector(node) }
        fabricView.onNavigate = { target -> navigateTo(target) }
        // Real pinch-to-zoom on the canvas keeps this label in sync too, not just the toolbar
        // +/- buttons (setZoom already updates it for those — this covers the gesture path).
        fabricView.onZoomChanged = { z -> txtZoom.text = getString(R.string.loom_zoom_percent_format, (z * 100).toInt()) }

        currentPageName = intent.getStringExtra(EXTRA_FILE_NAME)?.takeIf { it.isNotBlank() } ?: currentPageName
        txtTitle.text = currentPageName

        // نسجّل كمستمع أولاً — قبل أي start() — حتى لا نفوّت أول إطار (سباق مع خيط الجلسة بالخلفية).
        LoomPreviewManager.attach(this)

        currentDeviceWidth = if (LoomPreviewManager.isRunning) LoomPreviewManager.rootWidth else DEFAULT_DEVICE_WIDTH
        btnDevice.text = getString(R.string.loom_device_width_format, currentDeviceWidth)

        // savedInstanceState != null يعني أن هذه إعادة إنشاء (مثل تدوير الشاشة) لنفس الجلسة الحيّة —
        // لا نعيد التشغيل حتى لا نفقد حالة Warp (كعدّاد تم الضغط عليه)؛ نكتفي بإعادة رسم آخر إطار
        // معروف، وهو ما توفّره attach() أعلاه تلقائياً من الذاكرة المؤقتة.
        if (savedInstanceState == null) {
            val incomingCode = intent.getStringExtra(EXTRA_CODE)
            if (incomingCode != null) {
                if (LoomPreviewManager.isRunning) {
                    LoomPreviewManager.start(incomingCode, currentDeviceWidth)
                } else {
                    // جلسة جديدة تماماً: ننتظر أول تخطيط لسطح المعاينة لنقيس عرضه الفعلي على
                    // الشاشة (بالـ dp) ونبدأ الجلسة بهذا العرض، بدل عرض جهاز افتراضي ثابت (390) قد
                    // يترك فراغاً على الجانبين أو يفيض عن الشاشة — فتملأ المعاينة العرض كاملاً فعلياً.
                    previewSurface.post {
                        currentDeviceWidth = fitDeviceWidth()
                        btnDevice.text = getString(R.string.loom_device_width_format, currentDeviceWidth)
                        LoomPreviewManager.start(incomingCode, currentDeviceWidth)
                    }
                }
            }
        }
    }

    /** يحوّل عرض سطح المعاينة الحالي (بكسل فعلي) إلى dp — هذا هو "عرض الجهاز" الذي يملأ الشاشة تماماً. */
    private fun fitDeviceWidth(): Int {
        val widthPx = previewSurface.width
        if (widthPx <= 0) return DEFAULT_DEVICE_WIDTH
        val density = resources.displayMetrics.density
        return (widthPx / density).toInt().coerceAtLeast(200)
    }

    /** يُستدعى عند ضغط "تشغيل" مجدداً من المحرر بينما هذه الشاشة (singleTask) ما تزال في المهمّة. */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        val code = intent.getStringExtra(EXTRA_CODE) ?: return
        // A fresh "Run" from the editor restarts at the root page — any pages navigated into
        // during the previous session no longer apply.
        pageStack.clear()
        currentPageName = intent.getStringExtra(EXTRA_FILE_NAME)?.takeIf { it.isNotBlank() } ?: "main.rin"
        txtTitle.text = currentPageName
        inspectorPanel.visibility = View.GONE
        fabricView.clearInspection()
        LoomPreviewManager.start(code, currentDeviceWidth)
    }

    /** Real page navigation: reads [target] off disk under the current project's root (same root
     * save/installation/file already write to) and switches the live session to it, pushing the
     * page we came from onto [pageStack] for system Back. Fired by [LoomFabricView.onNavigate] —
     * either a tapped `onTap="navigate:...".` or a splash's timed `navigate=`/`duration=`. */
    private fun navigateTo(target: String) {
        val base = RinEngine.currentBaseDir()
        if (base.isBlank()) return
        var file = java.io.File(base, target)
        if (!file.isFile && !target.endsWith(".rin")) file = java.io.File(base, "$target.rin")
        val newSource = if (file.isFile) {
            try { file.readText() } catch (t: Throwable) { null }
        } else null

        if (newSource == null) {
            Toast.makeText(this, getString(R.string.loom_navigate_missing_toast, target), Toast.LENGTH_SHORT).show()
            return
        }

        pageStack.addLast(currentPageName to LoomPreviewManager.lastSource)
        currentPageName = file.name
        txtTitle.text = currentPageName
        inspectorPanel.visibility = View.GONE
        fabricView.clearInspection()
        LoomPreviewManager.start(newSource, currentDeviceWidth)
    }

    /** Pops [pageStack] back to the previous page, if any. Returns false (does nothing) once the
     * stack is empty, so the caller can fall back to the normal system-Back behavior (closing). */
    private fun navigateBack(): Boolean {
        val (name, source) = pageStack.removeLastOrNull() ?: return false
        currentPageName = name
        txtTitle.text = currentPageName
        inspectorPanel.visibility = View.GONE
        fabricView.clearInspection()
        LoomPreviewManager.start(source, currentDeviceWidth)
        return true
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (!navigateBack()) super.onBackPressed()
    }

    override fun onDestroy() {
        super.onDestroy()
        LoomPreviewManager.detach(this)
    }

    override fun onFabricUpdated(resultJson: String, elapsedMs: Long) {
        renderResult(resultJson, elapsedMs)
    }

    // ---- rendering the engine's result JSON ----

    private fun renderResult(resultJson: String, elapsedMs: Long) {
        val result = try { JSONObject(resultJson) } catch (t: Throwable) { null }
        if (result == null) {
            showError(getString(R.string.loom_internal_error))
            return
        }

        val fabric = result.optJSONObject("fabric")
        if (fabric == null) {
            // فشل كامل: لم تتوفر شجرة Fabric صالحة إطلاقاً بعد (لا إطار سابق سليم لعرضه).
            fabricView.setFabric(null, currentDeviceWidth, 640)
            showError(formatError(result))
            txtStats.text = getString(R.string.loom_stats_error_only)
            return
        }

        val h = fabric.optDouble("h", 640.0).toInt().coerceAtLeast(120)
        fabricView.setFabric(fabric, currentDeviceWidth, h, result.optJSONArray("overlays"))

        val hasSnag = result.has("snag") || result.has("error")
        if (hasSnag) showError(formatError(result)) else hideError()

        val measured = result.optInt("strandsMeasured", -1)
        val cache = result.optInt("cacheHits", -1)
        txtStats.text = if (measured >= 0) {
            getString(R.string.loom_stats_format, measured, elapsedMs, cache)
        } else {
            getString(R.string.loom_stats_idle)
        }
    }

    private fun formatError(result: JSONObject): String {
        val line = result.optInt("line", 0)
        val message = result.optString("error", getString(R.string.loom_unknown_error))
        return if (line > 0) getString(R.string.loom_error_line_format, line, message)
        else getString(R.string.loom_error_format, message)
    }

    private fun showError(message: String) {
        txtError.text = message
        errorBanner.visibility = View.VISIBLE
    }

    private fun hideError() {
        errorBanner.visibility = View.GONE
    }

    // ---- Inspector (long-press on the canvas) ----

    private fun showInspector(node: JSONObject?) {
        if (node == null) {
            inspectorPanel.visibility = View.GONE
            return
        }
        val kind = node.optString("kind", "?")
        val name = node.optString("name", "")
        val line = node.optInt("line", 0)
        txtInspectorTitle.text = getString(R.string.loom_inspector_title_format, kind, name, line)

        val x = node.optDouble("x", 0.0).toInt()
        val y = node.optDouble("y", 0.0).toInt()
        val w = node.optDouble("w", 0.0).toInt()
        val h = node.optDouble("h", 0.0).toInt()
        val sb = StringBuilder()
        sb.append("x: ").append(x).append("   y: ").append(y)
            .append("   w: ").append(w).append("   h: ").append(h)

        val attrs = node.optJSONObject("attrs")
        if (attrs != null) {
            val keys = attrs.keys()
            while (keys.hasNext()) {
                val key = keys.next()
                sb.append('\n').append(key).append(": ").append(attrs.optString(key))
            }
        }
        txtInspectorBody.text = sb.toString()
        inspectorPanel.visibility = View.VISIBLE
    }

    // ---- toolbar actions ----

    private fun setZoom(value: Float) {
        fabricView.zoom = value
        val pct = (fabricView.zoom * 100).toInt()
        txtZoom.text = getString(R.string.loom_zoom_percent_format, pct)
    }

    private fun restartSession(showToast: Boolean) {
        val source = LoomPreviewManager.lastSource
        LoomPreviewManager.start(source, currentDeviceWidth)
        inspectorPanel.visibility = View.GONE
        fabricView.clearInspection()
        if (showToast) {
            Toast.makeText(this, getString(R.string.loom_session_restarted_toast), Toast.LENGTH_SHORT).show()
        }
    }

    private fun showDeviceMenu(anchor: View) {
        val themedContext = ContextThemeWrapper(this, MaterialR.style.ThemeOverlay_MaterialComponents_Dark)
        val popup = PopupMenu(themedContext, anchor)
        popup.menu.add(0, -1, 0, getString(R.string.loom_device_fit_screen))
        DEVICE_WIDTHS.forEachIndexed { index, width ->
            popup.menu.add(0, index, index + 1, getString(R.string.loom_device_width_format, width))
        }
        popup.setOnMenuItemClickListener { item ->
            val width = if (item.itemId == -1) fitDeviceWidth() else DEVICE_WIDTHS.getOrNull(item.itemId)
            if (width != null && width != currentDeviceWidth) {
                currentDeviceWidth = width
                btnDevice.text = getString(R.string.loom_device_width_format, width)
                // تغيير عرض الجهاز يستلزم إعادة تخطيط (layout) كاملة من المحرّك الأصلي بعرض جديد،
                // لذا هذا الإجراء الوحيد الذي يعيد تشغيل الجلسة (ويُفقد حالة Warp) بخلاف زر "تحديث".
                restartSession(showToast = false)
                Toast.makeText(this, getString(R.string.loom_device_changed_toast, width), Toast.LENGTH_SHORT).show()
            }
            true
        }
        popup.show()
    }
}
