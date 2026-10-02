package com.dlof.rinlang

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.util.AttributeSet
import android.view.Gravity
import android.view.KeyEvent
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
 * terminal داخل المحرر: سجل مخرجات حيّ + سطر واحد في الأسفل له وضعان:
 *
 *  - **shell** (لا برنامج ينتظر): الموجّه `$`. أوامر: help · run · clear · history · version،
 *    وأي نص آخر يُنفَّذ كشيفرة Rin (مثل `print 1 + 2;`) عبر [onExecuteRequested].
 *  - **input** (برنامج Rin توقّف عند input/inputNumber/confirm/choose): الموجّه `›` وإطار مضيء؛
 *    ما يُكتب يُسلَّم للبرنامج مباشرةً، و`^C` يُلغي السؤال (يتوقف البرنامج بخطأ "تم إلغاء الإدخال").
 *
 * السجل نفسه في [RinTerminal] (لا يضيع بتدوير الشاشة)؛ هذا الصنف عرض فقط. السؤال يصله من
 * [RinInputBridge] عبر [beginProgramInput]، وجوابه يعود عبر `deliver` ثم يُسجَّله المُجدوِل في السجل.
 */
class RinTerminalView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : LinearLayout(context, attrs) {

    /** أمر `run`: تشغيل الملف المفتوح في المحرر. */
    var onRunRequested: (() -> Unit)? = null

    /** أي نص آخر في وضع shell: شيفرة Rin جاهزة للتنفيذ (أُضيفت لها `;` إن لزم). */
    var onExecuteRequested: ((String) -> Unit)? = null

    private val scroll = ScrollView(context)
    private val screen = TextView(context)
    private val promptLabel = TextView(context)
    private val field = EditText(context)
    private val btnCtrlC = chip("^C")
    private val btnUp = chip("↑")
    private val btnDown = chip("↓")
    private val btnSend = chip("⏎")
    private val inputBox = LinearLayout(context)

    /** غير null = برنامج ينتظر جواباً الآن (وضع input). */
    private var deliver: ((String?) -> Unit)? = null
    private var historyIndex = -1
    private var lastVersion = -1L
    private var stickToBottom = true

    private val changeListener: () -> Unit = { refresh() }

    init {
        orientation = VERTICAL
        layoutDirection = LAYOUT_DIRECTION_LTR   // الموجّه والأزرار ثابتة؛ اتجاه كل سطر نصي يُحدَّد بحسب لغته
        setBackgroundColor(BG)

        screen.apply {
            setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12.5f)
            typeface = Typeface.MONOSPACE
            setTextColor(FG)
            setLineSpacing(0f, 1.12f)
            textDirection = View.TEXT_DIRECTION_FIRST_STRONG
            setPadding(dp(12), dp(8), dp(12), dp(8))
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
        btnUp.setOnClickListener { stepHistory(+1) }
        btnDown.setOnClickListener { stepHistory(-1) }
        btnSend.setOnClickListener { submit() }

        inputBox.apply {
            orientation = HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(promptLabel, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
            addView(field, LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f))
            for (b in listOf(btnCtrlC, btnUp, btnDown, btnSend)) {
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
        runShell(text)
    }

    private fun cancelOrClear() {
        val d = deliver
        if (d != null) {
            deliver = null
            field.setText("")
            updateMode()
            d(null)
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

    private fun runShell(raw: String) {
        val cmd = raw.trim()
        if (cmd.isEmpty()) return
        RinTerminal.command(cmd)
        when (cmd.lowercase()) {
            "help", "?" -> RinTerminal.system(context.getString(R.string.terminal_help))
            "clear", "cls" -> RinTerminal.clear()
            "run" -> onRunRequested?.invoke()
            "history" -> {
                val h = RinTerminal.buffer.history()
                RinTerminal.system(
                    if (h.isEmpty()) "(empty)"
                    else h.mapIndexed { i, s -> "${(i + 1).toString().padStart(3)}  $s" }.joinToString("\n")
                )
            }
            "version" -> RinTerminal.system(
                try { RinEngine.engineVersion() } catch (t: Throwable) { "engine unavailable" }
            )
            "stop", "cancel" -> RinTerminal.system(context.getString(R.string.terminal_nothing_waiting))
            else -> {
                val code = if (cmd.endsWith(";") || cmd.endsWith("}")) cmd else "$cmd;"
                onExecuteRequested?.invoke(code)
            }
        }
    }

    // ---- الرسم ----

    private fun updateMode() {
        val waiting = deliver != null
        promptLabel.text = if (waiting) "›" else "$"
        promptLabel.setTextColor(if (waiting) ACCENT else PROMPT_GREEN)
        field.hint = context.getString(if (waiting) R.string.terminal_hint_input else R.string.terminal_hint_shell)
        btnUp.visibility = if (waiting) View.GONE else View.VISIBLE
        btnDown.visibility = if (waiting) View.GONE else View.VISIBLE
        btnCtrlC.setTextColor(if (waiting) ERROR_RED else DIM)
        inputBox.background = GradientDrawable().apply {
            cornerRadius = dp(10).toFloat()
            setColor(INPUT_BG)
            setStroke(dp(if (waiting) 2 else 1), if (waiting) ACCENT else BORDER)
        }
    }

    private fun refresh(force: Boolean = false) {
        val snap = RinTerminal.buffer.snapshot()
        if (!force && snap.version == lastVersion) return
        lastVersion = snap.version
        screen.text = render(snap)
        if (stickToBottom) scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun render(snap: TermSnapshot): CharSequence {
        val sb = SpannableStringBuilder()
        if (snap.lines.isEmpty() && snap.partial.isEmpty()) {
            sb.span(context.getString(R.string.terminal_welcome), DIM)
            return sb
        }
        for (line in snap.lines) {
            when (line.role) {
                TermRole.COMMAND -> {
                    sb.span("$ ", PROMPT_GREEN, bold = true)
                    sb.span(line.text, WHITE)
                }
                TermRole.SYSTEM -> sb.span(line.text, DIM)
                TermRole.SUCCESS -> sb.span(line.text, OK_GREEN)
                TermRole.ERROR -> sb.span(line.text, ERROR_RED)
                TermRole.OUTPUT -> {
                    val color = colorFor(line.kind)
                    if (line.answerFrom in 0..line.text.length) {
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

    private fun chip(label: String): TextView = TextView(context).apply {
        text = label
        gravity = Gravity.CENTER
        typeface = Typeface.MONOSPACE
        setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12f)
        setTextColor(DIM)
        minWidth = dp(34)
        setPadding(dp(8), 0, dp(8), 0)
        isClickable = true
        isFocusable = false   // لا يسرق التركيز من الحقل فيختفي لوحة المفاتيح
        background = GradientDrawable().apply {
            cornerRadius = dp(8).toFloat()
            setColor(CHIP_BG)
        }
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private companion object {
        // الـ terminal داكن دائماً (مثل أي terminal) بغضّ النظر عن سمة التطبيق.
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
    }
}
