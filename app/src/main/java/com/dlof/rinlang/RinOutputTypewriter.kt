package com.dlof.rinlang

import java.text.BreakIterator

/** نوع صف في شاشة Output. */
enum class OutRowKind {
    /** سطر ناتج عادي من البرنامج (له رقم في الهامش). */
    LINE,
    /** سطر `print.image(...)`: يُكتب اسم الملف ثم تظهر الصورة المصغّرة. */
    IMAGE,
    /** سؤال إدخال (input/confirm/choose) يطرحه البرنامج. */
    PROMPT,
    /** جواب المستخدم على السؤال السابق (يظهر فوراً، فالمستخدم كتبه بنفسه). */
    ANSWER,
    /** سطر حالة النهاية: «اكتمل بنجاح · 12ms». */
    STATUS
}

/**
 * صف واحد قابل للكتابة التدريجية. [text] هو النص الكامل؛ [stops] حدود «الحروف المرئية» (graphemes)
 * فلا تُقطَع كلمة عربية متصلة أو إيموجي مركّب في منتصفه. [typed] = عدد الخطوات المكتوبة حتى الآن.
 */
class OutRow(
    val kind: OutRowKind,
    val line: RinLogLine,
    val text: String,
    /** رقم السطر في الهامش (0 = بلا رقم). */
    val number: Int,
    /** عمق التداخل داخل الحاويات (يحدّد الإزاحة). */
    val depth: Int,
    /** لون عائلة الحاوية المحيطة (ARGB) أو null خارج أي حاوية. */
    val railColor: Int?,
    private val stops: IntArray
) {
    var typed: Int = 0
        internal set

    val total: Int get() = stops.size
    val isDone: Boolean get() = typed >= stops.size

    /** النص الظاهر حالياً (مقطوع عند آخر حرف مرئي مكتوب). */
    fun shownText(): String = when {
        typed <= 0 -> ""
        typed >= stops.size -> text
        else -> text.substring(0, stops[typed - 1])
    }

    internal fun finishNow() { typed = stops.size }

    companion object {
        /** يحسب حدود الحروف المرئية؛ لا يرمي أبداً (عند أي استثناء يعود لحدود كل code unit). */
        fun graphemeStops(text: String): IntArray {
            if (text.isEmpty()) return IntArray(0)
            return try {
                val it = BreakIterator.getCharacterInstance()
                it.setText(text)
                val out = ArrayList<Int>(text.length)
                var end = it.next()
                while (end != BreakIterator.DONE) {
                    out.add(end)
                    end = it.next()
                }
                if (out.isEmpty()) out.add(text.length)
                out.toIntArray()
            } catch (t: Throwable) {
                IntArray(text.length) { it + 1 }
            }
        }
    }
}

/** سرعة الكتابة: حروف/ثانية أساسية وفاصل توقّف بين سطرين (مللي ثانية). */
enum class TypeSpeed(val charsPerSecond: Double, val linePauseMs: Long) {
    SLOW(55.0, 110L),
    NORMAL(120.0, 45L),
    FAST(340.0, 10L),
    INSTANT(0.0, 0L);

    fun next(): TypeSpeed = values()[(ordinal + 1) % values().size]

    companion object {
        fun fromName(name: String?): TypeSpeed =
            values().firstOrNull { it.name == name } ?: NORMAL
    }
}

/**
 * محرّك الكتابة الحيّة لشاشة Output (بلا أي اعتماد على الواجهة؛ تقوده [OutputActivity] بإطار
 * Choreographer واحد لكل شاشة).
 *
 * - يحافظ على قائمة صفوف [rows]؛ كل صف يُكتب حرفاً حرفاً بالترتيب (صف واحد فقط يُكتب في كل لحظة).
 * - السرعة تتكيّف مع المتراكم: لا تقلّ عن سرعة [speed] الأساسية، وترتفع بنسبة المتراكم (÷ [DRAIN_SECONDS])
 *   فلا ينتظر المستخدم دقائق حتى يُكتب ناتج طويل، مع بقاء ناتج قصير بسرعة قراءة مريحة.
 * - بين سطرين توقّف قصير يعطي إيقاعاً طبيعياً، ويُلغى تلقائياً عند وجود متراكم كبير.
 * - الصفوف الأقدم من آخر [ANIMATED_TAIL] صفاً تظهر مكتملة فوراً عند التحميل الأول (ناتج 5000 سطر لا
 *   يُكتب كله حرفاً حرفاً).
 */
class RinOutputTypewriter {

    companion object {
        /** ثابت زمن تفريغ المتراكم: السرعة = المتراكم ÷ هذا الرقم (تتناقص أُسّياً)، فيُفرَّغ ناتج طويل في
         *  بضع ثوانٍ فقط (يتزايد الزمن لوغاريتمياً مع الحجم) بدل أن يتزايد خطياً. */
        const val DRAIN_SECONDS = 0.6
        /** عند التحميل الأول لناتج جاهز: عدد الصفوف الأخيرة فقط التي تُكتب بالحركة. */
        const val ANIMATED_TAIL = 120
        /** فوق هذا المتراكم (بالخطوات) يُلغى توقّف نهاية السطر. */
        private const val NO_PAUSE_BACKLOG = 90
        /** أقصى زمن إطار يُحتسب (حتى لا تقفز الكتابة بعد توقّف التطبيق مؤقتاً). */
        private const val MAX_FRAME_MS = 100L
    }

    /** نتيجة [step]: النطاق المتغيّر من الصفوف (أو null إن لم يتغيّر شيء). */
    class Change(val from: Int, val to: Int)

    val rows = ArrayList<OutRow>()

    var speed: TypeSpeed = TypeSpeed.NORMAL

    /** فهرس أول صف لم يكتمل (= الصف الجاري كتابته)، أو rows.size إن اكتمل الكل. */
    var cursorRow: Int = 0
        private set

    /** عدد الخطوات المتبقية في كل الصفوف غير المكتملة. */
    private var pendingSteps = 0L

    private var carry = 0.0
    private var pauseLeftMs = 0.0
    private var nextNumber = 1

    private val containerStack = ArrayList<Int>()

    /** يعيد كل شيء لحالة البداية (عند الانتقال إلى تشغيل آخر). */
    fun reset() {
        rows.clear()
        cursorRow = 0
        pendingSteps = 0
        carry = 0.0
        pauseLeftMs = 0.0
        nextNumber = 1
        containerStack.clear()
    }

    val isTyping: Boolean get() = cursorRow < rows.size

    /** عدد أسطر الناتج (LINE/IMAGE) المضافة حتى الآن. */
    var lineCount: Int = 0
        private set

    /**
     * يضيف أسطر ناتج جديدة. [instantCount] = عدد الصفوف الأولى من هذه الدفعة التي تظهر مكتملة بلا
     * حركة. يعيد النطاق المضاف (from..to حصرياً).
     */
    fun appendLines(lines: List<RinLogLine>, instantCount: Int = 0): IntRange {
        val start = rows.size
        lines.forEachIndexed { i, line -> addLineRow(line, i < instantCount) }
        return start until rows.size
    }

    /** يضيف صفاً اصطناعياً (سؤال/جواب/حالة). [instant] لجواب المستخدم. */
    fun appendSynthetic(kind: OutRowKind, line: RinLogLine, instant: Boolean = false): Int {
        val text = line.text
        val row = OutRow(kind, line, text, 0, depthNow(), containerStack.lastOrNull(), OutRow.graphemeStops(text))
        register(row, instant)
        return rows.size - 1
    }

    private fun depthNow(): Int = containerStack.size

    private fun addLineRow(line: RinLogLine, instant: Boolean) {
        val mark = line.container
        // سطر إغلاق حاوية يُرسم بنفس عمق سطر فتحها: نُخرج من المكدس قبل حساب العمق.
        if (mark != null && mark.role == ContainerRole.CLOSE && containerStack.isNotEmpty()) {
            containerStack.removeAt(containerStack.size - 1)
        }
        val text = displayText(line)
        val isImage = line.kind == LogKind.PRINT_IMAGE
        val row = OutRow(
            kind = if (isImage) OutRowKind.IMAGE else OutRowKind.LINE,
            line = line,
            text = text,
            number = nextNumber++,
            depth = depthNow(),
            railColor = if (mark != null && mark.role == ContainerRole.OPEN) mark.family.accent
            else containerStack.lastOrNull(),
            stops = OutRow.graphemeStops(text)
        )
        if (mark != null && mark.role == ContainerRole.OPEN) containerStack.add(mark.family.accent)
        lineCount++
        register(row, instant)
    }

    /** النص الذي يُكتب فعلياً للسطر: عناوين الحاويات تُصاغ بتسمية العائلة بدل الوسم الخام. */
    private fun displayText(line: RinLogLine): String {
        val mark = line.container
        val base = when {
            line.kind == LogKind.PRINT_IMAGE -> "print.image  " + (line.imageRelPath ?: line.text).substringAfterLast('/')
            mark != null && mark.role == ContainerRole.OPEN ->
                mark.family.label + (mark.name?.let { " · $it" } ?: "")
            mark != null && mark.role == ContainerRole.CLOSE -> buildString {
                append("◂ ").append(mark.family.label)
                mark.name?.let { append(" · ").append(it) }
                if (mark.members.isNotEmpty()) append("  —  ").append(mark.members.joinToString("، "))
            }
            else -> line.text
        }
        return if (line.repeat > 1) "$base  ×${line.repeat}" else base
    }

    private fun register(row: OutRow, instant: Boolean) {
        rows.add(row)
        if (instant || speed == TypeSpeed.INSTANT || row.total == 0) {
            row.finishNow()
        } else {
            pendingSteps += row.total
        }
        // الصف الجديد قد يكون هو الأول غير المكتمل.
        if (cursorRow == rows.size - 1 && row.isDone) cursorRow = rows.size
    }

    /** يُظهر كل شيء فوراً (زر التخطي أو اللمس). يعيد النطاق الذي تغيّر. */
    fun skipAll(): Change? {
        if (!isTyping) return null
        val from = cursorRow
        for (i in cursorRow until rows.size) rows[i].finishNow()
        cursorRow = rows.size
        pendingSteps = 0
        carry = 0.0
        pauseLeftMs = 0.0
        return Change(from, rows.size)
    }

    /**
     * يقدّم الكتابة بمقدار [dtMs] مللي ثانية. يعيد النطاق المتغيّر من الصفوف أو null.
     * في الوضع الفوري يُكمل كل شيء دفعة واحدة.
     */
    fun step(dtMs: Long): Change? {
        if (!isTyping) return null
        if (speed == TypeSpeed.INSTANT) return skipAll()

        var dt = dtMs.coerceIn(0L, MAX_FRAME_MS).toDouble()
        val from = cursorRow
        var changed = false

        // التوقّف بين سطرين يستهلك من زمن الإطار أولاً.
        if (pauseLeftMs > 0) {
            val used = minOf(pauseLeftMs, dt)
            pauseLeftMs -= used
            dt -= used
            if (dt <= 0.0) return null
        }

        val cps = maxOf(speed.charsPerSecond, pendingSteps / DRAIN_SECONDS)
        carry += cps * dt / 1000.0
        var budget = carry.toLong()
        carry -= budget.toDouble()

        var last = from
        while (budget > 0 && cursorRow < rows.size) {
            val row = rows[cursorRow]
            val left = (row.total - row.typed).toLong()
            val take = minOf(left, budget).toInt()
            row.typed += take
            pendingSteps -= take
            budget -= take
            changed = true
            last = cursorRow
            if (row.isDone) {
                cursorRow++
                if (pendingSteps < NO_PAUSE_BACKLOG && speed.linePauseMs > 0 && cursorRow < rows.size) {
                    pauseLeftMs = speed.linePauseMs.toDouble()
                    break
                }
            }
        }
        if (cursorRow >= rows.size) { carry = 0.0; pendingSteps = 0 }
        return if (changed) Change(from, maxOf(last + 1, minOf(cursorRow + 1, rows.size))) else null
    }
}
