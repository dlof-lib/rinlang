package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * طرفية Linux (`rin terminal`) داخل محرّر RinStudio.
 *
 * هذه هي نفس جلسة طرفية لينكس (cli/linux/src/terminal/rin_terminal.cpp) مبنيّة داخل `librinengine.so` عبر
 * `EmbeddedTerminal` (انظر app/src/main/cpp/terminal/rin_terminal_jni.cpp): مفسّر **دائم الحالة** (let/fun/class
 * تبقى بين الأوامر بلا إعادة تشغيل) مع أوامر `:` الكاملة — `:load :vars :type :time :check :ocr :img :cd :ls …`.
 * لا تعمل فيها `:sh` و`:edit` و`:pkg` و`:indsin` (تحتاج شلاً/tty) وتردّ برسالة واضحة.
 *
 * التوجيه (يقرّره [RinTerminalView.runShell]):
 *  - سطر يبدأ بـ `:` ← يذهب لهذه الجلسة؛
 *  - بعد `native on` ← كل سطر شيفرة يذهب لها بدل [RinRepl] القديمة (حالة حقيقية بدل إعادة تشغيل التصريحات)؛
 *  - أمر ناقص (قوس/نص مفتوح) في هذه الجلسة ← يكمل فيها.
 *
 * العقد: خيط عامل واحد (الجلسة ليست آمنة للخيوط)، والمخرجات وطلبات input() تصل على ذلك الخيط وتُكتب في
 * [RinTerminal] بنفس مسار [RinJobScheduler] (فتظهر الأسئلة في سطر الـ terminal عبر [RinInputBridge]).
 * المحرّك الأصلي لا يقبل المقاطعة (كما في [RinJobScheduler.stopAll]) فأمر طويل يُنتظر انتهاؤه.
 */
object RinNativeTerminal {

    private const val ST_OK = 0
    private const val ST_FAILED = 1
    private const val ST_NEED_MORE = 2
    private const val ST_QUIT = 3

    private val executor = Executors.newSingleThreadExecutor { r ->
        Thread(r, "rin-linux-terminal").apply { isDaemon = true }
    }
    private val main = Handler(Looper.getMainLooper())
    private val busy = AtomicBoolean(false)

    @Volatile private var handle = 0L
    @Volatile private var linkOk: Boolean? = null

    /** عرض شاشة الـ terminal بالأعمدة (تقيسه [RinTerminalView])؛ يُطبَّق قبل كل أمر ليعرف `:img` و`:ocr` عرض الهاتف. 0 = تلقائي. */
    @Volatile var columns: Int = 0

    /** وضع الجلسة الأصلية الكاملة: كل سطر شيفرة (لا الأوامر المدمجة في الـ terminal) يمرّ بها. */
    @Volatile var nativeMode: Boolean = false

    /** false إن لم تُحمَّل المكتبة أو لم تُربط دوال JNI (إصدار قديم/ABI غير مبنيّ). */
    fun isAvailable(): Boolean {
        linkOk?.let { return it }
        val ok = try {
            System.loadLibrary("rinengine")
            true
        } catch (t: Throwable) {
            false
        }
        linkOk = ok
        return ok
    }

    fun isBusy(): Boolean = busy.get()

    /** هل في الجلسة أمر ناقص ينتظر باقي أسطره؟ (آمن على الترد الرئيسي: قراءة فقط). */
    fun continuing(): Boolean {
        val h = handle
        if (h == 0L || !isAvailable()) return false
        return try { nativeContinuing(h) } catch (t: Throwable) { false }
    }

    fun cancelPending() {
        val h = handle
        if (h != 0L) try { nativeCancelPending(h) } catch (_: Throwable) { }
    }

    /** جلسة جديدة (تُنشأ عند أول أمر تالٍ). تستدعيه `reset` في الـ terminal مع [RinRepl]. */
    fun reset() {
        if (busy.get()) return
        val h = handle
        handle = 0L
        if (h != 0L) try { nativeDestroy(h) } catch (_: Throwable) { }
    }

    /** أسماء المتغيرات/الدوال المعرَّفة في الجلسة الأصلية. ينبغي استدعاؤها عندما لا تكون مشغولة. */
    fun variableNames(): List<String> {
        val h = handle
        if (h == 0L || busy.get()) return emptyList()
        return try { nativeVariables(h).split('\n').filter { it.isNotBlank() } } catch (t: Throwable) { emptyList() }
    }

    /**
     * يرسل [line] للجلسة على الخيط العامل. يعيد false إن كانت مشغولة (لا يُقبل سطران معاً).
     * [onDone] على الترد الرئيسي بعد ظهور كل المخرجات في الـ terminal.
     */
    fun submit(line: String, onDone: () -> Unit): Boolean {
        if (!busy.compareAndSet(false, true)) return false
        executor.execute {
            val started = SystemClock.elapsedRealtime()
            var status = -1
            var error: String? = null
            try {
                if (handle == 0L) {
                    val english = !Locale.getDefault().language.startsWith("ar")
                    handle = nativeCreate(RinEngine.currentBaseDir(), english)
                    if (handle == 0L) error = "could not create the Linux terminal session"
                }
                if (handle != 0L) {
                    RinTerminal.onJobStart(0, repl = true)
                    val listener = RinEngine.RinStreamListener { _, chunk -> RinTerminal.onChunk(chunk) }
                    val input = RinEngine.RinInputHandler { prompt ->
                        RinTerminal.onPrompt(prompt)
                        val answer = RinInputBridge.request(prompt)
                        RinTerminal.onAnswer(answer)
                        answer
                    }
                    nativeSetColumns(handle, columns)
                    status = nativeFeed(handle, line, listener, input)
                }
            } catch (t: Throwable) {
                error = "[Fatal error]: ${t.message ?: t.javaClass.simpleName}"
            }
            val ms = SystemClock.elapsedRealtime() - started
            if (error != null) {
                RinTerminal.system(error, TermRole.ERROR)
            } else {
                // نجاح/ناقص/خروج صامتة (كأي REPL)؛ الفشل يظهر بسطر الحالة. -1 = خطأ داخلي في الجلسة.
                val js = if (status == ST_FAILED || status < 0) JobStatus.ERROR else JobStatus.SUCCESS
                RinTerminal.onJobFinish(js, "", ms)
                if (status == ST_QUIT) {
                    // exit / quit: في الـ terminal الأصلي تنهي الجلسة؛ هنا نبدأ جلسة نظيفة بدل ترك المحرّر بلا طرفية.
                    RinTerminal.system("session closed — a fresh one starts with the next command")
                    val h = handle
                    handle = 0L
                    if (h != 0L) try { nativeDestroy(h) } catch (_: Throwable) { }
                }
            }
            busy.set(false)
            main.post { onDone() }
        }
        return true
    }

    private external fun nativeCreate(cwd: String, english: Boolean): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeFeed(
        handle: Long,
        line: String,
        listener: RinEngine.RinStreamListener?,
        inputHandler: RinEngine.RinInputHandler?
    ): Int
    private external fun nativeSetColumns(handle: Long, cols: Int)
    private external fun nativeCancelPending(handle: Long)
    private external fun nativeContinuing(handle: Long): Boolean
    private external fun nativeVariables(handle: Long): String
}
