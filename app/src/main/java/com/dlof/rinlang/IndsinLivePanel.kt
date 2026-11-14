package com.dlof.rinlang

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.Choreographer
import android.view.ContextThemeWrapper
import android.view.View
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.PopupMenu
import androidx.appcompat.widget.TooltipCompat
import androidx.core.content.FileProvider
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream

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

    /** انتقل المحرر إلى هذا السطر (شريط الخطأ أو مفتّش العناصر). */
    var onGoToLine: ((Int) -> Unit)? = null

    /** استُؤنف التحديث الحي بعد تجميد: على المضيف دفع نص المحرر الحالي إلى الجلسة. */
    var onResumeLive: (() -> Unit)? = null

    /** تغيّر حجم اللوحة (صغير/متوسط/كبير): على المضيف إعادة تطبيق وزنها في التخطيط. القيمة في تفضيل live_size. */
    var onSizeChanged: (() -> Unit)? = null

    /** التحديث الحي مجمَّد: التعديلات تبقى في المحرر ولا تُدفَع إلى الجلسة حتى الاستئناف. */
    var isFrozen: Boolean = false
        private set

    private val fabricView: IndsinFabricView = root.findViewById(R.id.livePanelFabric)
    private val surface: View = root.findViewById(R.id.livePanelHScroll)
    private val txtTitle: TextView = root.findViewById(R.id.livePanelTitle)
    private val txtStats: TextView = root.findViewById(R.id.livePanelStats)
    private val errorBanner: View = root.findViewById(R.id.livePanelErrorBanner)
    private val txtError: TextView = root.findViewById(R.id.livePanelError)
    private val btnBack: ImageButton = root.findViewById(R.id.livePanelBack)
    private val btnDevice: TextView = root.findViewById(R.id.livePanelDevice)
    private val btnZoomOut: TextView = root.findViewById(R.id.livePanelZoomOut)
    private val btnZoomIn: TextView = root.findViewById(R.id.livePanelZoomIn)
    private val txtZoom: TextView = root.findViewById(R.id.livePanelZoomLabel)
    private val btnGrid: TextView = root.findViewById(R.id.livePanelGrid)
    private val btnFreeze: TextView = root.findViewById(R.id.livePanelFreeze)
    private val btnShot: TextView = root.findViewById(R.id.livePanelShot)
    private val btnZoomFit: TextView = root.findViewById(R.id.livePanelZoomFit)
    private val btnSize: TextView = root.findViewById(R.id.livePanelSize)
    private val btnMore: TextView = root.findViewById(R.id.livePanelMore)
    private var lastFrameJson = ""

    /** تفضيلات المعاينة تبقى بين الجلسات (التكبير، الشبكة، عرض الجهاز، الخلفية). */
    private val prefs = activity.getSharedPreferences("indsin_prefs", Context.MODE_PRIVATE)

    /** عرض جهاز ثابت (dp) اختاره المستخدم؛ null = يملأ عرض اللوحة تلقائياً. */
    private var fixedWidth: Int? = null

    private var lastErrorLine = 0
    private var lastErrorText = ""

    /** منتقي الوسائط: يُسجّل launchers لذا يجب إنشاء اللوحة قبل STARTED (أي في onCreate). */
    private val mediaHost = IndsinMediaHost(activity)

    private var deviceWidth = 390
    private var pageName = "main.rin"
    private val pageStack = ArrayDeque<Pair<String, String>>()

    /** مصدر ينتظر أول قياس حقيقي لعرض السطح كي تبدأ الجلسة بعرضه الفعلي (dp). */
    private var pendingStart: String? = null

    val isShown: Boolean get() = root.visibility == View.VISIBLE

    /** اللوحة ظاهرة لكنها تنتظر أول قياس قبل بدء الجلسة: التعديلات في هذه الفجوة يجب ألا تضيع. */
    val hasPendingStart: Boolean get() = pendingStart != null

    init {
        restoreToolbarState()
        btnDevice.setOnClickListener { showDeviceMenu(it) }
        btnZoomOut.setOnClickListener { fabricView.zoom = fabricView.zoom - 0.1f }
        btnZoomIn.setOnClickListener { fabricView.zoom = fabricView.zoom + 0.1f }
        txtZoom.setOnClickListener { fabricView.zoom = 1f }
        fabricView.onZoomChanged = { z -> paintZoom(z) }
        btnGrid.setOnClickListener {
            fabricView.showGrid = !fabricView.showGrid
            prefs.edit().putBoolean("live_grid", fabricView.showGrid).apply()
            paintToggles()
        }
        btnFreeze.setOnClickListener { setFrozen(!isFrozen) }
        btnZoomFit.setOnClickListener { fitZoomToWidth() }
        btnSize.setOnClickListener { cycleSize() }
        btnMore.setOnClickListener { showMoreMenu(it) }
        btnShot.setOnClickListener { captureAndShare() }
        errorBanner.setOnClickListener { if (lastErrorLine > 0) onGoToLine?.invoke(lastErrorLine) }
        errorBanner.setOnLongClickListener { copyToClipboard(lastErrorText); true }
        TooltipCompat.setTooltipText(errorBanner, activity.getString(R.string.live_pro_err_hint))
        fabricView.onInspect = { node -> showInspector(node) }

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
                deviceWidth = fixedWidth ?: wDp.coerceAtLeast(200)
                updateDeviceLabel()
                IndsinPreviewManager.start(pending, deviceWidth)
            } else if (isShown && IndsinPreviewManager.isRunning && fixedWidth == null && kotlin.math.abs(wDp - deviceWidth) >= 2) {
                deviceWidth = wDp.coerceAtLeast(200)
                updateDeviceLabel()
                IndsinPreviewManager.start(IndsinPreviewManager.lastSource, deviceWidth)
            }
        }
    }

    // ------------------------------------------------------------------ دورة حياة اللوحة

    /** يُظهر اللوحة ويبدأ جلسة جديدة بـ [source] (نفس دلالة زر Run: إعادة تنفيذ كاملة). */
    fun show(source: String, fileName: String?) {
        IndsinPreviewManager.panelVisible = true
        if (isFrozen) setFrozen(false, notify = false) // تشغيل صريح (Run) = جلسة جديدة بالنص الحالي
        pageStack.clear()
        btnBack.visibility = View.GONE
        setTitle(fileName)
        hideError()
        fabricView.clearInspection() // جلسة جديدة: أعد ضبط مؤقت الانتقال التلقائي لصفحة البداية
        txtStats.text = activity.getString(R.string.live_panel_waiting)
        IndsinPreviewManager.attach(this)
        val wasVisible = isShown
        root.visibility = View.VISIBLE
        val w = if (wasVisible && surface.width > 0) pxToDp(surface.width) else 0
        if (w > 0) {
            deviceWidth = fixedWidth ?: w.coerceAtLeast(200)
            updateDeviceLabel()
            IndsinPreviewManager.start(source, deviceWidth)
        } else {
            pendingStart = source // يبدأ من مستمع التخطيط أعلاه بعد أول قياس
            surface.requestLayout()
        }
    }

    /** يُخفي اللوحة. [stopSession] = false يُبقي الجلسة حيّة (مثلاً عند التحويل إلى الشاشة الكاملة). */
    fun hide(stopSession: Boolean = true) {
        if (stopSession && isFrozen) setFrozen(false, notify = false)
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
        updateDeviceLabel()
        root.visibility = View.VISIBLE
        IndsinPreviewManager.attach(this)
    }

    /** يوقف المؤقتات ويفك الارتباط بلا إيقاف الجلسة (المحرر يخرج من الشاشة أو يُعاد إنشاؤه). */
    fun release() {
        stopTickers()
        IndsinPreviewManager.detach(this)
    }

    /** تعديل جديد من المحرر: يُلغي تنقّل الصفحات (المحرر هو المصدر الآن) ويحدّث العنوان. */
    fun onEditorEdit(fileName: String?, source: String? = null) {
        // قبل أول قياس لا توجد جلسة تُدفَع إليها التعديلات: حدّث المصدر المنتظِر كي لا تبدأ الجلسة بنص قديم.
        if (source != null && pendingStart != null) pendingStart = source
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
        lastFrameJson = resultJson
        val result = try { JSONObject(resultJson) } catch (_: Throwable) { null }
        if (result == null) {
            showError(activity.getString(R.string.indsin_internal_error))
            return
        }

        val fabric = result.optJSONObject("fabric")
        if (fabric == null) {
            // لا إطار سليم بعد: اعرض الخطأ ولوحة فارغة بدل إبقاء إطار قديم من صفحة أخرى.
            fabricView.setFabric(null, deviceWidth, 240)
            showError(formatError(result), result.optInt("line", 0))
            txtStats.text = activity.getString(R.string.indsin_stats_error_only)
            return
        }

        sanitizeGeometry(fabric)
        val h = fabric.optDouble("h", 640.0).toInt().coerceAtLeast(120)
        val canvasInfo = result.optJSONObject("canvas")
        val canvasW = canvasInfo?.optInt("width", deviceWidth)?.takeIf { it > 0 } ?: deviceWidth
        fabricView.setFabric(fabric, canvasW, h, result.optJSONArray("overlays"), canvasInfo)

        if (result.has("snag") || result.has("error")) showError(formatError(result), result.optInt("line", 0))
        else if ((fabric.optJSONArray("children")?.length() ?: 0) == 0 && fabric.optString("kind") != "TEXT")
            showError(activity.getString(R.string.indsin_empty_screen_hint)) // إطار أسود بلا تفسير → أخبر المستخدم السبب
        else hideError()

        val measured = result.optInt("strandsMeasured", -1)
        val cache = result.optInt("cacheHits", -1)
        val statsText = if (measured >= 0) {
            activity.getString(R.string.indsin_stats_format, measured, elapsedMs, cache)
        } else activity.getString(R.string.indsin_stats_idle)
        txtStats.text = if (isFrozen) "❚❚ $statsText" else statsText

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

    private fun showError(message: String, line: Int = 0) {
        lastErrorText = message
        lastErrorLine = line
        txtError.text = message
        errorBanner.visibility = View.VISIBLE
    }

    private fun hideError() {
        lastErrorLine = 0
        errorBanner.visibility = View.GONE
    }

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
        when (focus.optString("inputType")) {
            "date" -> {
                val cal = java.util.Calendar.getInstance()
                Regex("^(\\d{4})-(\\d{2})-(\\d{2})$").find(focus.optString("value"))?.let {
                    cal.set(it.groupValues[1].toInt(), it.groupValues[2].toInt() - 1, it.groupValues[3].toInt()) }
                android.app.DatePickerDialog(activity, { _, y, m, d ->
                    IndsinPreviewManager.setInput(bind, String.format(java.util.Locale.US, "%04d-%02d-%02d", y, m + 1, d))
                }, cal.get(java.util.Calendar.YEAR), cal.get(java.util.Calendar.MONTH), cal.get(java.util.Calendar.DAY_OF_MONTH)).show()
                return
            }
            "time" -> {
                var h = 9; var mi = 0
                Regex("^(\\d{2}):(\\d{2})").find(focus.optString("value"))?.let { h = it.groupValues[1].toInt(); mi = it.groupValues[2].toInt() }
                android.app.TimePickerDialog(activity, { _, hh, mm ->
                    IndsinPreviewManager.setInput(bind, String.format(java.util.Locale.US, "%02d:%02d", hh, mm))
                }, h, mi, true).show()
                return
            }
        }
        val edit = android.widget.EditText(activity)
        val secure = focus.optBoolean("secure")
        val multiline = focus.optBoolean("multiline")
        edit.hint = focus.optString("placeholder")
        edit.setText(focus.optString("value"))
        edit.inputType = when {
            secure -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            multiline -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
            focus.optString("inputType") == "email" -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS
            focus.optString("inputType") == "number" ->
                android.text.InputType.TYPE_CLASS_NUMBER or android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL or android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
            focus.optString("inputType") == "tel" -> android.text.InputType.TYPE_CLASS_PHONE
            focus.optString("inputType") == "url" -> android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI
            else -> android.text.InputType.TYPE_CLASS_TEXT
        }
        val maxLen = focus.optInt("maxLength", 0)
        if (maxLen > 0) edit.filters = arrayOf<android.text.InputFilter>(android.text.InputFilter.LengthFilter(maxLen))
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

    // ------------------------------------------------------------------ شريط الأدوات الاحترافي

    private fun restoreToolbarState() {
        fabricView.zoom = prefs.getFloat("live_zoom", 1f)
        fabricView.showGrid = prefs.getBoolean("live_grid", false)
        fabricView.showSafeArea = prefs.getBoolean("live_safe_area", false)
        fixedWidth = prefs.getInt("live_fixed_width", 0).takeIf { it >= 200 }
        val bg = prefs.getString("live_bg", "") ?: ""
        if (bg.isNotEmpty() && IndsinPreviewManager.defaultBackground.isEmpty()) IndsinPreviewManager.setDefaultBackground(bg)
        paintZoom(fabricView.zoom)
        paintToggles()
        updateDeviceLabel()
        paintSize()
        txtStats.visibility = if (prefs.getBoolean("live_stats", true)) View.VISIBLE else View.GONE
    }

    private fun paintZoom(z: Float) {
        txtZoom.text = activity.getString(R.string.indsin_zoom_percent_format, (z * 100).toInt())
        prefs.edit().putFloat("live_zoom", z).apply()
    }

    private fun paintToggles() {
        btnGrid.alpha = if (fabricView.showGrid) 1f else 0.55f
        btnFreeze.alpha = if (isFrozen) 1f else 0.55f
    }

    private fun updateDeviceLabel() {
        btnDevice.text = if (fixedWidth == null) "⇔ $deviceWidth" else "$deviceWidth"
    }

    /** قائمة الجهاز: ملء اللوحة، أعراض عمودية، ثم أفقية. الخلفية وبقية الخيارات في قائمة «المزيد». */
    private fun showDeviceMenu(anchor: View) {
        val popup = PopupMenu(darkThemed(), anchor)
        val menu = popup.menu
        menu.add(0, -1, 0, activity.getString(R.string.indsin_device_fit_screen))
        IndsinPreviewActivity.DEVICE_WIDTHS.forEachIndexed { i, w ->
            menu.add(0, i, i + 1, activity.getString(R.string.indsin_device_width_format, w))
        }
        val land = menu.addSubMenu(0, LANDSCAPE_BASE - 1, 50, activity.getString(R.string.live_pro_landscape))
        LANDSCAPE_WIDTHS.forEachIndexed { i, w ->
            land.add(0, LANDSCAPE_BASE + i, i, activity.getString(R.string.indsin_device_width_format, w))
        }
        popup.setOnMenuItemClickListener { item ->
            val id = item.itemId
            when {
                id == -1 -> applyDeviceChoice(null)
                id >= LANDSCAPE_BASE -> applyDeviceChoice(LANDSCAPE_WIDTHS.getOrNull(id - LANDSCAPE_BASE))
                else -> applyDeviceChoice(IndsinPreviewActivity.DEVICE_WIDTHS.getOrNull(id))
            }
            true
        }
        popup.show()
    }

    /** «المزيد»: الأقل استعمالاً — إعادة التشغيل، نسخ JSON، حدود المنطقة الآمنة، الإحصاءات، الخلفية، إعادة الضبط. */
    private fun showMoreMenu(anchor: View) {
        val popup = PopupMenu(darkThemed(), anchor)
        val menu = popup.menu
        menu.add(0, MORE_RESTART, 0, activity.getString(R.string.indsin_restart_session))
        menu.add(0, MORE_COPY_JSON, 1, activity.getString(R.string.live_pro_copy_json))
        menu.add(0, MORE_SAFE, 2, activity.getString(R.string.live_pro_safe_area)).apply {
            isCheckable = true; isChecked = fabricView.showSafeArea
        }
        menu.add(0, MORE_STATS, 3, activity.getString(R.string.live_pro_show_stats)).apply {
            isCheckable = true; isChecked = txtStats.visibility == View.VISIBLE
        }
        val bgBase = IndsinPreviewActivity.BG_MENU_ID
        val bgMenu = menu.addSubMenu(0, bgBase, 4, activity.getString(R.string.indsin_background_menu))
        bgMenu.add(0, bgBase + 1, 0, activity.getString(R.string.indsin_background_theme))
        IndsinPreviewActivity.BACKGROUND_PRESETS.forEachIndexed { i, (nameRes, _) ->
            bgMenu.add(0, bgBase + 2 + i, i + 1, activity.getString(nameRes))
        }
        menu.add(0, MORE_RESET, 5, activity.getString(R.string.live_pro_reset))
        popup.setOnMenuItemClickListener { item ->
            when {
                item.itemId == MORE_RESTART -> restart()
                item.itemId == MORE_COPY_JSON -> {
                    if (lastFrameJson.isNotBlank()) {
                        copyToClipboard(lastFrameJson, activity.getString(R.string.live_pro_json_copied))
                    }
                }
                item.itemId == MORE_SAFE -> {
                    fabricView.showSafeArea = !fabricView.showSafeArea
                    prefs.edit().putBoolean("live_safe_area", fabricView.showSafeArea).apply()
                }
                item.itemId == MORE_STATS -> {
                    val show = txtStats.visibility != View.VISIBLE
                    txtStats.visibility = if (show) View.VISIBLE else View.GONE
                    prefs.edit().putBoolean("live_stats", show).apply()
                }
                item.itemId == MORE_RESET -> resetPreviewSettings()
                item.itemId > bgBase -> {
                    val color = IndsinPreviewActivity.BACKGROUND_PRESETS.getOrNull(item.itemId - bgBase - 2)?.second ?: ""
                    prefs.edit().putString("live_bg", color).apply()
                    IndsinPreviewManager.setDefaultBackground(color)
                }
            }
            true
        }
        popup.show()
    }

    private fun darkThemed() =
        ContextThemeWrapper(activity, com.google.android.material.R.style.ThemeOverlay_MaterialComponents_Dark)

    /** يطابق التكبير عرض اللوحة الفعلي: (عرض اللوحة dp) ÷ (عرض الجهاز dp). */
    private fun fitZoomToWidth() {
        val panelDp = pxToDp(surface.width)
        if (panelDp <= 0 || deviceWidth <= 0) return
        fabricView.zoom = panelDp.toFloat() / deviceWidth.toFloat()
    }

    private fun cycleSize() {
        val next = (prefs.getInt("live_size", 1) + 1) % 3
        prefs.edit().putInt("live_size", next).apply()
        paintSize()
        onSizeChanged?.invoke()
        val name = when (next) { 0 -> R.string.live_pro_size_s; 2 -> R.string.live_pro_size_l; else -> R.string.live_pro_size_m }
        Toast.makeText(activity, activity.getString(R.string.live_pro_size_toast, activity.getString(name)), Toast.LENGTH_SHORT).show()
    }

    private fun paintSize() {
        btnSize.text = when (prefs.getInt("live_size", 1)) { 0 -> "S"; 2 -> "L"; else -> "M" }
    }

    /** يعيد كل تفضيلات المعاينة إلى الافتراضي (الجلسة الحالية تُعاد بعرض ملء اللوحة). */
    private fun resetPreviewSettings() {
        prefs.edit().remove("live_zoom").remove("live_grid").remove("live_safe_area")
            .remove("live_fixed_width").remove("live_bg").remove("live_stats").remove("live_size").apply()
        fabricView.showGrid = false
        fabricView.showSafeArea = false
        fabricView.zoom = 1f
        txtStats.visibility = View.VISIBLE
        paintToggles(); paintSize()
        IndsinPreviewManager.setDefaultBackground("")
        applyDeviceChoice(null)
        onSizeChanged?.invoke()
        Toast.makeText(activity, activity.getString(R.string.live_pro_reset_done), Toast.LENGTH_SHORT).show()
    }

    /** [width] = null يعني «املأ عرض اللوحة». تغيير العرض يعيد تخطيطاً كاملاً (وتبدأ جلسة جديدة). */
    private fun applyDeviceChoice(width: Int?) {
        fixedWidth = width
        prefs.edit().putInt("live_fixed_width", width ?: 0).apply()
        val fit = pxToDp(surface.width).coerceAtLeast(200)
        deviceWidth = width ?: fit
        updateDeviceLabel()
        if (IndsinPreviewManager.isRunning) {
            IndsinPreviewManager.start(IndsinPreviewManager.lastSource, deviceWidth)
            fabricView.clearInspection()
        }
    }

    private fun setFrozen(frozen: Boolean, notify: Boolean = true) {
        if (isFrozen == frozen) return
        isFrozen = frozen
        paintToggles()
        if (notify) {
            Toast.makeText(
                activity,
                activity.getString(if (frozen) R.string.live_pro_frozen_on else R.string.live_pro_frozen_off),
                Toast.LENGTH_SHORT
            ).show()
        }
        if (!frozen && notify) onResumeLive?.invoke()
    }

    private fun copyToClipboard(text: String, toast: String? = null) {
        if (text.isBlank()) return
        val cm = activity.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
        cm.setPrimaryClip(ClipData.newPlainText("indsin", text))
        Toast.makeText(activity, toast ?: activity.getString(R.string.live_pro_copied), Toast.LENGTH_SHORT).show()
    }

    /** يلتقط الإطار المرسوم حالياً (بالتكبير والخلفية) كـ PNG ويشاركه. الرسم على الخيط الرئيسي، والضغط/الكتابة في خيط خلفي. */
    private fun captureAndShare() {
        val v = fabricView
        val w = v.width
        val h = v.height
        if (w <= 0 || h <= 0 || w.toLong() * h > 24_000_000L) {
            Toast.makeText(activity, activity.getString(R.string.live_pro_shot_failed), Toast.LENGTH_SHORT).show()
            return
        }
        val bmp = try {
            Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).also { v.draw(Canvas(it)) }
        } catch (_: Throwable) {
            Toast.makeText(activity, activity.getString(R.string.live_pro_shot_failed), Toast.LENGTH_SHORT).show()
            return
        }
        Thread {
            val uri = try {
                val dir = File(activity.cacheDir, "indsin_shots").apply { mkdirs() }
                val cutoff = System.currentTimeMillis() - 24L * 3600 * 1000
                dir.listFiles()?.filter { it.lastModified() < cutoff }?.forEach { it.delete() }
                val f = File(dir, "preview_${System.currentTimeMillis()}.png")
                FileOutputStream(f).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
                FileProvider.getUriForFile(activity, "${activity.packageName}.fileprovider", f)
            } catch (_: Throwable) { null } finally { bmp.recycle() }
            activity.runOnUiThread {
                if (activity.isFinishing || activity.isDestroyed) return@runOnUiThread
                if (uri == null) {
                    Toast.makeText(activity, activity.getString(R.string.live_pro_shot_failed), Toast.LENGTH_SHORT).show()
                    return@runOnUiThread
                }
                val send = Intent(Intent.ACTION_SEND).apply {
                    type = "image/png"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                try {
                    activity.startActivity(Intent.createChooser(send, activity.getString(R.string.live_pro_shot_share)))
                } catch (_: Exception) {
                    Toast.makeText(activity, activity.getString(R.string.live_pro_shot_failed), Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    /** ضغطة مطوّلة على عنصر: خصائصه مع نسخها أو الانتقال إلى سطره في المحرر. */
    private fun showInspector(node: JSONObject?) {
        if (node == null) return
        val kind = node.optString("kind", "?")
        val name = node.optString("name", "")
        val line = node.optInt("line", 0)
        val sb = StringBuilder()
        sb.append("x: ").append(node.optDouble("x", 0.0).toInt())
            .append("   y: ").append(node.optDouble("y", 0.0).toInt())
            .append("   w: ").append(node.optDouble("w", 0.0).toInt())
            .append("   h: ").append(node.optDouble("h", 0.0).toInt())
        node.optJSONObject("attrs")?.let { attrs ->
            val keys = attrs.keys()
            while (keys.hasNext()) { val k = keys.next(); sb.append('\n').append(k).append(": ").append(attrs.optString(k)) }
        }
        val body = sb.toString()
        val builder = AlertDialog.Builder(activity)
            .setTitle(activity.getString(R.string.live_pro_inspector_title, kind, name, line))
            .setMessage(body)
            .setPositiveButton(R.string.live_pro_copy) { _, _ -> copyToClipboard(body) }
            .setNegativeButton(R.string.live_pro_close, null)
            .setOnDismissListener { fabricView.clearInspection() }
        if (line > 0) builder.setNeutralButton(activity.getString(R.string.live_pro_goto_line, line)) { _, _ -> onGoToLine?.invoke(line) }
        builder.show()
    }

    private companion object {
        const val MORE_RESTART = 901
        const val MORE_COPY_JSON = 902
        const val MORE_SAFE = 903
        const val MORE_STATS = 904
        const val MORE_RESET = 905
        const val LANDSCAPE_BASE = 2000
        val LANDSCAPE_WIDTHS = listOf(667, 844, 932)
    }
}
