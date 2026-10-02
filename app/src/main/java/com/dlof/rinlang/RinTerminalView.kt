package com.dlof.rinlang

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.TextPaint
import android.text.method.LinkMovementMethod
import android.text.style.ClickableSpan
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
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

    private val prefs = context.getSharedPreferences("rin_terminal", Context.MODE_PRIVATE)
    private val scroll = ScrollView(context)
    private val screen = TextView(context)
    private val promptLabel = TextView(context)
    private val field = EditText(context)
    private val btnCtrlC = chip("^C")
    private val btnTab = chip("⇥")
    private val btnUp = chip("↑")
    private val btnDown = chip("↓")
    private val btnSend = chip("⏎")
    private val inputBox = LinearLayout(context)

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
            }
            return true
        }
        override fun onScaleEnd(d: ScaleGestureDetector) {
            prefs.edit().putFloat(KEY_FONT, fontSp).apply()
        }
    })

    init {
        orientation = VERTICAL
        layoutDirection = LAYOUT_DIRECTION_LTR   // الموجّه والأزرار ثابتة؛ اتجاه كل سطر نصي يُحدَّد بحسب لغته
        setBackgroundColor(BG)

        screen.apply {
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, fontSp)
            typeface = Typeface.MONOSPACE
            setTextColor(FG)
            setLineSpacing(0f, 1.12f)
            textDirection = View.TEXT_DIRECTION_FIRST_STRONG
            setPadding(dp(12), dp(8), dp(12), dp(8))
            movementMethod = LinkMovementMethod.getInstance()   // أسطر الأخطاء قابلة للنقر
            highlightColor = 0x00000000
            setOnClickListener { focusInput() }
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
        addView(scroll, LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f))

        promptLabel.apply {
            typeface = Typeface.MONOSPACE
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 14f)
            setTypeface(typeface, Typeface.BOLD)
            setPadding(dp(10), 0, dp(6), 0)
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
        RinTerminal.buffer.restoreHistory(loadHistory())
        updateMode()
        refresh(force = true)
    }

    override fun onDetachedFromWindow() {
        if (RinTerminal.onChanged === changeListener) RinTerminal.onChanged = null
        // السؤال المعلّق لا يُلغى هنا: [RinInputBridge] يعيد عرضه على الواجهة التالية.
        deliver = null
        super.onDetachedFromWindow()
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
        if (line.isBlank() && !repl.isContinuing) return

        // أوامر الـ shell لا تُقبل في منتصف أمر متعدد الأسطر (قد تكون جزءاً من الشيفرة).
        if (!repl.isContinuing) {
            // `!!` = آخر أمر، `!N` = الأمر رقم N في `history` (يُعرض الأمر المُوسَّع قبل تنفيذه).
            expandHistory(line)?.let { expanded ->
                if (expanded.isEmpty()) {
                    RinTerminal.command(line)
                    RinTerminal.system(context.getString(R.string.terminal_history_none), TermRole.ERROR)
                    return
                }
                line = expanded
            }
            val parts = line.trim().split(Regex("\\s+"), limit = 2)
            val cmd = parts[0].lowercase()
            val arg = parts.getOrNull(1)?.trim().orEmpty()
            if (cmd in SHELL_COMMANDS && (arg.isEmpty() || cmd in COMMANDS_WITH_ARG)) {
                RinTerminal.command(line.trim())
                saveHistory()
                runBuiltin(cmd, arg)
                return
            }
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

    /** null = ليس أمر تاريخ؛ "" = أمر تاريخ لكن لا يوجد ما يطابقه؛ غير ذلك = الأمر المُوسَّع. */
    private fun expandHistory(line: String): String? {
        val m = HISTORY_REF.matchEntire(line.trim()) ?: return null
        val h = RinTerminal.buffer.history()
        val ref = m.groupValues[1]
        return if (ref == "!") h.lastOrNull().orEmpty()
        else h.getOrNull((ref.toIntOrNull() ?: 0) - 1).orEmpty()
    }

    private fun runBuiltin(cmd: String, arg: String) {
        when (cmd) {
            "help", "?" -> RinTerminal.system(context.getString(R.string.terminal_help))
            "clear", "cls" -> RinTerminal.clear()
            "run" -> onRunRequested?.invoke()
            "history" -> {
                val h = RinTerminal.buffer.history()
                RinTerminal.system(
                    if (h.isEmpty()) context.getString(R.string.terminal_empty)
                    else h.mapIndexed { i, s -> "${(i + 1).toString().padStart(3)}  $s" }.joinToString("\n")
                )
            }
            "version" -> RinTerminal.system(
                try { RinEngine.engineVersion() } catch (t: Throwable) { "engine unavailable" }
            )
            "reset" -> {
                RinTerminal.repl.reset()
                RinTerminal.system(context.getString(R.string.terminal_session_reset))
            }
            "session", "vars" -> {
                val names = RinTerminal.repl.declaredNames()
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

    private fun updateMode() {
        val waiting = deliver != null
        val continuing = !waiting && RinTerminal.repl.isContinuing
        promptLabel.text = when { waiting -> "›"; continuing -> "…"; else -> "$" }
        promptLabel.setTextColor(
            when { waiting -> ACCENT; continuing -> CONT_YELLOW; else -> PROMPT_GREEN }
        )
        field.hint = context.getString(
            when { waiting -> R.string.terminal_hint_input; continuing -> R.string.terminal_hint_continue; else -> R.string.terminal_hint_shell }
        )
        val shellOnly = if (waiting) View.GONE else View.VISIBLE
        btnTab.visibility = shellOnly
        btnUp.visibility = shellOnly
        btnDown.visibility = shellOnly
        val running = !waiting && RinJobScheduler.isRunning()
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
        screen.text = render(snap)
        updateMode()
        if (stickToBottom) scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun render(snap: TermSnapshot): CharSequence {
        val sb = SpannableStringBuilder()
        if (snap.lines.isEmpty() && snap.partial.isEmpty()) {
            sb.span(context.getString(R.string.terminal_welcome), DIM)
            return sb
        }
        // الرسم على آخر RENDER_MAX سطراً فقط (سجل ضخم يُعاد بناؤه كل ~60ms أثناء البث)؛ Copy/Share يأخذان الكل.
        val hidden = (snap.lines.size - RENDER_MAX).coerceAtLeast(0)
        if (hidden > 0) sb.span(context.getString(R.string.terminal_hidden_lines, hidden) + "\n", DIM)
        for (idx in hidden until snap.lines.size) {
            val line = snap.lines[idx]
            when (line.role) {
                TermRole.COMMAND -> {
                    sb.span("$ ", PROMPT_GREEN, bold = true)
                    sb.span(line.text, WHITE)
                }
                TermRole.CONT -> {
                    sb.span("… ", CONT_YELLOW, bold = true)
                    sb.span(line.text, WHITE)
                }
                TermRole.SYSTEM -> sb.span(line.text, DIM)
                TermRole.SUCCESS -> sb.span(line.text, OK_GREEN)
                TermRole.ERROR -> sb.span(line.text, ERROR_RED)
                TermRole.OUTPUT -> {
                    val color = colorFor(line.kind)
                    val errLine = line.errorLine
                    if (errLine != null) {
                        sb.link(line.text, ERROR_RED) { onGoToLine?.invoke(errLine) }
                    } else if (line.answerFrom in 0..line.text.length) {
                        sb.span(line.text.substring(0, line.answerFrom), color)
                        sb.span(line.text.substring(line.answerFrom), ANSWER_BLUE, bold = true)
                    } else {
                        sb.span(line.text, color)
                    }
                }
            }
            sb.append('\n')
        }
        // السطر الجاري (غالباً نص سؤال ينتظر جواباً) + مؤشر كتابة أثناء الانتظار.
        sb.span(snap.partial, FG)
        if (deliver != null) sb.span("▋", ACCENT)
        return sb
    }

    private fun colorFor(kind: LogKind): Int =
        if (kind == LogKind.PLAIN) FG else ContextCompat.getColor(context, kind.colorRes)

    private fun SpannableStringBuilder.span(text: String, color: Int, bold: Boolean = false) {
        if (text.isEmpty()) return
        val start = length
        append(text)
        setSpan(ForegroundColorSpan(color), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (bold) setSpan(StyleSpan(Typeface.BOLD), start, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
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

    private fun chip(label: String): TextView = TextView(context).apply {
        text = label
        gravity = Gravity.CENTER
        typeface = Typeface.MONOSPACE
        setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12f)
        setTextColor(DIM)
        minWidth = dp(34)
        setPadding(dp(8), 0, dp(8), 0)
        isClickable = true
        isFocusable = false   // لا يسرق التركيز من الحقل فتختفي لوحة المفاتيح
        background = GradientDrawable().apply {
            cornerRadius = dp(8).toFloat()
            setColor(CHIP_BG)
        }
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private companion object {
        const val KEY_FONT = "font_sp"
        const val FONT_MIN = 9f
        const val FONT_MAX = 24f
        const val RENDER_MAX = 800

        const val KEY_HISTORY = "history"
        const val HISTORY_SEP = "\u0001"

        /** `!!` أو `!N` فقط (لا يلتبس مع نفي Rin `!flag`). */
        val HISTORY_REF = Regex("^!(!|\\d+)$")

        /** الأوامر الأساسية تُقترح بالإكمال؛ البدائل (cls/vars/cancel/?) تُقبل عند الكتابة فقط. */
        val SHELL_COMMANDS = listOf(
            "help", "run", "stop", "clear", "history", "version", "reset", "session", "grep", "goto", "share", "copy",
            "cls", "vars", "cancel", "?"
        )
        val COMMANDS_WITH_ARG = setOf("grep", "goto")
        val ALIAS_ONLY = setOf("cls", "vars", "cancel")

        // الterminal داكن دائماً (مثل أي terminal) بغضّ النظر عن سمة التطبيق.
        const val BG = 0xFF0D1117.toInt()
        const val INPUT_BG = 0xFF161B22.toInt()
        const val CHIP_BG = 0xFF21262D.toInt()
        const val BORDER = 0xFF30363D.toInt()
        const val FG = 0xFFC9D1D9.toInt()
        const val WHITE = 0xFFF0F6FC.toInt()
        const val DIM = 0xFF6E7681.toInt()
        const val ACCENT = 0xFF8A7CFF.toInt()
        const val ANSWER_BLUE = 0xFF79C0FF.toInt()
        const val PROMPT_GREEN = 0xFF56D364.toInt()
        const val OK_GREEN = 0xFF3FB950.toInt()
        const val ERROR_RED = 0xFFF85149.toInt()
        const val CONT_YELLOW = 0xFFD29922.toInt()
    }
}
