package com.dlof.rinlang

import android.view.Choreographer
import android.view.View
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject

/**
 * لوحة «المعاينة الحية» المدمجة داخل المحرر (بجانب الكود مباشرة).
 *
 * قبل هذه اللوحة كانت المعاينة الحية شاشة كاملة منفصلة ([IndsinPreviewActivity]) — أي أن المحرر
 * يختفي أثناء النظر إلى المعاينة، فلا يمكن رؤية أثر الكتابة لحظياً رغم وسم «مباشر». هنا تُعرض
 * **نفس الجلسة الأصلية** ([IndsinPreviewManager]) داخل [MainActivity] نفسها: كل ضغطة مفتاح تُدفَع
 * إلى المحرك (انظر MainActivity.scheduleLivePreviewPush) وتعود النتيجة إلى [onFabricUpdated] فتُرسم
 * فوراً بجانب المحرر، مع الإبقاء على حالة Warp (كعدّاد ضُغط عليه) واللمس الحقيقي والتأثيرات والساعة.
 *
 * مستمع [IndsinPreviewManager] واحد فقط في كل لحظة: عند فتح الشاشة الكاملة تحلّ مكان هذه اللوحة، ثم
 * تعيد [resume] ربط اللوحة عند الرجوع إلى المحرر (الإطار الأخير يُستعاد من ذاكرة المدير فلا وميض).
 */
class IndsinLivePanel(
    private val activity: AppCompatActivity,
    private val root: View,
) : IndsinPreviewManager.Listener {

    /** المستخدم ضغط «ملء الشاشة»: المضيف يفتح [IndsinPreviewActivity] بالمصدر الحالي. */
    var onExpandRequested: (() -> Unit)? = null

    private val fabricView: IndsinFabricView = root.findViewById(R.id.livePanelFabric)
    private val surface: View = root.findViewById(R.id.livePanelHScroll)
    private val txtTitle: TextView = root.findViewById(R.id.livePanelTitle)
    private val txtStats: TextView = root.findViewById(R.id.livePanelStats)
    private val errorBanner: View = root.findViewById(R.id.livePanelErrorBanner)
    private val txtError: TextView = root.findViewById(R.id.livePanelError)
    private val btnBack: ImageButton = root.findViewById(R.id.livePanelBack)

    /** منتقي الوسائط: يُسجّل launchers لذا يجب إنشاء اللوحة قبل STARTED (أي في onCreate). */
    private val mediaHost = IndsinMediaHost(activity)

    private var deviceWidth = 390
    private var pageName = "main.rin"
    private val pageStack = ArrayDeque<Pair<String, String>>()

    /** مصدر ينتظر أول قياس حقيقي لعرض السطح كي تبدأ الجلسة بعرضه الفعلي (dp). */
    private var pendingStart: String? = null

    val isShown: Boolean get() = root.visibility == View.VISIBLE

    init {
        root.findViewById<ImageButton>(R.id.livePanelClose).setOnClickListener { hide() }
        root.findViewById<ImageButton>(R.id.livePanelExpand).setOnClickListener { onExpandRequested?.invoke() }
        root.findViewById<ImageButton>(R.id.livePanelRefresh).setOnClickListener { restart() }
        btnBack.setOnClickListener { navigateBack() }

        fabricView.onTap = { x, y -> IndsinPreviewManager.tap(x, y) }
        fabricView.onLongPressHandler = { x, y -> IndsinPreviewManager.longPress(x, y) }
        fabricView.onDoubleTapHandler = { x, y -> IndsinPreviewManager.doubleTap(x, y) }
        fabricView.onHoverHandler = { x, y, entering -> IndsinPreviewManager.hover(x, y, entering) }
        fabricView.onNavigate = { target -> navigateTo(target) }
        fabricView.onOpenUrl = { url ->
            try {
                activity.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(url)))
            } catch (_: Exception) {
                Toast.makeText(activity, activity.getString(R.string.indsin_link_open_failed, url), Toast.LENGTH_SHORT).show()
            }
        }

        // قياس السطح: يبدأ الجلسة بأول عرض حقيقي، ويُبلغ المحرك بارتفاع الشاشة (Dialog/Tooltip)،
        // ويعيد التخطيط إن تغيّر العرض فعلاً (تدوير الشاشة / تغيّر حجم اللوحة).
        surface.addOnLayoutChangeListener { _, left, top, right, bottom, _, _, _, _ ->
            val wDp = pxToDp(right - left)
            val hDp = pxToDp(bottom - top)
            if (wDp <= 0 || hDp <= 0) return@addOnLayoutChangeListener
            IndsinPreviewManager.setViewport(hDp.coerceAtLeast(120))
            val pending = pendingStart
            if (pending != null) {
                pendingStart = null
                deviceWidth = wDp.coerceAtLeast(200)
                IndsinPreviewManager.start(pending, deviceWidth)
            } else if (isShown && IndsinPreviewManager.isRunning && kotlin.math.abs(wDp - deviceWidth) >= 2) {
                deviceWidth = wDp.coerceAtLeast(200)
                IndsinPreviewManager.start(IndsinPreviewManager.lastSource, deviceWidth)
            }
        }
    }

    // ------------------------------------------------------------------ دورة حياة اللوحة

    /** يُظهر اللوحة ويبدأ جلسة جديدة بـ [source] (نفس دلالة زر Run: إعادة تنفيذ كاملة). */
    fun show(source: String, fileName: String?) {
        IndsinPreviewManager.panelVisible = true
        pageStack.clear()
        btnBack.visibility = View.GONE
        setTitle(fileName)
        hideError()
        txtStats.text = activity.getString(R.string.live_panel_waiting)
        IndsinPreviewManager.attach(this)
        val wasVisible = isShown
        root.visibility = View.VISIBLE
        val w = if (wasVisible && surface.width > 0) pxToDp(surface.width) else 0
        if (w > 0) {
            deviceWidth = w.coerceAtLeast(200)
            IndsinPreviewManager.start(source, deviceWidth)
        } else {
            pendingStart = source // يبدأ من مستمع التخطيط أعلاه بعد أول قياس
            surface.requestLayout()
        }
    }

    /** يُخفي اللوحة. [stopSession] = false يُبقي الجلسة حيّة (مثلاً عند التحويل إلى الشاشة الكاملة). */
    fun hide(stopSession: Boolean = true) {
        IndsinPreviewManager.panelVisible = false
        pendingStart = null
        stopTickers()
        root.visibility = View.GONE
        if (stopSession) {
            IndsinPreviewManager.detach(this)
            IndsinPreviewManager.stop()
        }
    }

    /**
     * يستعيد اللوحة بعد إعادة إنشاء المحرر (تدوير الشاشة) أو بعد إغلاق الشاشة الكاملة: لا يبدأ جلسة
     * جديدة بل يربط اللوحة بالجلسة الحيّة ويرسم آخر إطار محفوظ فوراً (حالة Warp سليمة).
     */
    fun resume() {
        if (!IndsinPreviewManager.panelVisible) return
        if (!IndsinPreviewManager.isRunning) {
            // الجلسة انتهت (قتلتها العملية مثلاً): أخفِ اللوحة بدل أن تبقى فارغة.
            IndsinPreviewManager.panelVisible = false
            root.visibility = View.GONE
            return
        }
        setTitle(IndsinPreviewManager.panelTitle)
        // عرض الجلسة الفعلي (لا الافتراضي 390): كي لا يعدّه مستمع التخطيط تغيّراً فيعيد التشغيل بلا داعٍ.
        deviceWidth = IndsinPreviewManager.rootWidth
        root.visibility = View.VISIBLE
        IndsinPreviewManager.attach(this)
    }

    /** يوقف المؤقتات ويفك الارتباط بلا إيقاف الجلسة (المحرر يخرج من الشاشة أو يُعاد إنشاؤه). */
    fun release() {
        stopTickers()
        IndsinPreviewManager.detach(this)
    }

    /** تعديل جديد من المحرر: يُلغي تنقّل الصفحات (المحرر هو المصدر الآن) ويحدّث العنوان. */
    fun onEditorEdit(fileName: String?) {
        if (pageStack.isNotEmpty()) {
            pageStack.clear()
            btnBack.visibility = View.GONE
        }
        setTitle(fileName)
    }

    private fun restart() {
        IndsinPreviewManager.start(IndsinPreviewManager.lastSource, deviceWidth)
        fabricView.clearInspection()
    }

    private fun setTitle(fileName: String?) {
        if (!fileName.isNullOrBlank()) {
            pageName = fileName
            IndsinPreviewManager.panelTitle = fileName
        }
        txtTitle.text = pageName
    }

    // ------------------------------------------------------------------ تنقّل الصفحات (onTap="navigate:...")

    private fun navigateTo(target: String) {
        val base = RinEngine.currentBaseDir()
        if (base.isBlank()) return
        var file = java.io.File(base, target)
        if (!file.isFile && !target.endsWith(".rin")) file = java.io.File(base, "$target.rin")
        val newSource = if (file.isFile) {
            try { file.readText() } catch (_: Throwable) { null }
        } else null
        if (newSource == null) {
            Toast.makeText(activity, activity.getString(R.string.indsin_navigate_missing_toast, target), Toast.LENGTH_SHORT).show()
            return
        }
        pageStack.addLast(pageName to IndsinPreviewManager.lastSource)
        btnBack.visibility = View.VISIBLE
        pageName = file.name
        txtTitle.text = pageName
        fabricView.clearInspection()
        IndsinPreviewManager.start(newSource, deviceWidth)
    }

    private fun navigateBack() {
        val (name, source) = pageStack.removeLastOrNull() ?: return
        pageName = name
        txtTitle.text = pageName
        if (pageStack.isEmpty()) btnBack.visibility = View.GONE
        fabricView.clearInspection()
        IndsinPreviewManager.start(source, deviceWidth)
    }

    // ------------------------------------------------------------------ استقبال إطارات المحرك

    override fun onFabricUpdated(resultJson: String, elapsedMs: Long) {
        if (!isShown) return
        val result = try { JSONObject(resultJson) } catch (_: Throwable) { null }
        if (result == null) {
            showError(activity.getString(R.string.indsin_internal_error))
            return
        }

        val fabric = result.optJSONObject("fabric")
        if (fabric == null) {
            // لا إطار سليم بعد: اعرض الخطأ ولوحة فارغة بدل إبقاء إطار قديم من صفحة أخرى.
            fabricView.setFabric(null, deviceWidth, 240)
            showError(formatError(result))
            txtStats.text = activity.getString(R.string.indsin_stats_error_only)
            return
        }

        sanitizeGeometry(fabric)
        val h = fabric.optDouble("h", 640.0).toInt().coerceAtLeast(120)
        val canvasInfo = result.optJSONObject("canvas")
        val canvasW = canvasInfo?.optInt("width", deviceWidth)?.takeIf { it > 0 } ?: deviceWidth
        fabricView.setFabric(fabric, canvasW, h, result.optJSONArray("overlays"), canvasInfo)

        if (result.has("snag") || result.has("error")) showError(formatError(result)) else hideError()

        val measured = result.optInt("strandsMeasured", -1)
        val cache = result.optInt("cacheHits", -1)
        txtStats.text = if (measured >= 0) {
            activity.getString(R.string.indsin_stats_format, measured, elapsedMs, cache)
        } else activity.getString(R.string.indsin_stats_idle)

        // pickMedia()/uploadMedia() وحقول الإدخال: المضيف وحده يفتح المنتقي/الحوار.
        result.optJSONObject("media")?.let { try { mediaHost.handle(it) } catch (_: Throwable) {} }
        result.optJSONObject("focus")?.let { showInputDialog(it) }

        val animating = result.optBoolean("animating", false)
        if (animating) startEffectsTicking() else stopEffectsTicking()
        val ticking = result.optBoolean("ticking", false)
        if (ticking && !animating) scheduleClockTick() else cancelClockTick()
    }

    private fun formatError(result: JSONObject): String {
        val line = result.optInt("line", 0)
        val message = result.optString("error", activity.getString(R.string.indsin_unknown_error))
        return if (line > 0) activity.getString(R.string.indsin_error_line_format, line, message)
        else activity.getString(R.string.indsin_error_format, message)
    }

    private fun showError(message: String) {
        txtError.text = message
        errorBanner.visibility = View.VISIBLE
    }

    private fun hideError() { errorBanner.visibility = View.GONE }

    /** يمنع وصول قيمة «غير محدود» الداخلية (1e9) إلى Canvas — نفس الحماية في IndsinPreviewActivity. */
    private fun sanitizeGeometry(node: JSONObject) {
        val viewport = IndsinPreviewManager.viewportHeight.coerceAtLeast(120)
        if (node.optDouble("h", 0.0) >= 900_000_000.0) node.put("h", viewport)
        if (node.optDouble("w", 0.0) >= 900_000_000.0) node.put("w", deviceWidth.toDouble())
        val children = node.optJSONArray("children") ?: return
        for (i in 0 until children.length()) children.optJSONObject(i)?.let(::sanitizeGeometry)
    }

    private fun showInputDialog(focus: JSONObject) {
        val bind = focus.optString("bind")
        if (bind.isEmpty()) return
        val edit = android.widget.EditText(activity)
        val secure = focus.optBoolean("secure")
        val multiline = focus.optBoolean("multiline")
        edit.hint = focus.optString("placeholder")
        edit.setText(focus.optString("value"))
        edit.inputType = when {
            secure -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            multiline -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
            focus.optString("inputType") == "email" -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS
            focus.optString("inputType") == "number" -> android.text.InputType.TYPE_CLASS_NUMBER
            else -> android.text.InputType.TYPE_CLASS_TEXT
        }
        edit.setSelection(edit.text.length)
        AlertDialog.Builder(activity)
            .setView(edit)
            .setPositiveButton(android.R.string.ok) { _, _ -> IndsinPreviewManager.setInput(bind, edit.text.toString()) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    // ------------------------------------------------------------------ مؤقتات: التأثيرات (كل إطار) والساعة (كل ثانية)

    private val clockHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private var clockTickScheduled = false
    private val clockTickRunnable = Runnable {
        clockTickScheduled = false
        IndsinPreviewManager.tick()
    }
    private fun scheduleClockTick() {
        if (clockTickScheduled) return
        clockTickScheduled = true
        clockHandler.postDelayed(clockTickRunnable, 1000L)
    }
    private fun cancelClockTick() {
        if (!clockTickScheduled) return
        clockTickScheduled = false
        clockHandler.removeCallbacks(clockTickRunnable)
    }

    private var effectsTickScheduled = false
    private val effectsFrameCallback = Choreographer.FrameCallback {
        effectsTickScheduled = false
        IndsinPreviewManager.tick()
    }
    private fun startEffectsTicking() {
        if (effectsTickScheduled) return
        effectsTickScheduled = true
        Choreographer.getInstance().postFrameCallback(effectsFrameCallback)
    }
    private fun stopEffectsTicking() {
        if (!effectsTickScheduled) return
        effectsTickScheduled = false
        Choreographer.getInstance().removeFrameCallback(effectsFrameCallback)
    }
    private fun stopTickers() { stopEffectsTicking(); cancelClockTick() }

    private fun pxToDp(px: Int): Int = (px / activity.resources.displayMetrics.density).toInt()
}
