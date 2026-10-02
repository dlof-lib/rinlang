package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import java.util.concurrent.atomic.AtomicBoolean

/** دور السطر داخل الـ terminal؛ يحدّد نمط العرض (اللون/الخط). */
enum class TermRole { OUTPUT, COMMAND, SYSTEM, SUCCESS, ERROR }

/**
 * سطر واحد في سجل الـ terminal.
 * [kind] يلوّن أسطر [TermRole.OUTPUT] بنفس تصنيف [RinConsoleFormatter] (خطأ/نجاح/حاوية...)،
 * و[answerFrom] ≥ 0 يعني أن النص من هذا الفهرس هو ما كتبه المستخدم جواباً لـ input().
 */
data class TermLine(
    val text: String,
    val role: TermRole,
    val kind: LogKind = LogKind.PLAIN,
    val answerFrom: Int = -1
)

/** لقطة ثابتة من السجل لرسمها: الأسطر المكتملة + السطر الجاري كتابته (مثل سؤال بلا سطر جديد بعد). */
data class TermSnapshot(val lines: List<TermLine>, val partial: String, val version: Long)

/**
 * سجل الـ terminal: منطق خالص بلا أي اعتماد على واجهة أندرويد، فيُختبر بـ JUnit مباشرةً.
 *
 * يُكتب إليه من ترد التشغيل (مخرجات حيّة، سؤال input، جوابه) ومن الواجهة (أوامر المستخدم)،
 * لذلك كل دالة عامة [Synchronized]. الترتيب مضمون لأن المحرك يفرغ كل ما طُبع قبل أن يسأل
 * (انظر askRaw في rin_interpreter.cpp)، فيظهر: مخرجات ← سؤال ← جواب ← مخرجات.
 */
class RinTerminalBuffer(
    private val maxLines: Int = 2000,
    private val maxTrackedChars: Int = 1_000_000,
    private val maxHistory: Int = 100
) {
    private val lines = ArrayList<TermLine>()
    private val partial = StringBuilder()
    private val history = ArrayList<String>()

    // ما بُثّ حيّاً للمهمة الحالية؛ يلزم عند الانتهاء لإكمال ما لم يُبثّ (أخطاء المحلّل مثلاً).
    private var streamed = StringBuilder()
    private var streamedChars = 0
    private var trackOverflow = false
    private var version = 0L

    @Synchronized
    fun snapshot(): TermSnapshot = TermSnapshot(lines.toList(), partial.toString(), version)

    @Synchronized
    fun history(): List<String> = history.toList()

    @Synchronized
    fun isEmpty(): Boolean = lines.isEmpty() && partial.isEmpty()

    /** كل السجل كنص عادي للنسخ؛ الأوامر تُسبق بـ "$ ". */
    @Synchronized
    fun toPlainText(): String {
        val sb = StringBuilder()
        for (l in lines) {
            if (l.role == TermRole.COMMAND) sb.append("$ ")
            sb.append(l.text).append('\n')
        }
        if (partial.isNotEmpty()) sb.append(partial).append('\n')
        return sb.toString().trimEnd('\n')
    }

    // ---- كتابة المخرجات ----

    /** يضيف [text] عند السطر الجاري؛ كل '\n' يُنهي سطراً. '\r' يُتجاهل. */
    @Synchronized
    fun write(text: String, role: TermRole = TermRole.OUTPUT) {
        if (text.isEmpty()) return
        for (ch in text) {
            when (ch) {
                '\n' -> commitPartial(role, -1)
                '\r' -> Unit
                else -> partial.append(ch)
            }
        }
        touch()
    }

    /** مخرجات المحرك الحيّة (تُسجَّل أيضاً لمقارنتها بالناتج النهائي عند الانتهاء). */
    @Synchronized
    fun onChunk(chunk: String) {
        write(chunk)
        streamedChars += chunk.length
        if (!trackOverflow) {
            streamed.append(chunk)
            if (streamed.length > maxTrackedChars) {
                trackOverflow = true
                streamed = StringBuilder()
            }
        }
    }

    @Synchronized
    fun startJob(number: Int) {
        flushPartial()
        streamed = StringBuilder()
        streamedChars = 0
        trackOverflow = false
        addLine(TermLine("── run #$number ──", TermRole.SYSTEM))
        touch()
    }

    /**
     * سؤال إدخال: يُكتب على السطر الجاري بلا سطر جديد، فيُكمل الجواب نفس السطر
     * كما في أي terminal حقيقي. السؤال المتعدد الأسطر (choose) تُقسَّم أسطره تلقائياً.
     */
    @Synchronized
    fun prompt(text: String) {
        write(if (text.isEmpty()) "› " else text)
    }

    /** جواب المستخدم (null = ألغى). يُنهي سطر السؤال. */
    @Synchronized
    fun answer(text: String?) {
        if (text == null) {
            partial.append("^C")
            commitPartial(TermRole.OUTPUT, -1)
            addLine(TermLine("input cancelled", TermRole.SYSTEM))
        } else {
            val from = partial.length
            partial.append(text.replace("\r", "").replace('\n', ' '))
            commitPartial(TermRole.OUTPUT, from)
        }
        touch()
    }

    /**
     * ختام المهمة: يكمل ما لم يُبثّ حيّاً من [output] (لا يُكرّر ما عُرض)، ثم يكتب سطر الحالة.
     * [output] للمهمة التي لم تبثّ شيئاً (خطأ في المحلّل/المعجم) يُعرض كاملاً.
     */
    @Synchronized
    fun finishJob(status: JobStatus, output: String, durationMs: Long) {
        flushPartial()
        if (streamedChars == 0) {
            if (output.isNotBlank()) writeBlock(output)
        } else if (!trackOverflow) {
            val s = streamed.toString().trimEnd()
            val o = output.trimEnd()
            if (o.length > s.length && o.startsWith(s)) {
                writeBlock(o.substring(s.length).trimStart('\n', '\r'))
            }
        }
        val ms = "${durationMs} ms"
        when (status) {
            JobStatus.SUCCESS -> addLine(TermLine("✓ finished in $ms", TermRole.SUCCESS))
            JobStatus.ERROR -> addLine(TermLine("✗ failed after $ms", TermRole.ERROR))
            JobStatus.TIMEOUT -> addLine(TermLine("⏱ timed out after $ms", TermRole.ERROR))
            JobStatus.CANCELLED -> addLine(TermLine("cancelled", TermRole.SYSTEM))
            else -> Unit
        }
        streamed = StringBuilder()
        streamedChars = 0
        touch()
    }

    // ---- أوامر الواجهة ----

    /** أمر كتبه المستخدم في سطر الأوامر؛ يُحفظ في التاريخ (بلا تكرار متتالٍ). */
    @Synchronized
    fun command(text: String) {
        flushPartial()
        addLine(TermLine(text, TermRole.COMMAND))
        if (history.lastOrNull() != text) {
            history.add(text)
            if (history.size > maxHistory) history.removeAt(0)
        }
        touch()
    }

    @Synchronized
    fun system(text: String, role: TermRole = TermRole.SYSTEM) {
        flushPartial()
        for (l in text.split('\n')) addLine(TermLine(l, role))
        touch()
    }

    @Synchronized
    fun clear() {
        lines.clear()
        partial.setLength(0)
        touch()
    }

    // ---- داخلي ----

    private fun writeBlock(text: String) {
        for (ch in text) {
            when (ch) {
                '\n' -> commitPartial(TermRole.OUTPUT, -1)
                '\r' -> Unit
                else -> partial.append(ch)
            }
        }
        flushPartial()
    }

    private fun flushPartial() {
        if (partial.isNotEmpty()) commitPartial(TermRole.OUTPUT, -1)
    }

    private fun commitPartial(role: TermRole, answerFrom: Int) {
        val s = partial.toString()
        partial.setLength(0)
        val kind = if (role == TermRole.OUTPUT) kindOf(s) else LogKind.PLAIN
        addLine(TermLine(s, role, kind, answerFrom))
    }

    private fun addLine(line: TermLine) {
        lines.add(line)
        // قصّ على دفعات (لا removeAt(0) لكل سطر) كي تبقى الكتابة O(1) تقريباً.
        if (lines.size > maxLines + 200) lines.subList(0, lines.size - maxLines).clear()
    }

    private fun touch() {
        version++
    }

    private fun kindOf(line: String): LogKind =
        if (line.isBlank()) LogKind.PLAIN
        else RinConsoleFormatter.formatLines(line).firstOrNull()?.kind ?: LogKind.PLAIN
}

/**
 * الـ terminal الوحيد في التطبيق (singleton يعيش أطول من الـ Activity، مثل [RinJobScheduler])،
 * فلا يضيع السجل ولا السؤال المعلّق عند تدوير الشاشة. الكتابة تأتي من أي ترد؛ إشعار الواجهة
 * يُجمَّع في مهلة قصيرة ويصل على الترد الرئيسي (لا إعادة رسم لكل سطر).
 */
object RinTerminal {

    private const val NOTIFY_BATCH_MS = 60L

    val buffer = RinTerminalBuffer()

    /** يضبطها [RinTerminalView] عند الإرفاق وتُصفَّر عند الفصل. تُستدعى على الترد الرئيسي. */
    @Volatile var onChanged: (() -> Unit)? = null

    /** التبويب المختار في ذيل المحرر (Terminal أو Runs)؛ static كي يبقى بعد تدوير الشاشة. */
    @Volatile var terminalTabSelected: Boolean = true

    private val main = Handler(Looper.getMainLooper())
    private val notifyPending = AtomicBoolean(false)

    private fun notifyChanged() {
        if (notifyPending.compareAndSet(false, true)) {
            main.postDelayed({
                notifyPending.set(false)
                onChanged?.invoke()
            }, NOTIFY_BATCH_MS)
        }
    }

    // ---- تُستدعى من RinJobScheduler (ترد التشغيل) ----
    fun onJobStart(number: Int) { buffer.startJob(number); notifyChanged() }
    fun onChunk(chunk: String) { buffer.onChunk(chunk); notifyChanged() }
    fun onPrompt(prompt: String) { buffer.prompt(prompt); notifyChanged() }
    fun onAnswer(answer: String?) { buffer.answer(answer); notifyChanged() }
    fun onJobFinish(status: JobStatus, output: String, durationMs: Long) {
        buffer.finishJob(status, output, durationMs); notifyChanged()
    }

    // ---- تُستدعى من الواجهة ----
    fun command(text: String) { buffer.command(text); notifyChanged() }
    fun system(text: String, role: TermRole = TermRole.SYSTEM) { buffer.system(text, role); notifyChanged() }
    fun clear() { buffer.clear(); notifyChanged() }
}
