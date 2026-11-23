package com.dlof.rinlang

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.content.res.ColorStateList
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.text.InputType
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.TextPaint
import android.text.method.LinkMovementMethod
import android.text.style.BackgroundColorSpan
import android.text.style.LineHeightSpan
import android.text.style.RelativeSizeSpan
import android.text.style.TypefaceSpan
import android.text.style.ClickableSpan
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.text.style.UnderlineSpan
import android.util.AttributeSet
import android.view.Gravity
import android.view.KeyEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat

/**
 * terminal داخل المحرر: سجل مخرجات حيّ + سطر واحد في الأسفل له ثلاثة أوضاع:
 *
 *  - **shell** (`$`): أوامر `help run stop clear history version reset session grep goto share copy`، و`!!`/`!N` لإعادة أمر سابق، وأي نص آخر
 *    يُنفَّذ كشيفرة Rin ضمن **جلسة REPL** (انظر [RinReplSession]): التصريحات تبقى بين الأوامر،
 *    والتعبير بلا `;` (مثل `1 + 2`) يُطبع ناتجه، وأمر ناقص الأقواس (`fun f() {`) يكمل على أسطر `…`.
 *  - **continuation** (`…`): نفس السطر لكن بانتظار إكمال أمر متعدد الأسطر.
 *  - **input** (`›`، إطار مضيء): برنامج Rin متوقف عند input/inputNumber/confirm/choose؛ ما يُكتب يُسلَّم له.
 *
 * ميزات الواجهة: ⇥ إكمال تلقائي (أوامر + كلمات Rin + أسماء الجلسة) · ↑/↓ تاريخ (محفوظ بين الجلسات) ·
 * ^C إلغاء السؤال/الأمر المعلّق أو إيقاف التشغيل الجاري (كذلك أمر `stop`) ·
 * قرص بإصبعين لتغيير الخط (يُحفظ) · النقر على سطر `[Error line N]` يقفز لذلك السطر في المحرر.
 *
 * السجل والجلسة في [RinTerminal] (لا يضيعان بتدوير الشاشة)؛ هذا الصنف عرض فقط.
 */
class RinTerminalView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : LinearLayout(context, attrs) {

    /** أمر `run`: تشغيل الملف المفتوح في المحرر. */
    var onRunRequested: (() -> Unit)? = null

    /**
     * أمر Rin جاهز للتنفيذ ([source] يشمل prelude الجلسة). يعيد false إن امتلأ الطابور.
     * [onSuccess] يُستدعى عند نجاحه (لحفظ تصريحات الجلسة).
     */
    var onExecuteRequested: ((source: String, onSuccess: () -> Unit) -> Boolean)? = null

    /** النقر على خطأ `[Error line N]`: انتقل للسطر N في المحرر. */
    var onGoToLine: ((Int) -> Unit)? = null
    /** يفتح منتقي الصور؛ تضبطه MainActivity وتعيد النتيجة عبر [setBackgroundFromUri]. */
    var onPickBackground: (() -> Unit)? = null

    private val prefs = context.getSharedPreferences("rin_terminal", Context.MODE_PRIVATE)
    private var themeId: String = RinTheme.byId(prefs.getString(KEY_THEME, "rin")).also { setThemeColors(it) }.id
    private var spacingName: String = (prefs.getString(KEY_SPACING, RinSizes.DEFAULT_SPACING) ?: RinSizes.DEFAULT_SPACING)
        .takeIf { it in RinSizes.SPACING } ?: RinSizes.DEFAULT_SPACING
    private val lineRatio: Float get() = RinSizes.SPACING[spacingName] ?: 0.15f
    private val chips = ArrayList<TextView>()
    private var bgBitmap: Bitmap? = null
    private var bgMode: String = prefs.getString(KEY_BG_MODE, "none") ?: "none"   // none | image | space | anim
    private var bgAnimated: Boolean = prefs.getBoolean(KEY_BG_ANIM, true)
    private var live: LiveBackground? = null
    private var bgDim: Int = prefs.getInt(KEY_BG_DIM, 55).coerceIn(0, 90)
    private val divider = View(context)
    private val scroll = ScrollView(context)
    private val screen = TextView(context)
    private val promptLabel = TextView(context)
    private val field = EditText(context)
    private val btnCtrlC = chip("^C")
    private val btnTab = chipIcon(R.drawable.rs_ic_tab, 16)
    private val btnUp = chipIcon(R.drawable.rs_ic_arrow_up, 16)
    private val btnDown = chipIcon(R.drawable.rs_ic_arrow_down, 16)
    private val btnSend = chipIcon(R.drawable.rs_ic_enter, 16)
    private val inputBox = LinearLayout(context)
    private val header = LinearLayout(context)
    private val activity = TextView(context)   // سطر التنفيذ الحيّ (مؤشر دوّار + الزمن) فوق سطر الأوامر
    private val ticker = Handler(Looper.getMainLooper())
    private var tickerOn = false
    private var runStartedAt = 0L
    private val tickRunnable = object : Runnable {
        override fun run() {
            if (!isRunningNow()) { stopTicker(); return }
            val el = (SystemClock.elapsedRealtime() - runStartedAt) / 1000.0
            val frame = SPINNER[((SystemClock.elapsedRealtime() / 80) % SPINNER.size).toInt()]
            activity.text = String.format(java.util.Locale.US, "%s Running… %.1fs", frame, el)
            ticker.postDelayed(this, 80)
        }
    }

    private val statusDot = TextView(context)
    private val statusLabel = TextView(context)
    private val statusInfo = TextView(context)

    /** غير null = برنامج ينتظر جواباً الآن (وضع input). */
    private var deliver: ((String?) -> Unit)? = null
    private var historyIndex = -1
    private var lastVersion = -1L
    private var stickToBottom = true
    private var fontSp = prefs.getFloat(KEY_FONT, 12.5f).coerceIn(FONT_MIN, FONT_MAX)
    private var lastTabPrefix: String? = null

    private val changeListener: () -> Unit = { refresh() }

    private val scaleDetector = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(d: ScaleGestureDetector): Boolean {
            val next = (fontSp * d.scaleFactor).coerceIn(FONT_MIN, FONT_MAX)
            if (kotlin.math.abs(next - fontSp) >= 0.05f) {
                fontSp = next
                screen.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, fontSp)
                refresh(true)   // مقياس الرسومات يعتمد على حجم الخط
            }
            return true
        }
        override fun onScaleEnd(d: ScaleGestureDetector) {
            prefs.edit().putFloat(KEY_FONT, fontSp).apply()
        }
    })

    // مهم: يجب تعريفهما قبل كتلة init لأن init تستدعي refresh() ← decorate() التي تستخدمهما.
    // (كانا بعد init فكانا null وقت البناء → NullPointerException عند فتح التطبيق.)
    /** خط يحوي كل رموز الصناديق والكتل بعرض موحّد 0.6em — لا يعتمد على خطوط احتياطية متفاوتة العرض. */
    private val artTypeface: Typeface by lazy {
        try { context.resources.getFont(R.font.rin_code_regular) } catch (t: Throwable) { Typeface.MONOSPACE }
    }

    private val artPaint by lazy { TextPaint(TextPaint.ANTI_ALIAS_FLAG).apply { typeface = artTypeface } }

    init {
        orientation = VERTICAL
        layoutDirection = LAYOUT_DIRECTION_LTR   // الموجّه والأزرار ثابتة؛ اتجاه كل سطر نصي يُحدَّد بحسب لغته
        bgBitmap = TerminalBackground.load(context)
        applyBackground()

        screen.apply {
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, fontSp)
            typeface = Typeface.MONOSPACE
            setTextColor(FG)
            setLineSpacing(0f, 1f)   // التباعد يُضاف لكل فقرة نصية على حدة (انظر decorate) كي لا تنقطع أطر الرسومات
            textDirection = View.TEXT_DIRECTION_FIRST_STRONG
            setPadding(dp(12), dp(8), dp(12), dp(8))
            movementMethod = LinkMovementMethod.getInstance()   // أسطر الأخطاء قابلة للنقر
            highlightColor = 0x00000000
            setOnClickListener { focusInput() }
        }
        screen.addOnLayoutChangeListener { _, l, _, r, _, ol, _, oldR, _ ->
            if (r - l != oldR - ol) refresh(true)   // تغيّر العرض (تدوير/لوحة مفاتيح) → أعد ملاءمة الرسومات
        }
        scroll.apply {
            isFillViewport = true
            isVerticalScrollBarEnabled = true
            addView(screen, android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT
            ))
            setOnScrollChangeListener { _, _, scrollY, _, _ ->
                val content = getChildAt(0) ?: return@setOnScrollChangeListener
                // "ملتصق بالأسفل" ما دام المستخدم لم يصعد أكثر من ~48dp؛ عندها تتبّع المخرجات الجديدة.
                stickToBottom = content.bottom - (height + scrollY) < dp(48)
            }
            // قرص بإصبعين = تكبير/تصغير الخط. نمرّر الحدث للكاشف دائماً ونستهلكه فقط أثناء القرص.
            setOnTouchListener { _, ev ->
                scaleDetector.onTouchEvent(ev)
                scaleDetector.isInProgress
            }
        }
        buildHeader()
        addView(header, 0, LayoutParams(LayoutParams.MATCH_PARENT, dp(28)))
        addView(divider.apply { setBackgroundColor(BORDER) }, 1, LayoutParams(LayoutParams.MATCH_PARENT, dp(1)))
        addView(scroll, LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f))

        promptLabel.apply {
            typeface = Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12f)
            setSingleLine(true)
            ellipsize = android.text.TextUtils.TruncateAt.MIDDLE
            maxWidth = dp(200)
            setPadding(dp(10), 0, dp(4), 0)
        }
        field.apply {
            background = null
            typeface = Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 13f)
            setTextColor(FG)
            setHintTextColor(DIM)
            setSingleLine(true)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_SEND or EditorInfo.IME_FLAG_NO_EXTRACT_UI
            textDirection = View.TEXT_DIRECTION_FIRST_STRONG
            setPadding(0, dp(10), 0, dp(10))
            setOnEditorActionListener { _, actionId, event ->
                val enter = event != null && event.keyCode == KeyEvent.KEYCODE_ENTER && event.action == KeyEvent.ACTION_DOWN
                if (actionId == EditorInfo.IME_ACTION_SEND || enter) { submit(); true } else false
            }
        }

        btnCtrlC.setOnClickListener { cancelOrClear() }
        btnTab.setOnClickListener { complete() }
        btnUp.setOnClickListener { stepHistory(+1) }
        btnDown.setOnClickListener { stepHistory(-1) }
        btnSend.setOnClickListener { submit() }

        inputBox.apply {
            orientation = HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(promptLabel, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
            addView(field, LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f))
            for (b in listOf(btnCtrlC, btnTab, btnUp, btnDown, btnSend)) {
                addView(b, LayoutParams(LayoutParams.WRAP_CONTENT, dp(30)).apply { marginEnd = dp(4) })
            }
            setPadding(0, 0, dp(4), 0)
        }
        activity.apply {
            typeface = Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12f)
            setTextColor(ACCENT)
            setPadding(dp(12), dp(2), dp(12), dp(2))
            visibility = View.GONE
        }
        addView(activity, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))
        addView(inputBox, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT).apply {
            setMargins(dp(8), dp(4), dp(8), dp(8))
        })

        updateMode()
        refresh(force = true)
    }

    // ---- دورة الحياة ----

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        RinTerminal.onChanged = changeListener
        live?.start()
        RinTerminal.buffer.restoreHistory(loadHistory())
        updateMode()
        refresh(force = true)
    }

    /** إيقاف الحركة عند إخفاء النافذة (قفل الشاشة/الخروج) توفيراً للبطارية، واستئنافها عند الرجوع. */
    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        if (visibility == View.VISIBLE) live?.start() else live?.stop()
    }

    override fun onDetachedFromWindow() {
        if (RinTerminal.onChanged === changeListener) RinTerminal.onChanged = null
        live?.stop()
        // السؤال المعلّق لا يُلغى هنا: [RinInputBridge] يعيد عرضه على الواجهة التالية.
        if (fsDialog != null) fsKeep = deliver   // إغلاق نافذة ملء الشاشة يفصل العرض مؤقتاً؛ نحفظ السؤال المعلّق
        deliver = null
        stopTicker()
        super.onDetachedFromWindow()
    }

    private fun restyleChip(v: TextView) {
        val shape = GradientDrawable().apply {
            cornerRadius = dp(8).toFloat()
            setColor(CHIP_BG)
        }
        v.background = RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), shape, null)   // تأثير لمس واضح
    }

    // ---------------------------------------------------------------- صورة الخلفية

    /** خلفية الطرفية: لون السمة، أو صورة المستخدم بقصّ مركزي وتعتيم قابل للضبط. */
    private fun applyBackground() {
        live?.stop(); live = null
        val bmp = bgBitmap
        background = when {
            bgMode == "space" -> {
                // السمة الفاتحة: طبقة تغطية بلون السمة (≥80%) كي يبقى النص الداكن مقروءاً فوق الفضاء
                val light = RinTheme.luminance(BG) > RinTheme.LIGHT_THRESHOLD
                StarfieldDrawable(context, if (light) maxOf(80, bgDim) else (bgDim * 0.6f).toInt(), if (light) BG else android.graphics.Color.BLACK)
                    .also { attachLive(it) }
            }
            bgMode == "anim" -> {
                val anim = TerminalBackground.loadAnimated(context)
                if (anim != null) AnimatedCropDrawable(context, anim, BG, bgDim).also { attachLive(it) }
                else android.graphics.drawable.ColorDrawable(BG)
            }
            bgMode == "image" && bmp != null -> CropDimDrawable(bmp, BG, bgDim)
            else -> android.graphics.drawable.ColorDrawable(BG)
        }
    }

    private fun attachLive(d: LiveBackground) {
        live = d; d.setAnimated(bgAnimated); if (isAttachedToWindow) d.start()
    }

    private fun setBgMode(mode: String) {
        bgMode = mode
        prefs.edit().putString(KEY_BG_MODE, mode).apply()
        applyBackground()
    }

    /** يفعّل خلفية الفضاء والنجوم. */
    fun enableSpaceBackground() { setBgMode("space"); RinTerminal.system(context.getString(R.string.terminal_bg_space_on)) }

    /** يشغّل/يوقف حركة الفضاء (النجوم تبقى ظاهرة ثابتة عند الإيقاف). */
    fun setBackgroundAnimated(on: Boolean) {
        bgAnimated = on
        prefs.edit().putBoolean(KEY_BG_ANIM, on).apply()
        live?.setAnimated(on); if (on && isAttachedToWindow && windowVisibility == View.VISIBLE) live?.start()
    }

    fun toggleBackgroundAnimation() {
        if (live == null) { RinTerminal.system(context.getString(R.string.terminal_bg_anim_need_space), TermRole.ERROR); return }
        setBackgroundAnimated(!bgAnimated)
        RinTerminal.system(context.getString(if (bgAnimated) R.string.terminal_bg_anim_on else R.string.terminal_bg_anim_off))
    }

    /** يستورد الصورة المختارة ويفعّلها. يُرجع false عند الفشل. */
    fun setBackgroundFromUri(uri: android.net.Uri): Boolean {
        // أولاً: هل الملف متحرك (GIF / WebP متحرك / APNG)؟ وإلا يُعامَل كصورة ثابتة.
        when (TerminalBackground.importAnimated(context, uri)) {
            TerminalBackground.AnimResult.OK -> {
                setBgMode("anim")
                RinTerminal.system(context.getString(R.string.terminal_bg_anim_set, bgDim.toString()))
                return true
            }
            TerminalBackground.AnimResult.TOO_BIG -> {
                RinTerminal.system(context.getString(R.string.terminal_bg_too_big), TermRole.ERROR); return false
            }
            else -> {}
        }
        if (!TerminalBackground.importFrom(context, uri)) {
            RinTerminal.system(context.getString(R.string.terminal_bg_failed), TermRole.ERROR); return false
        }
        bgBitmap?.recycle()
        bgBitmap = TerminalBackground.load(context)
        setBgMode("image")
        RinTerminal.system(context.getString(R.string.terminal_bg_set, bgDim.toString()))
        return true
    }

    fun clearBackgroundImage() {
        TerminalBackground.clear(context)
        bgBitmap = null
        setBgMode("none")
    }

    fun setBackgroundDim(percent: Int) {
        bgDim = percent.coerceIn(0, 90)
        prefs.edit().putInt(KEY_BG_DIM, bgDim).apply()
        applyBackground()
    }

    // ---------------------------------------------------------------- السمات والأحجام

    /** يضبط متغيّرات الألوان المشتركة (الرسم يقرؤها وقت البناء). */
    private fun setThemeColors(t: RinTheme) {
        BG = t.bg; INPUT_BG = t.inputBg; CHIP_BG = t.chipBg; BORDER = t.border; HEADER_BG = t.headerBg; CMD_BG = t.cmdBg
        FG = t.fg; WHITE = t.white; DIM = t.dim; ACCENT = t.accent; ANSWER_BLUE = t.blue; PROMPT_GREEN = t.promptGreen
        OK_GREEN = t.ok; ERROR_RED = t.error; CONT_YELLOW = t.warn
        RinBanner.setGradient(t.gradient.first, t.gradient.second)
    }

    /** يطبّق سمة على الواجهة الحيّة ويحفظها. */
    fun applyTheme(t: RinTheme) {
        themeId = t.id
        setThemeColors(t)
        prefs.edit().putString(KEY_THEME, t.id).apply()
        applyBackground()
        divider.setBackgroundColor(BORDER)
        header.setBackgroundColor(HEADER_BG)
        screen.setTextColor(FG)
        field.setTextColor(FG); field.setHintTextColor(DIM)
        activity.setTextColor(ACCENT)
        statusLabel.setTextColor(FG); statusInfo.setTextColor(DIM)
        for (c in chips) { restyleChip(c); c.setTextColor(DIM) }
        fsDialog?.window?.setBackgroundDrawable(android.graphics.drawable.ColorDrawable(BG))
        (fsDialog?.window?.decorView as? View)?.setBackgroundColor(BG)
        updateMode(); refresh(true)
    }

    fun setFontSize(sp: Float) {
        fontSp = sp.coerceIn(FONT_MIN, FONT_MAX)
        screen.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, fontSp)
        prefs.edit().putFloat(KEY_FONT, fontSp).apply()
        refresh(true)
    }

    fun setSpacing(name: String) {
        spacingName = name
        prefs.edit().putString(KEY_SPACING, name).apply()
        refresh(true)
    }

    /** قائمة السمات: اسم + عيّنة ألوان (خلفية/نص/تمييز/أخضر/أحمر) وعلامة السمة الحالية. */
    private fun themeListText(): String {
        fun fg(c: Int) = "\u001b[38;2;${(c shr 16) and 255};${(c shr 8) and 255};${c and 255}m"
        fun bgc(c: Int) = "\u001b[48;2;${(c shr 16) and 255};${(c shr 8) and 255};${c and 255}m"
        return RinTheme.ALL.joinToString("\n") { t ->
            val mark = if (t.id == themeId) "\u001b[38;2;63;185;80m●\u001b[0m" else " "
            val sw = bgc(t.bg) + fg(t.fg) + " Aa " + fg(t.accent) + "██" + fg(t.promptGreen) + "██" + fg(t.error) + "██" + fg(t.warn) + "██" + "\u001b[0m"
            "$mark ${t.id.padEnd(10)} $sw  \u001b[38;2;139;148;158m${t.title}\u001b[0m${if (t.isLight) " ☀" else ""}"
        }
    }

    private fun showStyle() {
        RinTerminal.output(
            "\u001b[38;2;139;148;158mtheme\u001b[0m    $themeId\n" +
            "\u001b[38;2;139;148;158mfont\u001b[0m     ${fontSp}sp\n" +
            "\u001b[38;2;139;148;158mspacing\u001b[0m  $spacingName\n" +
            "\u001b[38;2;139;148;158mbackground\u001b[0m  ${if (bgMode == "image" && bgBitmap != null) "image · dim $bgDim%" else if (bgMode == "anim") "animated image · dim $bgDim% · ${if (bgAnimated) "playing" else "paused"}" else if (bgMode == "space") "space · ${if (bgAnimated) "animated" else "still"}" else "none"}"
        )
    }

    // ---------------------------------------------------------------- ملء الشاشة

    private var fsDialog: android.app.Dialog? = null
    private var fsParent: android.view.ViewGroup? = null
    private var fsIndex = 0
    private var fsKeep: ((String?) -> Unit)? = null
    private var fsParams: android.view.ViewGroup.LayoutParams? = null

    val isFullscreen: Boolean get() = fsDialog != null

    /**
     * يعيد استضافة الطرفية نفسها (بجلستها وسجلّها) داخل نافذة بملء الشاشة، ويعيدها لمكانها عند الإغلاق
     * (زر ⛶ أو رجوع أو أمر `fullscreen`). السؤال المعلّق (input) يبقى عبر إعادة الإرفاق.
     */
    fun toggleFullscreen() {
        if (fsDialog != null) { fsDialog?.dismiss(); return }
        val parent = parent as? android.view.ViewGroup ?: return
        val saved = deliver
        fsParent = parent; fsIndex = parent.indexOfChild(this); fsParams = layoutParams
        parent.removeView(this)
        val d = android.app.Dialog(context, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
        val root = android.widget.FrameLayout(context).apply { setBackgroundColor(BG); fitsSystemWindows = true }
        root.addView(this, android.widget.FrameLayout.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT))
        d.setContentView(root)
        d.window?.apply {
            setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
            setBackgroundDrawable(android.graphics.drawable.ColorDrawable(BG))
            androidx.core.view.WindowCompat.setDecorFitsSystemWindows(this, false)
            androidx.core.view.WindowCompat.getInsetsController(this, decorView).apply {
                hide(androidx.core.view.WindowInsetsCompat.Type.statusBars())
                systemBarsBehavior = androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }
        d.setOnDismissListener {
            val keep = fsKeep; fsKeep = null
            (this.parent as? android.view.ViewGroup)?.removeView(this)
            fsParent?.addView(this, fsIndex.coerceAtMost(fsParent!!.childCount), fsParams)
            fsDialog = null; fsParent = null
            deliver = keep
            updateMode(); refresh(true)
        }
        fsDialog = d
        d.show()
        deliver = saved
        updateMode(); refresh(true)
        focusInput()
    }

    // ---- واجهة عامة ----

    /** يُستدعى من [RinInputBridge] (ترد رئيسي) عند توقّف برنامج لإدخال؛ نص السؤال في السجل أصلاً. */
    fun beginProgramInput(@Suppress("UNUSED_PARAMETER") prompt: String, deliver: (String?) -> Unit) {
        this.deliver = deliver
        field.setText("")
        updateMode()
        refresh(force = true)
        focusInput()
    }

    /** true إن كان برنامج ينتظر جواباً الآن. */
    fun isAwaitingInput(): Boolean = deliver != null

    fun focusInput() {
        field.requestFocus()
        field.post {
            val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
            imm?.showSoftInput(field, InputMethodManager.SHOW_IMPLICIT)
        }
    }

    fun copyTranscript() {
        val text = RinTerminal.buffer.toPlainText()
        if (text.isEmpty()) return
        val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
        cm.setPrimaryClip(ClipData.newPlainText("rin-terminal", text))
        Toast.makeText(context, R.string.terminal_copied, Toast.LENGTH_SHORT).show()
    }

    fun shareTranscript() {
        val text = RinTerminal.buffer.toPlainText()
        if (text.isEmpty()) return
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_TEXT, text)
        }
        val chooser = Intent.createChooser(send, null)
        if (context !is android.app.Activity) chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try { context.startActivity(chooser) } catch (_: Throwable) { /* لا تطبيق مشاركة */ }
    }

    // ---- الإدخال ----

    private fun submit() {
        val text = field.text.toString()
        val d = deliver
        if (d != null) {
            // جواب لبرنامج: يُسلَّم كما هو (الفارغ مسموح — Enter يُبقي القيمة الحالية)، والصدى يكتبه المُجدوِل.
            deliver = null
            field.setText("")
            updateMode()
            d(text)
            return
        }
        field.setText("")
        historyIndex = -1
        lastTabPrefix = null
        runShell(text)
    }

    private fun cancelOrClear() {
        val d = deliver
        if (d != null) {
            deliver = null
            field.setText("")
            updateMode()
            d(null)
        } else if (RinTerminal.repl.isContinuing) {
            RinTerminal.repl.cancelPending()
            field.setText("")
            RinTerminal.system("^C")
            updateMode()
        } else if (RinNativeTerminal.continuing()) {
            RinNativeTerminal.cancelPending()
            field.setText("")
            RinTerminal.system("^C")
            updateMode()
        } else if (RinNativeTerminal.isBusy()) {
            // المحرّك الأصلي لا يقبل المقاطعة: نخبر المستخدم بدل أن نوهمه بالإيقاف.
            field.setText("")
            RinTerminal.system(context.getString(R.string.terminal_native_busy), TermRole.ERROR)
        } else if (RinJobScheduler.isRunning()) {
            // ^C أثناء تشغيل لا ينتظر إدخالاً: أوقفه (وأفرغ الطابور خلفه).
            field.setText("")
            RinTerminal.system("^C")
            RinJobScheduler.stopAll()
        } else {
            field.setText("")
            historyIndex = -1
        }
    }

    private fun stepHistory(direction: Int) {
        if (deliver != null) return
        val h = RinTerminal.buffer.history()
        if (h.isEmpty()) return
        historyIndex = (historyIndex + direction).coerceIn(-1, h.size - 1)
        val text = if (historyIndex < 0) "" else h[h.size - 1 - historyIndex]
        field.setText(text)
        field.setSelection(text.length)
    }

    // ---- shell ----

    private fun runShell(raw: String) {
        val repl = RinTerminal.repl
        var line = raw.trimEnd()
        val nativeContinuing = RinNativeTerminal.continuing()
        if (line.isBlank() && !repl.isContinuing && !nativeContinuing) return

        // أوامر الـ shell لا تُقبل في منتصف أمر متعدد الأسطر (قد تكون جزءاً من الشيفرة).
        if (!repl.isContinuing && !nativeContinuing) {
            // `!!` = آخر أمر، `!N` = الأمر رقم N في `history` (يُعرض الأمر المُوسَّع قبل تنفيذه).
            expandHistory(line)?.let { expanded ->
                if (expanded.isEmpty()) {
                    RinTerminal.command(line)
                    RinTerminal.system(context.getString(R.string.terminal_history_none), TermRole.ERROR)
                    return
                }
                line = expanded
            }
            expandAlias(line)?.let { line = it }
            val parts = line.trim().split(Regex("\\s+"), limit = 2)
            val cmd = parts[0].lowercase()
            val arg = parts.getOrNull(1)?.trim().orEmpty()
            val codeLike = cmd in FILE_TOOLS && (arg.startsWith("=") || arg.startsWith("("))   // head = 5 / find(x) شيفرة لا أمر
            if (cmd in SHELL_COMMANDS && !codeLike && (arg.isEmpty() || cmd in COMMANDS_WITH_ARG)) {
                RinTerminal.command(line.trim())
                saveHistory()
                runBuiltin(cmd, arg)
                return
            }
        }

        // أوامر `:` (وكل شيفرة بعد `native on`) تذهب إلى جلسة طرفية Linux الأصلية المُضمَّنة.
        if (routeToNative(line, nativeContinuing)) return

        if (RinNativeTerminal.isBusy()) {
            RinTerminal.system(context.getString(R.string.terminal_native_busy), TermRole.ERROR)
            return
        }
        RinTerminal.echo(line, if (repl.isContinuing) TermRole.CONT else TermRole.COMMAND)
        when (val r = repl.feed(line)) {
            is RinReplSession.Feed.NeedMore -> Unit
            is RinReplSession.Feed.Ready -> {
                RinTerminal.remember(r.display.replace('\n', ' ').trim())
                saveHistory()
                val source = repl.buildSource(r.code)
                val accepted = onExecuteRequested?.invoke(source) { repl.commit(r.code) } ?: false
                if (!accepted) RinTerminal.system(context.getString(R.string.job_queue_full_toast), TermRole.ERROR)
            }
        }
        updateMode()
    }

    /** سطر أمر لطرفية Linux الأصلية: يبدأ بـ `:` أو بـ `$` متبوعاً باسم أمر (مثل `$ cp *.rin ~/`). */
    private fun isCommandLine(line: String): Boolean {
        val t = line.trimStart()
        return t.startsWith(":") || DOLLAR_CMD.containsMatchIn(t)
    }

    /**
     * يوجّه [line] إلى جلسة طرفية Linux الأصلية ([RinNativeTerminal]) عندما: هناك أمر ناقص فيها، أو يبدأ السطر
     * بـ `:`، أو وضع `native on` فعّال. يعيد true إن تولّت الأمر (حتى لو رفضته برسالة)، وfalse ليكمل المسار
     * القديم (الـ REPL). أمر ناقص في الـ REPL القديم له الأولوية فلا يُخطَف منه سطر المتابعة.
     */
    /** عدد أعمدة الأحرف التي تتسع لها الشاشة (الخط أحادي المسافة). 0 إن لم يُقَس بعد (قبل أول تخطيط). */
    private fun measureColumns(): Int {
        val usable = screen.width - screen.paddingLeft - screen.paddingRight
        val cell = screen.paint.measureText("M")
        return if (usable > 0 && cell > 0f) (usable / cell).toInt().coerceIn(20, 200) else 0
    }

    private fun routeToNative(line: String, nativeContinuing: Boolean): Boolean {
        if (RinTerminal.repl.isContinuing) return false
        val isColon = isCommandLine(line)   // `:cmd …` or `$ cmd …`
        if (!nativeContinuing && !isColon && !RinNativeTerminal.nativeMode) return false
        if (!RinNativeTerminal.isAvailable()) {
            if (!nativeContinuing && !isColon) return false   // native on لكن المكتبة غير متاحة: عُد إلى الـ REPL
            RinTerminal.command(line.trim())
            RinTerminal.system(context.getString(R.string.terminal_native_unavailable), TermRole.ERROR)
            return true
        }
        if (RinJobScheduler.isRunning() || RinNativeTerminal.isBusy()) {
            RinTerminal.system(context.getString(R.string.terminal_native_busy), TermRole.ERROR)
            return true
        }
        RinNativeTerminal.columns = measureColumns()
        RinTerminal.echo(line, if (nativeContinuing) TermRole.CONT else TermRole.COMMAND)
        if (!nativeContinuing) {
            RinTerminal.remember(line.trim())
            saveHistory()
        }
        if (!RinNativeTerminal.submit(line) { updateMode() }) {
            RinTerminal.system(context.getString(R.string.terminal_native_busy), TermRole.ERROR)
        }
        updateMode()
        return true
    }

    /** null = ليس أمر تاريخ؛ "" = أمر تاريخ لكن لا يوجد ما يطابقه؛ غير ذلك = الأمر المُوسَّع. */
    private fun expandHistory(line: String): String? {
        val m = HISTORY_REF.matchEntire(line.trim()) ?: return null
        val h = RinTerminal.buffer.history()
        val ref = m.groupValues[1]
        return if (ref == "!") h.lastOrNull().orEmpty()
        else h.getOrNull((ref.toIntOrNull() ?: 0) - 1).orEmpty()
    }

    private fun engineVersionOrNull(): String? = try { RinBanner.parseVersion(RinEngine.engineVersion()) } catch (t: Throwable) { null }

    private fun appVersionOrNull(): String? = try {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    } catch (t: Throwable) { null }

    /** صفوف معلومات الشعار: اللغة، المحرّك، التطبيق، المكتبات. */
    private fun bannerInfo(): List<Pair<String, String>> = buildList {
        val lang = engineVersionOrNull()
        add("Language" to "Rin ${lang ?: "—"}")
        add("Engine" to "C++17 · ${if (RinNativeTerminal.nativeMode) "native" else "REPL"}")
        add("App" to "RinStudio ${appVersionOrNull() ?: "—"}")
        add("Libraries" to "${BuiltinLibraries.all.size} built-in")
    }

    private fun bannerText(): String = RinBanner.logoText(engineVersionOrNull(), info = bannerInfo())

    // ---- aliases (محفوظة بين الجلسات) ----

    private fun loadAliases(): MutableMap<String, String> {
        val m = LinkedHashMap<String, String>()
        prefs.getString("aliases", "")?.lineSequence()?.forEach { l ->
            val i = l.indexOf('\t'); if (i > 0) m[l.substring(0, i)] = l.substring(i + 1)
        }
        return m
    }

    private fun saveAliases(m: Map<String, String>) {
        prefs.edit().putString("aliases", m.entries.joinToString("\n") { it.key + "\t" + it.value }).apply()
    }

    /** يستبدل الكلمة الأولى إن كانت alias (مرّة واحدة بلا تكرار)؛ null = لا alias. */
    private fun expandAlias(line: String): String? {
        val t = line.trimStart()
        val first = t.substringBefore(' ')
        val target = loadAliases()[first] ?: return null
        return target + t.substring(first.length)
    }

    private fun fsBase(): java.io.File = java.io.File(try { RinEngine.currentBaseDir() } catch (t: Throwable) { "." })

    private fun showFs(o: RinFs.Out, arg: String, countLabel: Boolean = false) {
        if (o.error != null) {
            val res = when (o.error) {
                RinFs.OUTSIDE -> R.string.terminal_fs_outside
                RinFs.NOT_DIR -> R.string.terminal_fs_not_dir
                RinFs.NOT_FILE -> R.string.terminal_fs_not_file
                RinFs.BINARY -> R.string.terminal_fs_binary
                else -> R.string.terminal_fs_not_found
            }
            RinTerminal.system(context.getString(res, arg), TermRole.ERROR)
            return
        }
        if (o.lines.isEmpty()) { RinTerminal.system(context.getString(R.string.terminal_fs_empty)); return }
        RinTerminal.output(o.lines.joinToString("\n"))
    }

    /** تفاصيل أمر واحد من جدول المساعدة نفسه (name::desc)؛ null إن لم يوجد. */
    private fun helpFor(cmd: String): String? {
        for (line in context.getString(R.string.terminal_help_rows).lines()) {
            val p = line.trim()
            if (p.isEmpty() || p.startsWith("#")) continue
            val parts = p.split("::", limit = 2)
            if (parts.size < 2) continue
            val names = parts[0].split(Regex("[\\s·]+")).filter { it.isNotEmpty() }
            if (names.any { it.equals(cmd, ignoreCase = true) })
                return "\u001b[1m${parts[0].trim()}\u001b[0m\n${parts[1].trim()}"
        }
        return null
    }

    private fun runBuiltin(cmd: String, arg: String) {
        when (cmd) {
            "help", "?" -> if (arg.isNotBlank()) {
                val h = helpFor(arg.trim())
                if (h == null) RinTerminal.system(context.getString(R.string.terminal_help_none, arg.trim()), TermRole.ERROR) else RinTerminal.output(h)
            } else {
                RinTerminal.output(RinBanner.help(context.getString(R.string.terminal_help_rows), context.getString(R.string.terminal_help_footer)).joinToString("\n"))
                RinTerminal.system(context.getString(R.string.terminal_help_native))
            }
            "clear", "cls" -> RinTerminal.clear()
            "run" -> onRunRequested?.invoke()
            "history" -> if (arg.trim().lowercase() == "clear") {
                RinTerminal.buffer.clearHistory(); saveHistory()
                RinTerminal.system(context.getString(R.string.terminal_history_cleared))
            } else {
                val h = RinTerminal.buffer.history()
                RinTerminal.system(
                    if (h.isEmpty()) context.getString(R.string.terminal_empty)
                    else h.mapIndexed { i, s -> "${(i + 1).toString().padStart(3)}  $s" }.joinToString("\n")
                )
            }
            "version" -> {
                RinTerminal.output(bannerText())
                RinTerminal.system(try { RinEngine.engineVersion() } catch (t: Throwable) { "engine unavailable" })
            }
            "pwd" -> RinTerminal.output(RinFs.resolve(fsBase(), "")?.path ?: fsBase().path)
            "ls" -> {
                val all = arg.split(Regex("\\s+")).any { it == "-a" }
                val path = arg.split(Regex("\\s+")).filter { it.isNotEmpty() && it != "-a" }.joinToString(" ")
                showFs(RinFs.ls(fsBase(), path, all), path)
            }
            "tree" -> showFs(RinFs.tree(fsBase(), arg), arg)
            "find" -> if (arg.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_usage_fmt, "find <text>"), TermRole.ERROR)
                else showFs(RinFs.find(fsBase(), arg), arg)
            "head", "tail" -> {
                val (path, n) = RinFs.parseCountArg(arg)
                if (path.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_usage_fmt, "$cmd <file> [n]"), TermRole.ERROR)
                else {
                    val o = if (cmd == "head") RinFs.head(fsBase(), path, n) else RinFs.tail(fsBase(), path, n)
                    showFs(o, path)
                    if (o.error == null && o.extra > 0) RinTerminal.system(context.getString(R.string.terminal_fs_truncated, o.extra.toString()))
                }
            }
            "wc" -> if (arg.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_usage_fmt, "wc <file>"), TermRole.ERROR)
                else showFs(RinFs.wc(fsBase(), arg), arg)
            "log" -> {
                val text = RinTerminal.buffer.toPlainText()
                if (text.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_log_empty), TermRole.ERROR)
                else {
                    val saved = RinFs.saveLog(fsBase(), text, System.currentTimeMillis())
                    if (saved == null) RinTerminal.system(context.getString(R.string.terminal_log_failed), TermRole.ERROR)
                    else RinTerminal.system(context.getString(R.string.terminal_log_saved, saved))
                }
            }
            "cat" -> {
                if (arg.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_cat_usage), TermRole.ERROR)
                else {
                    val o = RinFs.cat(fsBase(), arg)
                    showFs(o, arg)
                    if (o.error == null && o.extra > 0) RinTerminal.system(context.getString(R.string.terminal_fs_truncated, o.extra.toString()))
                }
            }
            "alias" -> {
                val m = loadAliases()
                val eq = arg.indexOf('=')
                if (arg.isEmpty()) {
                    if (m.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_alias_none))
                    else RinTerminal.output(m.entries.joinToString("\n") { "\u001b[38;2;57;197;207m${it.key}\u001b[0m\u001b[38;2;139;148;158m = \u001b[0m${it.value}" })
                } else if (eq < 1) {
                    RinTerminal.system(context.getString(R.string.terminal_alias_usage), TermRole.ERROR)
                } else {
                    val name = arg.substring(0, eq).trim()
                    val value = arg.substring(eq + 1).trim().trim('"', '\'')
                    if (name.isEmpty() || value.isEmpty() || name.contains(' ') || name.lowercase() in SHELL_COMMANDS) {
                        RinTerminal.system(context.getString(R.string.terminal_alias_usage), TermRole.ERROR)
                    } else { m[name] = value; saveAliases(m); RinTerminal.system(context.getString(R.string.terminal_alias_set, name, value)) }
                }
            }
            "unalias" -> {
                val m = loadAliases()
                if (m.remove(arg.trim()) != null) { saveAliases(m); RinTerminal.system(context.getString(R.string.terminal_alias_removed, arg.trim())) }
                else RinTerminal.system(context.getString(R.string.terminal_alias_missing, arg.trim()), TermRole.ERROR)
            }
            "theme" -> {
                val a = arg.trim()
                if (a.isEmpty()) {
                    RinTerminal.output(themeListText())
                    RinTerminal.system(context.getString(R.string.terminal_theme_usage))
                } else {
                    val night = (context.resources.configuration.uiMode and android.content.res.Configuration.UI_MODE_NIGHT_MASK) ==
                        android.content.res.Configuration.UI_MODE_NIGHT_YES
                    val t = when (a.lowercase()) {   // اختصارات: light / dark / auto (يتبع وضع النظام)
                        "light" -> RinTheme.PAPER
                        "dark" -> RinTheme.RIN
                        "auto" -> if (night) RinTheme.MIDNIGHT else RinTheme.DAYLIGHT
                        else -> RinTheme.find(a)
                    }
                    if (t == null) RinTerminal.system(context.getString(R.string.terminal_theme_unknown, a), TermRole.ERROR)
                    else { applyTheme(t); RinTerminal.system(context.getString(R.string.terminal_theme_set, t.title)) }
                }
            }
            "size" -> {
                val a = arg.trim().lowercase()
                val sp = RinSizes.FONT_PRESETS[a]
                if (a.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_size_usage, RinSizes.FONT_PRESETS.keys.joinToString(" · ")))
                else if (sp == null) RinTerminal.system(context.getString(R.string.terminal_size_usage, RinSizes.FONT_PRESETS.keys.joinToString(" · ")), TermRole.ERROR)
                else { setFontSize(sp); RinTerminal.system(context.getString(R.string.terminal_font_set, fontSp.toString())) }
            }
            "spacing" -> {
                val a = arg.trim().lowercase()
                if (a in RinSizes.SPACING) { setSpacing(a); RinTerminal.system(context.getString(R.string.terminal_spacing_set, a)) }
                else RinTerminal.system(context.getString(R.string.terminal_spacing_usage, RinSizes.SPACING.keys.joinToString(" · ")), if (a.isEmpty()) TermRole.SYSTEM else TermRole.ERROR)
            }
            "style" -> showStyle()
            "background" -> {
                val a = arg.trim().lowercase()
                val parts = a.split(Regex("\\s+"))
                when {
                    a.isEmpty() || a == "set" || a == "pick" -> {
                        val pick = onPickBackground
                        if (pick == null) RinTerminal.system(context.getString(R.string.terminal_bg_unavailable), TermRole.ERROR) else pick()
                    }
                    a == "space" || a == "stars" -> enableSpaceBackground()
                    a == "stop" || a == "freeze" || a == "anim off" -> { setBackgroundAnimated(false); RinTerminal.system(context.getString(R.string.terminal_bg_anim_off)) }
                    a == "play" || a == "anim on" || a == "anim" -> { setBackgroundAnimated(true); RinTerminal.system(context.getString(R.string.terminal_bg_anim_on)) }
                    a == "clear" || a == "off" || a == "reset" -> {
                        clearBackgroundImage(); RinTerminal.system(context.getString(R.string.terminal_bg_cleared))
                    }
                    parts[0] == "dim" && parts.size == 2 && parts[1].toIntOrNull() != null -> {
                        setBackgroundDim(parts[1].toInt()); RinTerminal.system(context.getString(R.string.terminal_bg_dim, bgDim.toString()))
                    }
                    else -> RinTerminal.system(context.getString(R.string.terminal_bg_usage), TermRole.ERROR)
                }
            }
            "fullscreen" -> toggleFullscreen()
            "banner" -> RinTerminal.output(bannerText())
            "colors" -> RinTerminal.output(RinBanner.palette().joinToString("\n"))
            "echo" -> RinTerminal.output(arg.replace("\\e", "\u001b").replace("\\033", "\u001b"))
            "font" -> {
                val a = arg.trim()
                val next = when {
                    a.isEmpty() -> null
                    a == "+" -> fontSp + 1f
                    a == "-" -> fontSp - 1f
                    a == "reset" -> 12.5f
                    else -> a.toFloatOrNull()
                }
                if (next == null) {
                    RinTerminal.system(context.getString(R.string.terminal_font_usage, fontSp.toString()), if (a.isEmpty()) TermRole.SYSTEM else TermRole.ERROR)
                } else {
                    setFontSize(next)
                    RinTerminal.system(context.getString(R.string.terminal_font_set, fontSp.toString()))
                }
            }
            "about" -> {
                val rt = Runtime.getRuntime()
                val usedMb = (rt.totalMemory() - rt.freeMemory()) / (1024 * 1024)
                val extra = listOf(
                    "Memory" to "$usedMb / ${rt.maxMemory() / (1024 * 1024)} MB",
                    "Font" to "${fontSp}sp",
                    "History" to "${RinTerminal.buffer.history().size} cmds"
                )
                RinTerminal.output(RinBanner.logoText(null, info = bannerInfo() + extra))
            }
            "libs" -> {
                val groups = LibCategory.values().map { cat ->
                    context.getString(cat.labelRes) to BuiltinLibraries.all.filter { it.category == cat }.map { it.displayName }
                }
                RinTerminal.output(RinBanner.libraryIndex(groups).joinToString("\n"))
            }
            "lib" -> {
                val q = arg.trim().removePrefix("lib/").removeSuffix(".og.rin").lowercase()
                val info = BuiltinLibraries.all.firstOrNull { it.displayName.lowercase() == q }
                    ?: BuiltinLibraries.all.filter { it.displayName.lowercase().startsWith(q) }.takeIf { q.isNotEmpty() && it.size == 1 }?.first()
                if (info == null) {
                    RinTerminal.system(context.getString(if (q.isEmpty()) R.string.terminal_lib_usage else R.string.terminal_lib_unknown, q), TermRole.ERROR)
                } else {
                    RinTerminal.output(RinBanner.library(
                        info.displayName, context.getString(info.category.labelRes), info.description,
                        info.sampleFunctions, info.importPath,
                        listOf("Language" to "Rin ${engineVersionOrNull() ?: "—"}", "Source" to "built-in")
                    ).joinToString("\n"))
                }
            }
            "reset" -> {
                RinTerminal.repl.reset()
                RinNativeTerminal.reset()
                RinTerminal.system(context.getString(R.string.terminal_session_reset))
            }
            "session", "vars" -> {
                val names = if (RinNativeTerminal.nativeMode) RinNativeTerminal.variableNames() else RinTerminal.repl.declaredNames()
                RinTerminal.system(
                    if (names.isEmpty()) context.getString(R.string.terminal_session_empty)
                    else names.joinToString("  ")
                )
            }
            "grep" -> {
                if (arg.isEmpty()) RinTerminal.system(context.getString(R.string.terminal_grep_usage))
                else {
                    val hits = RinTerminal.buffer.grep(arg)
                    RinTerminal.system(if (hits.isEmpty()) context.getString(R.string.terminal_grep_none, arg) else hits.joinToString("\n"))
                }
            }
            "goto" -> {
                val n = arg.toIntOrNull()
                if (n == null || n < 1) RinTerminal.system(context.getString(R.string.terminal_goto_usage), TermRole.ERROR)
                else onGoToLine?.invoke(n)
            }
            "native" -> when (arg.lowercase()) {
                "on" -> if (RinNativeTerminal.isAvailable()) {
                    RinNativeTerminal.nativeMode = true
                    RinTerminal.system(context.getString(R.string.terminal_native_on))
                } else RinTerminal.system(context.getString(R.string.terminal_native_unavailable), TermRole.ERROR)
                "off" -> {
                    RinNativeTerminal.nativeMode = false
                    RinTerminal.system(context.getString(R.string.terminal_native_off))
                }
                else -> RinTerminal.system(
                    context.getString(if (RinNativeTerminal.nativeMode) R.string.terminal_native_on else R.string.terminal_native_off)
                )
            }
            "share" -> shareTranscript()
            "copy" -> copyTranscript()
            "stop", "cancel" -> {
                RinTerminal.system(
                    context.getString(
                        if (RinJobScheduler.stopAll()) R.string.terminal_stopped else R.string.terminal_nothing_waiting
                    )
                )
            }
        }
    }

    /**
     * `ocr <صورة> [--lang ara+eng] [--psm N] [--enhance] [--min N]`: لا منطق OCR جديد هنا — يُبنى استدعاء الدالة
     * الموجودة `make.image.text` ويُشغَّل بالمسار نفسه لأي أمر في الـ terminal (نفس الأمان والأخطاء الواضحة).
     * النص يُطبع كما هو بـ Unicode، وسطر الثقة يُلوَّن بـ `colorAnsi` (أخضر ≥80، أصفر ≥50، أحمر دون ذلك).
     */
    private fun runOcr(arg: String) {
        val source = ocrSource(arg)
        if (source == null) {
            RinTerminal.system(context.getString(R.string.terminal_ocr_usage), TermRole.ERROR)
            return
        }
        val accepted = onExecuteRequested?.invoke(source) {} ?: false
        if (!accepted) RinTerminal.system(context.getString(R.string.job_queue_full_toast), TermRole.ERROR)
    }

    /** مصدر Rin لأمر ocr، أو null إن كانت المعاملات غير صالحة (تُفحص قبل أن تدخل نص الشيفرة). */
    private fun ocrSource(arg: String): String? {
        var path: String? = null
        val opts = ArrayList<String>()
        val tokens = OCR_TOKEN.findAll(arg).map { m -> m.groups[1]?.value ?: m.groups[2]?.value ?: m.value }.toList()
        var i = 0
        fun value(pattern: Regex): String? = tokens.getOrNull(++i)?.takeIf { pattern.matches(it) }
        while (i < tokens.size) {
            when (val t = tokens[i]) {
                "--lang", "-l" -> opts.add("\"lang\": " + rinString(value(OCR_LANG) ?: return null))
                "--psm" -> opts.add("\"psm\": " + (value(OCR_NUMBER) ?: return null))
                "--min" -> opts.add("\"minConfidence\": " + (value(OCR_NUMBER) ?: return null))
                "--enhance" -> opts.add("\"enhance\": true")
                else -> if (t.startsWith("--") || path != null) return null else path = t
            }
            i++
        }
        val file = path ?: return null
        val dim = "\"#6e7681\""
        return """
            let r = make.image.text(${rinString(file)}, {${opts.joinToString(", ")}});
            if (r.ok) {
                print r.text;
                let tone = "#f85149";
                if (r.confidence >= 50) { tone = "#d29922"; }
                if (r.confidence >= 80) { tone = "#3fb950"; }
                print colorAnsi("confidence ", $dim) + colorAnsi(r.confidence + "%", tone) + colorAnsi("  ·  " + r.wordCount + " words  ·  " + r.lang, $dim);
            } else {
                print colorAnsi(r.error, "#f85149");
            }
        """.trimIndent()
    }

    /** حرفية نص Rin آمنة: تُهرَّب `\` و`"` و`$` (استيفاء) والأسطر. */
    private fun rinString(s: String): String =
        "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"").replace("$", "\\$")
            .replace("\r", "\\r").replace("\n", "\\n") + "\""

    // ---- تاريخ الأوامر المحفوظ بين الجلسات ----

    private fun loadHistory(): List<String> =
        prefs.getString(KEY_HISTORY, null)?.split(HISTORY_SEP)?.filter { it.isNotBlank() }.orEmpty()

    private fun saveHistory() {
        prefs.edit().putString(KEY_HISTORY, RinTerminal.buffer.history().joinToString(HISTORY_SEP)).apply()
    }

    // ---- الإكمال التلقائي (⇥) ----

    private fun complete() {
        if (deliver != null) return
        val text = field.text.toString()
        val cursor = field.selectionStart.coerceIn(0, text.length)
        val before = text.substring(0, cursor)
        val prefix = Regex("[\\p{L}\\p{N}_]*$").find(before)?.value.orEmpty()
        if (prefix.isEmpty()) return

        val pool = LinkedHashSet<String>()
        if (before.trim() == prefix) pool.addAll(SHELL_COMMANDS.filter { it.length > 1 && it !in ALIAS_ONLY })   // الكلمة الأولى فقط
        pool.addAll(rinWords())
        val matches = RinTerminal.repl.complete(prefix, pool)
        if (matches.isEmpty()) return

        val common = matches.reduce { a, b -> a.commonPrefixWith(b) }
        if (common.length > prefix.length) {
            val start = cursor - prefix.length
            field.setText(text.substring(0, start) + common + text.substring(cursor))
            field.setSelection(start + common.length)
            lastTabPrefix = null
        } else if (lastTabPrefix == prefix || matches.size == 1) {
            // ضغطة ثانية بلا تقدّم (أو مرشّح واحد مطابق تماماً): اعرض الخيارات.
            RinTerminal.system(matches.take(40).joinToString("  "))
            lastTabPrefix = null
        } else {
            lastTabPrefix = prefix
        }
    }

    private var cachedWords: List<String>? = null

    private fun rinWords(): List<String> =
        cachedWords ?: (try { RinSyntax.keywordsFor(SyntaxLanguage.RIN) } catch (t: Throwable) { emptyList() }).also { cachedWords = it }

    // ---- الرسم ----

    /** شريط الحالة العلوي: نقطة ملوّنة + حالة الجلسة (READY/RUNNING/INPUT/NATIVE) + عدد الأسطر. */
    private fun buildHeader() {
        header.apply {
            orientation = HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(HEADER_BG)
            setPadding(dp(12), 0, dp(12), 0)
        }
        statusDot.apply { text = "●"; setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 10f); setTextColor(OK_GREEN) }
        statusLabel.apply {
            typeface = Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 11f)
            setTypeface(typeface, Typeface.BOLD)
            letterSpacing = 0.08f
            setTextColor(FG)
            setPadding(dp(6), 0, 0, 0)
        }
        statusInfo.apply {
            typeface = Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 10.5f)
            setTextColor(DIM)
            gravity = Gravity.END
        }
        header.addView(statusDot, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        header.addView(statusLabel, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        header.addView(statusInfo, LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f))
        // أزرار سريعة: ↓ للأسفل · ⎘ نسخ الكل · ⇪ مشاركة · ⌫ مسح الشاشة · ⛶ ملء الشاشة
        for ((icon, action) in listOf<Pair<Int, () -> Unit>>(
            R.drawable.rs_ic_arrow_down to { stickToBottom = true; scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }; Unit },
            R.drawable.rs_ic_copy to { copyTranscript() },
            R.drawable.rs_ic_share to { shareTranscript() },
            R.drawable.rs_ic_backspace to { RinTerminal.clear() },
            R.drawable.rs_ic_sparkle to { toggleBackgroundAnimation() },
            R.drawable.rs_ic_fullscreen to { toggleFullscreen() }
        )) {
            val b = chipIcon(icon, 14)
            b.minWidth = dp(28)
            b.setPadding(dp(6), 0, dp(6), 0)
            b.setOnClickListener { action() }
            header.addView(b, LayoutParams(LayoutParams.WRAP_CONTENT, dp(22)).apply { marginStart = dp(4) })
        }
    }

    private fun updateStatus(waiting: Boolean, continuing: Boolean, running: Boolean) {
        val native = RinNativeTerminal.nativeMode
        val (label, color) = when {
            waiting -> "INPUT" to ACCENT
            running || RinNativeTerminal.isBusy() -> "RUNNING" to CONT_YELLOW
            continuing -> "MULTI-LINE" to CONT_YELLOW
            else -> "READY" to OK_GREEN
        }
        statusDot.setTextColor(color)
        statusLabel.text = label
        val lines = RinTerminal.buffer.snapshot().lines.size
        statusInfo.text = (if (native) "NATIVE · " else "REPL · ") + lines + " ln · " + fontSp.toInt() + "sp"
    }

    /** اسم المجلد الحالي للموجّه (مجلد المشروع)، أو `~`. */
    private fun cwdName(): String =
        try { RinEngine.currentBaseDir().trimEnd('/').substringAfterLast('/').ifEmpty { "~" } } catch (t: Throwable) { "~" }

    /** موجّه كأي shell: `rin@studio:project$ ` (مستخدم بالأخضر، مجلد بالأزرق). */
    private fun promptSpan(): SpannableStringBuilder = SpannableStringBuilder().also {
        it.span("rin@studio", PROMPT_GREEN, bold = true)
        it.span(":", WHITE)
        it.span(cwdName(), ANSWER_BLUE, bold = true)
        it.span("$ ", WHITE)
    }

    // ---- سطر التنفيذ الحيّ: مؤشر Braille دوّار + الزمن المنقضي (مثل pip/npm) ----

    private fun isRunningNow(): Boolean = deliver == null && (RinJobScheduler.isRunning() || RinNativeTerminal.isBusy())

    private fun syncTicker() {
        if (isRunningNow()) {
            if (!tickerOn) {
                tickerOn = true
                runStartedAt = SystemClock.elapsedRealtime()
                activity.visibility = View.VISIBLE
                ticker.post(tickRunnable)
            }
        } else if (tickerOn) stopTicker()
    }

    private fun stopTicker() {
        tickerOn = false
        ticker.removeCallbacks(tickRunnable)
        activity.visibility = View.GONE
    }

    private fun updateMode() {
        val waiting = deliver != null
        val continuing = !waiting && (RinTerminal.repl.isContinuing || RinNativeTerminal.continuing())
        promptLabel.text = when {
            waiting -> SpannableStringBuilder().also { it.span("› ", ACCENT, bold = true) }
            continuing -> SpannableStringBuilder().also { it.span("> ", CONT_YELLOW, bold = true) }   // مثل PS2 في bash
            else -> promptSpan()
        }
        field.hint = context.getString(
            when { waiting -> R.string.terminal_hint_input; continuing -> R.string.terminal_hint_continue; else -> R.string.terminal_hint_shell }
        )
        val shellOnly = if (waiting) View.GONE else View.VISIBLE
        btnTab.visibility = shellOnly
        btnUp.visibility = shellOnly
        btnDown.visibility = shellOnly
        val running = !waiting && RinJobScheduler.isRunning()
        updateStatus(waiting, continuing, running)
        syncTicker()
        btnCtrlC.setTextColor(if (waiting || running) ERROR_RED else if (continuing) CONT_YELLOW else DIM)
        inputBox.background = GradientDrawable().apply {
            cornerRadius = dp(10).toFloat()
            setColor(INPUT_BG)
            setStroke(dp(if (waiting) 2 else 1), if (waiting) ACCENT else if (continuing) CONT_YELLOW else BORDER)
        }
    }

    private fun refresh(force: Boolean = false) {
        val snap = RinTerminal.buffer.snapshot()
        if (!force && snap.version == lastVersion) return
        lastVersion = snap.version
        screen.text = decorate(render(snap))
        updateMode()
        if (stickToBottom) scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
    }


    // ---------------------------------------------------------------- الرسومات (إطارات/شعار)

    private fun isArtLine(t: CharSequence, from: Int, to: Int): Boolean {
        for (i in from until to) { val c = t[i]; if (c in '\u2500'..'\u259F') return true }
        return false
    }

    /** يزيل تباعد الأسطر عن الرسومات (فتتصل خطوط الإطار) ويلائم عرضها مع الشاشة ويُضيف التباعد للنص العادي. */
    private fun decorate(src: CharSequence): CharSequence {
        val sb = if (src is SpannableStringBuilder) src else SpannableStringBuilder(src)
        val avail = (screen.width - screen.paddingLeft - screen.paddingRight).toFloat()
        artPaint.textSize = screen.textSize
        val cw = artPaint.measureText("M")
        var i = 0
        val n = sb.length
        while (i < n) {
            var e = i
            while (e < n && sb[e] != '\n') e++
            val end = if (e < n) e + 1 else e
            if (end > i) {
                if (isArtLine(sb, i, e)) {
                    sb.setSpan(TypefaceSpan(artTypeface), i, end, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                    if (avail > 0f && cw > 0f && (e - i) * cw > avail) {
                        val scale = (avail / ((e - i) * cw)).coerceIn(0.45f, 1f)
                        sb.setSpan(RelativeSizeSpan(scale), i, end, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                    }
                } else {
                    sb.setSpan(TextLineSpacing(lineRatio, dp(1)), i, end, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                }
            }
            i = end
        }
        return sb
    }

    /** تباعد أسطر للنص العادي فقط (نسبة من ارتفاع السطر + مقدار ثابت). */
    private class TextLineSpacing(private val ratio: Float, private val extraPx: Int) : LineHeightSpan {
        override fun chooseHeight(text: CharSequence, start: Int, end: Int, spanstartv: Int, lineHeight: Int, fm: Paint.FontMetricsInt) {
            val extra = ((fm.descent - fm.ascent) * ratio).toInt() + extraPx
            fm.descent += extra
            fm.bottom += extra
        }
    }

    private fun render(snap: TermSnapshot): CharSequence {
        val sb = SpannableStringBuilder()
        if (snap.lines.isEmpty() && snap.partial.isEmpty()) {
            sb.ansi(bannerText() + "\n\n", FG)
            sb.span(context.getString(R.string.terminal_welcome), DIM, italic = true)
            return sb
        }
        // الرسم على آخر RENDER_MAX سطراً فقط (سجل ضخم يُعاد بناؤه كل ~60ms أثناء البث)؛ Copy/Share يأخذان الكل.
        val hidden = (snap.lines.size - RENDER_MAX).coerceAtLeast(0)
        if (hidden > 0) sb.span(context.getString(R.string.terminal_hidden_lines, hidden) + "\n", DIM)
        for (idx in hidden until snap.lines.size) {
            val line = snap.lines[idx]
            when (line.role) {
                TermRole.COMMAND -> {
                    // صدى الأمر كما في shell حقيقي: موجّه كامل ثم الأمر.
                    sb.append(promptSpan())
                    sb.span(line.text, WHITE)
                }
                TermRole.CONT -> {
                    sb.span("> ", CONT_YELLOW, bold = true)
                    sb.span(line.text, WHITE)
                }
                TermRole.SYSTEM -> sb.span(line.text, DIM, italic = true)
                TermRole.SUCCESS -> { if (!hasStatusGlyph(line.text)) sb.span("✔ ", OK_GREEN, bold = true); sb.span(line.text, OK_GREEN) }
                TermRole.ERROR -> { if (!hasStatusGlyph(line.text)) sb.span("✖ ", ERROR_RED, bold = true); sb.span(line.text, ERROR_RED) }
                TermRole.OUTPUT -> {
                    val color = colorFor(line.kind)
                    val errLine = line.errorLine
                    if (errLine != null) {
                        sb.link(RinAnsi.strip(line.text), ERROR_RED) { onGoToLine?.invoke(errLine) }
                    } else if (line.answerFrom in 0..line.text.length) {
                        sb.ansi(line.text.substring(0, line.answerFrom), color)
                        sb.span(RinAnsi.strip(line.text.substring(line.answerFrom)), ANSWER_BLUE, bold = true)
                    } else {
                        sb.ansi(line.text, color)
                    }
                }
            }
            sb.append('\n')
        }
        // السطر الجاري (غالباً نص سؤال ينتظر جواباً) + مؤشر كتابة أثناء الانتظار.
        sb.ansi(snap.partial, FG)
        if (deliver != null) sb.span("▋", ACCENT)
        return sb
    }

    /** أسطر الحالة من [RinTerminal] تحمل رمزها أصلاً (✓ ✗ ⏱)؛ لا نكرّره. */
    private fun hasStatusGlyph(t: String): Boolean = t.isNotEmpty() && t[0] in "✓✗⏱✔✖"

    private fun colorFor(kind: LogKind): Int =
        if (kind == LogKind.PLAIN) FG else ContextCompat.getColor(context, kind.colorRes)

    private fun SpannableStringBuilder.span(text: String, color: Int, bold: Boolean = false, italic: Boolean = false) {
        if (text.isEmpty()) return
        val start = length
        append(text)
        setSpan(ForegroundColorSpan(RinTheme.readable(color, BG)), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)   // ألوان ANSI الداكنة-الأصل تُعتَّم على السمات الفاتحة
        if (bold) setSpan(StyleSpan(Typeface.BOLD), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (italic) setSpan(StyleSpan(Typeface.ITALIC), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * نص فيه أكواد ANSI (مثل `colorAnsi`) يُرسم بألوانه؛ ما بلا لون صريح يأخذ [default].
     * المقاطع تمرّ على [span] نفسها، ويُضاف الخط المائل/التسطير/الخلفية فقط حين تُطلب.
     */
    private fun SpannableStringBuilder.ansi(text: String, default: Int) {
        for (run in RinAnsi.parse(text)) {
            val start = length
            span(run.text, run.fg ?: default, run.bold)
            if (length == start) continue
            run.bg?.let { setSpan(BackgroundColorSpan(it), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE) }
            if (run.italic) setSpan(StyleSpan(Typeface.ITALIC), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            if (run.underline) setSpan(UnderlineSpan(), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    /** نص قابل للنقر بلون ثابت وتحته خط (لا لون الرابط الافتراضي). */
    private fun SpannableStringBuilder.link(text: String, color: Int, onClick: () -> Unit) {
        if (text.isEmpty()) return
        val start = length
        append(text)
        setSpan(object : ClickableSpan() {
            override fun onClick(widget: View) = onClick()
            override fun updateDrawState(ds: TextPaint) {
                ds.color = color
                ds.isUnderlineText = true
            }
        }, start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** زر أيقونة (vector) بدل رمز نصي. */
    private fun chipIcon(@androidx.annotation.DrawableRes res: Int, sizeDp: Int): TextView =
        chip("").also { RsIcons.only(it, res, sizeDp, DIM) }

    private fun chip(label: String): TextView = TextView(context).apply {
        chips.add(this)
        text = label
        gravity = Gravity.CENTER
        typeface = Typeface.MONOSPACE
        setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12f)
        setTextColor(DIM)
        minWidth = dp(34)
        setPadding(dp(8), 0, dp(8), 0)
        isClickable = true
        isFocusable = false   // لا يسرق التركيز من الحقل فتختفي لوحة المفاتيح
        restyleChip(this)
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private companion object {
        val SPINNER = arrayOf("⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏")
        const val KEY_FONT = "font_sp"
        const val KEY_THEME = "theme"
        const val KEY_BG_DIM = "bg_dim"
        const val KEY_BG_MODE = "bg_mode"
        const val KEY_BG_ANIM = "bg_anim"
        const val KEY_SPACING = "spacing"
        const val FONT_MIN = 9f
        const val FONT_MAX = 24f
        const val RENDER_MAX = 800

        const val KEY_HISTORY = "history"
        const val HISTORY_SEP = "\u0001"

        /** `!!` أو `!N` فقط (لا يلتبس مع نفي Rin `!flag`). */
        val HISTORY_REF = Regex("^!(!|\\d+)$")

        /** رموز معاملات ocr: "نص بين اقتباسين مزدوجين" (مجموعة 1) أو 'مفردين' (مجموعة 2) أو كلمة بلا مسافات. */
        val OCR_TOKEN = Regex("\"([^\"]*)\"|'([^']*)'|\\S+")

        /** رمز لغة tesseract: eng أو eng+ara (أحرف وأرقام و _ و + فقط، فلا تدخل نص Rin شيفرةً). */
        val OCR_LANG = Regex("^[A-Za-z0-9_+]{2,40}$")

        /** رقم صحيح غير سالب (psm / min). */
        val OCR_NUMBER = Regex("^\\d{1,3}$")

        /** `$ cmd` / `$cmd` — أوامر طرفية Linux الإضافية (الـ 50 أمراً). */
        val DOLLAR_CMD = Regex("^\\$\\s*[A-Za-z?]")

        /** الأوامر الأساسية تُقترح بالإكمال؛ البدائل (cls/vars/cancel/?) تُقبل عند الكتابة فقط. */
        val SHELL_COMMANDS = listOf(
            "help", "run", "stop", "clear", "history", "version", "reset", "session", "grep", "goto", "ocr", "share", "copy",
            "pwd", "ls", "tree", "cat", "alias", "unalias", "fullscreen", "theme", "size", "spacing", "style", "background", "banner", "libs", "lib", "about", "colors", "font", "echo", "native", "find", "head", "tail", "wc", "log", "cls", "vars", "cancel", "?"
        )
        /** أوامر ملفات أسماؤها شائعة كمتغيرات؛ تُعامَل كشيفرة إن تلاها `=` أو `(`. */
        val FILE_TOOLS = setOf("find", "head", "tail", "wc", "log")
        val COMMANDS_WITH_ARG = setOf("grep", "goto", "ocr", "native", "lib", "font", "echo", "ls", "tree", "cat", "alias", "unalias", "theme", "size", "spacing", "background", "find", "head", "tail", "wc", "help", "history")
        val ALIAS_ONLY = setOf("cls", "vars", "cancel")

        // الterminal داكن دائماً (مثل أي terminal) بغضّ النظر عن سمة التطبيق.
        var BG = 0xFF0D1117.toInt()
        var INPUT_BG = 0xFF161B22.toInt()
        var CHIP_BG = 0xFF21262D.toInt()
        var BORDER = 0xFF30363D.toInt()
        var HEADER_BG = 0xFF010409.toInt()
        var CMD_BG = 0xFF151B23.toInt()
        var FG = 0xFFC9D1D9.toInt()
        var WHITE = 0xFFF0F6FC.toInt()
        var DIM = 0xFF6E7681.toInt()
        var ACCENT = 0xFF8A7CFF.toInt()
        var ANSWER_BLUE = 0xFF79C0FF.toInt()
        var PROMPT_GREEN = 0xFF56D364.toInt()
        var OK_GREEN = 0xFF3FB950.toInt()
        var ERROR_RED = 0xFFF85149.toInt()
        var CONT_YELLOW = 0xFFD29922.toInt()
    }
}
