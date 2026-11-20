package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.TextWatcher
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import java.util.concurrent.Callable
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import java.util.regex.Pattern

/**
 * RinFlow — Rin's official live pipeline visualizer.
 *
 * يفهم الآن كل مفاهيم الأنابيب في Rin، لا `@container.pipe` وحده:
 *  - `@container.pipe=name … .end/container.pipe` (كتلة أو أكثر)،
 *  - سلاسل `|>` المجرَّدة داخل حاوية أو خارجها،
 *  - `reckon name(collection) [where …] |> stage();`.
 *
 * الطريق الأساسي هو محرّك Flow الأصلي ([RinEngine.runSourceAsFlow]): تنفيذ واحد حقيقي يُنتج Flow
 * Graph بأزمنة وقيم إدخال/إخراج وأخطاء (بسطر/عمود) لكل عقدة، مع إلغاء حقيقي ومهلة زمنية وإعادة
 * تشغيل (Replay). [PipelineTracer] (إعادة تشغيل الكود مرة لكل مرحلة) يبقى فقط كاحتياطي لكتل
 * `@container.pipe` التي لا يُنتج منها المحرّك عقداً (مثلاً كتلة فيها خطأ قبل أول `|>`)، فيُعرَض
 * سبب الفشل الحقيقي بدل رسالة عامة.
 *
 * المصدر قابل للتحرير مباشرةً داخل الشاشة؛ لا يُستبدل كود المستخدم أبداً إلا بطلبه الصريح (مثال).
 */
class PipelineRunnerActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_CODE = "code"

        private const val PREFS = "rinflow_prefs"
        private const val KEY_TIMEOUT_S = "timeout_s"
        private const val DEFAULT_TIMEOUT_S = 15
        /** خيارات المهلة بالثواني (0 = بلا مهلة). */
        private val TIMEOUT_CHOICES = intArrayOf(5, 15, 30, 60, 0)
        /** المحرّك يفحص المهلة/الإلغاء بين العقد فقط؛ هذا الهامش لِـ watchdog الخارجي. */
        private const val WATCHDOG_GRACE_MS = 5_000L
        private const val MAX_NODES_SHOWN = 120
        private const val MAX_STDOUT_LINES = 40

        private val SAMPLE_PIPELINE = """
            @container.pipe=sales_pipeline
                let raw = [10, 20, 30, 40, 50]; // Input Data

                fun transform(data) {          // Step 1 (Transform)
                    return normalize(data);
                }

                fun aggregate(data) {          // Step 2 (Aggregation)
                    return mean(data);
                }

                let final_output = raw |> transform() |> aggregate();
                print final_output;
            .end/container.pipe
        """.trimIndent()

        private val SAMPLE_CHAIN = """
            // سلسلة |> مجرَّدة بلا حاوية (filter/map تُعرَّفان هنا لأنهما ليستا دالتين أصليتين)
            fun isAdult(x) { return x > 18; }
            fun myDouble(x) { return x * 2; }
            fun filter(arr, pred) { let out = []; let i = 0; while (i < len(arr)) { if (pred(arr[i])) { push(out, arr[i]); } i = i + 1; } return out; }
            fun map(arr, fn) { let out = []; let i = 0; while (i < len(arr)) { push(out, fn(arr[i])); i = i + 1; } return out; }
            let data = [10, 20, 30, 5, 40];
            let result = data |> filter(isAdult) |> map(myDouble) |> sum;
            print result;
        """.trimIndent()

        private val SAMPLE_RECKON = """
            // reckon: الصياغة المختصرة فوق |>
            let scores = [-5, 42, 88, 0, 73, 91, -12, 60];

            reckon average(scores)
                where item > 0 |> mean();
            print average;

            reckon smoothed(scores)
                |> movingAverage(3) |> clamp(0, 100);
            print smoothed;
        """.trimIndent()

        private val SAMPLES = listOf(SAMPLE_PIPELINE, SAMPLE_CHAIN, SAMPLE_RECKON)

        // أنماط التلوين (تُبنى مرة واحدة)
        private val P_KEYWORD = Pattern.compile("\\b(let|const|var|print|if|else|while|for|fun|return|true|false|nil|and|or|reckon|where|from|import|export|as)\\b")
        // مفهوم الإيجابية (docs/positive.md): المعامل `=[×]=` ومفاتيحه السهلة `=[x]=` `=[X]=` `=[*]=`، والدالة positive.
        private val P_POSITIVE = Pattern.compile("=\\[[×xX*]\\]=|\\bpositive\\b")
        private val P_CONTAINER = Pattern.compile("\\b(container|Containers|Group|Volume|Section|end|pipe)\\b")
        private val P_NUMBER = Pattern.compile("\\b\\d+(\\.\\d+)?\\b")
        private val P_TAG = Pattern.compile("@[A-Za-z][A-Za-z0-9_.]*|\\.end/[A-Za-z0-9_.]*")
        private val P_PIPE_OP = Pattern.compile("\\|>")
        private val P_STRING = Pattern.compile("\"[^\"\\n]*\"")
        private val P_COMMENT = Pattern.compile("//[^\\n]*")
    }

    /** نتيجة تشغيل واحدة، تُنتَج على خيط العمل وتُعرَض على الخيط الرئيسي. */
    private sealed class Outcome {
        class Real(val result: RinFlowRunResult) : Outcome()
        class Legacy(val trace: PipelineTracer.PipelineTrace) : Outcome()
        class NoPipe : Outcome()
        class NotExecuted : Outcome()
        class TimedOut : Outcome()
        class Failed(val message: String) : Outcome()
    }

    private lateinit var txtContainerName: TextView
    private lateinit var edtCode: EditText
    private lateinit var txtDetected: TextView
    private lateinit var flowContainer: LinearLayout
    private lateinit var statusBanner: LinearLayout
    private lateinit var txtStatusIcon: android.widget.ImageView
    private lateinit var progressStatusRunning: ProgressBar
    private lateinit var txtStatus: TextView
    private lateinit var txtMetrics: TextView
    private lateinit var txtDetails: TextView
    private lateinit var btnRun: Button
    private lateinit var btnStop: Button
    private lateinit var btnReplay: Button
    private lateinit var btnConfigure: Button
    private lateinit var btnLoadSample: Button

    private val queueExecutor = Executors.newSingleThreadExecutor()
    private val workerPool = Executors.newCachedThreadPool()
    private val mainHandler = Handler(Looper.getMainLooper())
    private var runningFuture: Future<*>? = null

    /** يزداد عند كل تشغيل/إيقاف؛ أي نتيجة متأخرة من تشغيل أقدم تُهمَل. الخيط الرئيسي فقط. */
    private var runGeneration = 0
    /** معرّف جلسة Flow الجارية (يصل بحدث FLOW_STARTED على خيط العمل) — لِـ Stop. */
    @Volatile private var currentSessionId: String = ""
    private var lastSessionId: String = ""
    private var lastReal: RinFlowRunResult? = null
    private var lastScan: RinPipeScanner.Scan = RinPipeScanner.EMPTY
    private var sourceCode: String = ""
    private var sampleIndex = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_pipeline_runner)
        RinEngine.init(applicationContext)

        txtContainerName = findViewById(R.id.txtPipelineContainerName)
        edtCode = findViewById(R.id.edtPipelineCode)
        txtDetected = findViewById(R.id.txtPipelineDetected)
        flowContainer = findViewById(R.id.flowContainer)
        statusBanner = findViewById(R.id.statusBanner)
        txtStatusIcon = findViewById(R.id.txtPipelineStatusIcon)
        progressStatusRunning = findViewById(R.id.progressPipelineRunning)
        txtStatus = findViewById(R.id.txtPipelineStatus)
        txtMetrics = findViewById(R.id.txtPipelineMetrics)
        txtDetails = findViewById(R.id.txtPipelineDetails)
        btnRun = findViewById(R.id.btnPipelineRun)
        btnStop = findViewById(R.id.btnPipelineStop)
        btnReplay = findViewById(R.id.btnPipelineReplay)
        btnConfigure = findViewById(R.id.btnPipelineConfigure)
        btnLoadSample = findViewById(R.id.btnPipelineLoadSample)
        val btnClose: ImageButton = findViewById(R.id.btnPipelineClose)

        edtCode.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(e: Editable) {
                applyHighlight(e)
                updateDetected(e.toString())
            }
        })

        sourceCode = intent.getStringExtra(EXTRA_CODE) ?: ""
        edtCode.setText(sourceCode)

        btnClose.setOnClickListener { finish() }
        btnRun.setOnClickListener { runPipeline() }
        btnStop.setOnClickListener { stopPipeline() }
        btnReplay.setOnClickListener { replayPipeline() }
        btnConfigure.setOnClickListener { showConfigureDialog() }
        btnLoadSample.setOnClickListener { onLoadSampleClicked() }

        setButton(btnStop, false)
        setButton(btnReplay, false)

        // فارغ (فُتحت الشاشة من «المزيد» مثلاً): حالة انتظار هادئة، لا خطأ.
        if (sourceCode.isBlank()) showIdle() else runPipeline()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopCursorBlink()
        runGeneration++
        val sid = currentSessionId
        if (sid.isNotEmpty()) workerPool.execute { RinEngine.cancelFlow(sid) }
        runningFuture?.cancel(true)
        queueExecutor.shutdownNow()
        workerPool.shutdownNow()
    }

    // ------------------------------------------------------------------ execution

    private fun timeoutSeconds(): Int =
        getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY_TIMEOUT_S, DEFAULT_TIMEOUT_S)

    private fun runPipeline() {
        hideKeyboard()
        sourceCode = edtCode.text.toString()
        if (sourceCode.isBlank()) { showIdle(); return }
        val scan = RinPipeScanner.scan(sourceCode)
        lastScan = scan
        if (!scan.hasPipes) { showNoPipe(); return }
        val src = sourceCode
        startJob { gen, timeoutMs -> executeRun(src, timeoutMs, gen) }
    }

    private fun replayPipeline() {
        val sid = lastSessionId
        if (sid.isEmpty()) return
        startJob { gen, timeoutMs ->
            val r = withWatchdog(timeoutMs) { RinEngine.replayFlow(sid, timeoutMs, flowListener(gen)) }
            val err = r.parseError
            if (err != null) Outcome.Failed(getString(R.string.pipeline_replay_failed, err)) else Outcome.Real(r)
        }
    }

    private fun startJob(work: (Int, Long) -> Outcome) {
        val gen = ++runGeneration
        currentSessionId = ""
        setButton(btnRun, false)
        setButton(btnStop, true)
        setButton(btnReplay, false)
        setStatus(
            R.drawable.ic_status_running,
            getString(R.string.pipeline_status_running),
            R.drawable.bg_pipeline_status_neutral,
            R.color.pipeline_text_primary,
            running = true
        )
        txtDetails.visibility = View.GONE
        txtMetrics.visibility = View.GONE
        stopCursorBlink()
        flowContainer.removeAllViews()

        val timeoutMs = timeoutSeconds() * 1000L
        runningFuture = queueExecutor.submit(Runnable {
            val outcome: Outcome = try {
                work(gen, timeoutMs)
            } catch (e: TimeoutException) {
                val sid = currentSessionId
                if (sid.isNotEmpty()) RinEngine.cancelFlow(sid)
                Outcome.TimedOut()
            } catch (e: ExecutionException) {
                Outcome.Failed("[Internal error]: ${e.cause?.message ?: e.message}")
            } catch (t: Throwable) {
                Outcome.Failed("[Internal error]: ${t.message}")
            }
            mainHandler.post {
                if (gen == runGeneration && !isFinishing) onOutcome(outcome)
            }
        })
    }

    /** يُنفِّذ [block] على عامل منفصل مع watchdog خارجي (المحرّك نفسه يفحص المهلة بين العقد فقط). */
    private fun <T> withWatchdog(timeoutMs: Long, block: () -> T): T {
        val future = workerPool.submit(Callable<T> { block() })
        return if (timeoutMs > 0) future.get(timeoutMs + WATCHDOG_GRACE_MS, TimeUnit.MILLISECONDS) else future.get()
    }

    private fun flowListener(gen: Int) = RinEngine.RinFlowListener { json ->
        val ev = RinFlowEvent.parse(json) ?: return@RinFlowListener
        if (ev.type == "FLOW_STARTED") currentSessionId = ev.flowId
        if (ev.type == "NODE_STARTED") {
            mainHandler.post {
                if (gen == runGeneration) txtStatus.text = getString(R.string.pipeline_status_running_node, ev.nodeId + 1)
            }
        }
    }

    private fun executeRun(src: String, timeoutMs: Long, gen: Int): Outcome {
        // 1) الطريق الحقيقي: Flow Graph واحد من محرّك RinFlow الأصلي — يغطي `|>` (داخل الحاويات
        //    وخارجها) و`reckon` معاً. نستدعي المحرّك مباشرةً (لا عبر RinFlowTracer.traceRealFlow)
        //    لأن فحص هذا الأخير بـ regex ضيق لا يرى مثل `foo(1) |> bar;` ولا reckon المنفرد.
        val real = withWatchdog(timeoutMs) { RinEngine.runSourceAsFlow(src, timeoutMs, flowListener(gen)) }
        val perr = real.parseError
        if (perr != null) {
            val where = if (real.parseErrorLine > 0) " (line ${real.parseErrorLine})" else ""
            return Outcome.Failed(perr + where)
        }
        if (real.nodes.isNotEmpty()) return Outcome.Real(real)

        // 2) احتياطي: كتلة @container.pipe لم تُنتج عقداً — PipelineTracer يشرح السبب الحقيقي (تشخيص بسطر).
        val legacy = withWatchdog(timeoutMs) { PipelineTracer.findPipeline(src) }
        if (legacy != null) return Outcome.Legacy(legacy)

        // 3) فشل التشغيل قبل أي أنبوب (خطأ في الكود نفسه)، أو وُجدت أنابيب لم تُنفَّذ (داخل دالة لم تُستدعَ).
        if (real.status != RinFlowSessionStatus.SUCCESS && real.output.isNotBlank()) {
            return Outcome.Failed(real.output)
        }
        return Outcome.NotExecuted()
    }

    private fun stopPipeline() {
        runGeneration++ // أي نتيجة متأخرة تُهمَل
        val sid = currentSessionId
        if (sid.isNotEmpty()) workerPool.execute { RinEngine.cancelFlow(sid) }
        setButton(btnRun, true)
        setButton(btnStop, false)
        updateReplayEnabled()
        setStatus(R.drawable.ic_status_stopped, getString(R.string.pipeline_status_stopped), R.drawable.bg_pipeline_status_neutral, R.color.pipeline_text_primary)
    }

    // ------------------------------------------------------------------ outcomes

    private fun onOutcome(o: Outcome) {
        setButton(btnRun, true)
        setButton(btnStop, false)
        when (o) {
            is Outcome.Real -> showReal(o.result)
            is Outcome.Legacy -> showLegacy(o.trace)
            is Outcome.NoPipe -> showNoPipe()
            is Outcome.NotExecuted -> showError(getString(R.string.pipeline_not_executed))
            is Outcome.TimedOut -> {
                flowContainer.removeAllViews()
                setStatus(R.drawable.ic_status_warning, getString(R.string.pipeline_status_timeout), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
            }
            is Outcome.Failed -> showError(o.message)
        }
        updateReplayEnabled()
    }

    private fun showIdle() {
        flowContainer.removeAllViews()
        stopCursorBlink()
        txtContainerName.text = ""
        txtMetrics.visibility = View.GONE
        setStatus(R.drawable.ic_status_stopped, getString(R.string.pipeline_status_idle), R.drawable.bg_pipeline_status_neutral, R.color.pipeline_text_primary)
        txtDetails.text = getString(R.string.pipeline_empty_hint)
        txtDetails.visibility = View.VISIBLE
        updateReplayEnabled()
    }

    private fun showNoPipe() {
        showError(getString(R.string.pipeline_no_block_found))
        updateReplayEnabled()
    }

    private fun showError(message: String) {
        flowContainer.removeAllViews()
        stopCursorBlink()
        txtMetrics.visibility = View.GONE
        txtContainerName.text = ""
        setStatus(R.drawable.ic_status_warning, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
        txtDetails.text = message
        txtDetails.visibility = View.VISIBLE
    }

    /** نتيجة محرّك Flow الحقيقي. */
    private fun showReal(res: RinFlowRunResult) {
        lastReal = res
        lastSessionId = res.sessionId
        txtContainerName.text = headerLabel(lastScan)
        buildRealFlowDiagram(res)

        val m = res.metrics
        txtMetrics.text = getString(
            R.string.pipeline_metrics_fmt,
            m.totalNodes, m.completedNodes, m.failedNodes, m.skippedNodes,
            m.totalDurationMs, m.totalInputRecords, m.totalOutputRecords
        )
        txtMetrics.visibility = View.VISIBLE

        when (res.status) {
            RinFlowSessionStatus.SUCCESS -> {
                setStatus(
                    R.drawable.ic_status_success,
                    getString(R.string.pipeline_status_success_timed, m.totalDurationMs),
                    R.drawable.bg_pipeline_status_success, R.color.pipeline_green_light_text
                )
                txtDetails.visibility = View.GONE
            }
            RinFlowSessionStatus.CANCELLED -> {
                setStatus(R.drawable.ic_status_stopped, getString(R.string.pipeline_status_stopped), R.drawable.bg_pipeline_status_neutral, R.color.pipeline_text_primary)
                txtDetails.visibility = View.GONE
            }
            RinFlowSessionStatus.TIMEOUT -> {
                setStatus(R.drawable.ic_status_warning, getString(R.string.pipeline_status_timeout), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
                txtDetails.visibility = View.GONE
            }
            RinFlowSessionStatus.ERROR, RinFlowSessionStatus.RUNNING -> {
                setStatus(R.drawable.ic_status_error, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
                val failing = res.nodes.firstOrNull { it.error != null }?.error
                txtDetails.text = if (failing != null) formatNodeError(failing) else res.output
                txtDetails.visibility = View.VISIBLE
            }
        }
    }

    /** النتيجة الاحتياطية القديمة (إعادة تشغيل لكل مرحلة) لكتلة @container.pipe بلا عقد من المحرّك. */
    private fun showLegacy(trace: PipelineTracer.PipelineTrace) {
        lastReal = null
        lastSessionId = ""
        txtMetrics.visibility = View.GONE
        txtContainerName.text = "@container.pipe = ${trace.containerName}"
        buildFlowDiagram(trace)
        if (trace.success) {
            setStatus(
                R.drawable.ic_status_success,
                getString(R.string.pipeline_status_success_timed, trace.totalDurationMs),
                R.drawable.bg_pipeline_status_success, R.color.pipeline_green_light_text
            )
            txtDetails.visibility = View.GONE
        } else {
            setStatus(R.drawable.ic_status_error, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
            txtDetails.text = trace.errorMessage ?: trace.rawEngineOutput
            txtDetails.visibility = View.VISIBLE
        }
    }

    private fun headerLabel(scan: RinPipeScanner.Scan): String = when {
        scan.blocks.isNotEmpty() -> {
            val extra = if (scan.blocks.size > 1) " (+${scan.blocks.size - 1})" else ""
            "@container.pipe = ${scan.blocks.first().name}$extra"
        }
        scan.reckonCount > 0 && scan.chainCount == 0 -> "reckon"
        else -> "|>"
    }

    private fun formatNodeError(e: RinFlowNodeError): String =
        "${e.code} · ${e.line}:${e.column} — ${e.message}"

    private fun updateReplayEnabled() {
        // Replay الأصلي يعيد تنفيذ آخر سلسلة `|>` في الجلسة (لا reckon).
        val can = lastReal != null && lastSessionId.isNotEmpty() && lastScan.canReplay && !btnStop.isEnabled
        setButton(btnReplay, can)
    }

    private fun setButton(btn: Button, enabled: Boolean) {
        btn.isEnabled = enabled
        btn.alpha = if (enabled) 1f else 0.45f
    }

    private fun setStatus(iconRes: Int, text: String, bg: Int, textColor: Int, running: Boolean = false) {
        txtStatusIcon.visibility = if (running) View.GONE else View.VISIBLE
        progressStatusRunning.visibility = if (running) View.VISIBLE else View.GONE
        txtStatusIcon.setImageResource(iconRes)
        val color = ContextCompat.getColor(this, textColor)
        txtStatusIcon.imageTintList = android.content.res.ColorStateList.valueOf(color)
        txtStatus.text = text
        statusBanner.setBackgroundResource(bg)
        txtStatus.setTextColor(color)
    }

    // ------------------------------------------------------------------ diagram (real Flow Graph)

    private fun buildRealFlowDiagram(res: RinFlowRunResult) {
        flowContainer.removeAllViews()
        stopCursorBlink()

        addTerminalPrompt("rinflow run   # ${res.sessionId}")

        var shown = 0
        for (node in res.nodes) {
            if (shown >= MAX_NODES_SHOWN) {
                addTerminalNote(getString(R.string.pipeline_nodes_hidden, res.nodes.size - shown), R.color.pipeline_terminal_dim)
                break
            }
            shown++
            if (node.type == "INPUT") addTerminalSection(chainTitle(node))
            addRealNode(node)
        }

        if (res.output.isNotBlank()) {
            addTerminalSection(getString(R.string.pipeline_stdout_label))
            val lines = res.output.trimEnd().split("\n")
            val text = lines.take(MAX_STDOUT_LINES).joinToString("\n") +
                if (lines.size > MAX_STDOUT_LINES) "\n…" else ""
            addTerminalNote(text, R.color.pipeline_terminal_text)
        }

        addTerminalCursor()
        startCursorBlink()
    }

    /** عنوان قسم لكل سلسلة: اسم `reckon` يأتي من المحرّك نفسه (اسم عقدة INPUT)، وللأنابيب العادية السطر الحقيقي. */
    private fun chainTitle(input: RinFlowGraphNode): String =
        if (input.name.startsWith("reckon ")) "${input.name} @ ${input.line}"
        else getString(R.string.pipeline_chain_header, input.line)

    private fun addRealNode(node: RinFlowGraphNode) {
        // تعبير when (لا عبارة) كي يُلزم المترجم بتغطية كل حالات FlowNodeStatus.
        val (prefix, color) = when (node.status) {
            FlowNodeStatus.SUCCESS -> "▸" to R.color.pipeline_terminal_green
            FlowNodeStatus.SKIPPED, FlowNodeStatus.QUEUED, FlowNodeStatus.RUNNING -> "·" to R.color.pipeline_terminal_dim
            FlowNodeStatus.CANCELLED -> "■" to R.color.pipeline_terminal_amber
            FlowNodeStatus.TIMEOUT -> "⏱" to R.color.pipeline_terminal_amber
            FlowNodeStatus.ERROR -> "✗" to R.color.pipeline_terminal_red
        }
        val detail = buildString {
            append(node.type.lowercase())
            if (node.status != FlowNodeStatus.SKIPPED) append(" · ").append(node.durationMs).append("ms")
            if (node.line > 0) append(" · L").append(node.line)
        }
        addTerminalStep(
            prefix = prefix,
            label = node.name,
            detail = detail,
            valueText = previewText(node.output),
            ok = node.status == FlowNodeStatus.SUCCESS,
            skipped = node.status == FlowNodeStatus.SKIPPED,
            statusColorRes = color,
            inputText = if (node.type == "INPUT") "" else previewText(node.input),
            errorText = node.error?.let { formatNodeError(it) } ?: ""
        )
    }

    private fun previewText(p: RinFlowDataPreview?): String {
        if (p == null || p.preview.isEmpty()) return ""
        val sb = StringBuilder(p.preview)
        if (p.truncated) sb.append(" …")
        if (p.recordCount >= 0) sb.append("  (").append(getString(R.string.pipeline_records_fmt, p.recordCount)).append(")")
        return sb.toString()
    }

    // ------------------------------------------------------------------ diagram (legacy per-stage trace)

    private fun buildFlowDiagram(trace: PipelineTracer.PipelineTrace) {
        flowContainer.removeAllViews()
        stopCursorBlink()
        if (trace.sourceExpr.isEmpty()) return

        addTerminalPrompt("run @container.pipe = ${trace.containerName}")

        addTerminalStep(
            prefix = "▸",
            label = getString(R.string.pipeline_node_source),
            detail = trace.sourceExpr,
            valueText = trace.sourceValueText,
            ok = true
        )

        // حالة لكل مرحلة: مرحلة لم تُنفَّذ لأن سابقتها فشلت (SKIPPED) تختلف عن التي فشلت فعلاً.
        for (stage in trace.stages) {
            val prefix = when (stage.status) {
                FlowNodeStatus.SUCCESS -> "▸"
                FlowNodeStatus.SKIPPED -> "·"
                else -> "✗"
            }
            addTerminalStep(
                prefix = prefix,
                label = stage.label,
                detail = stage.call,
                valueText = stage.valueText,
                ok = stage.status == FlowNodeStatus.SUCCESS,
                skipped = stage.status == FlowNodeStatus.SKIPPED
            )
        }

        addTerminalStep(
            prefix = if (trace.success) "✓" else "✗",
            label = getString(R.string.pipeline_node_output),
            detail = if (trace.success) "print • ${trace.totalDurationMs}ms" else "print",
            valueText = if (trace.success) trace.finalValueText else "—",
            ok = trace.success,
            isFinal = true
        )

        addTerminalCursor()
        startCursorBlink()
    }

    // ------------------------------------------------------------------ terminal-style line rendering

    /** سطر أمر بأسلوب طرفية حقيقية: موجّه `$` سماوي ثم نص الأمر بلون فاتح محايد. */
    private fun addTerminalPrompt(command: String) {
        val dp = resources.displayMetrics.density
        val line = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12.5f
            setPadding(0, 0, 0, (8 * dp).toInt())
        }
        val sb = SpannableStringBuilder()
        appendColored(sb, "$ ", R.color.pipeline_terminal_prompt, bold = true)
        appendColored(sb, command, R.color.pipeline_terminal_text, bold = false)
        line.text = sb
        flowContainer.addView(line)
    }

    /** عنوان قسم (سلسلة |> / reckon / stdout) بخط فاصل خافت. */
    private fun addTerminalSection(title: String) {
        val dp = resources.displayMetrics.density
        val line = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11.5f
            setPadding(0, (6 * dp).toInt(), 0, (2 * dp).toInt())
        }
        val sb = SpannableStringBuilder()
        appendColored(sb, "── ", R.color.pipeline_terminal_dim, bold = false)
        appendColored(sb, title, R.color.pipeline_terminal_prompt, bold = true)
        appendColored(sb, " ──", R.color.pipeline_terminal_dim, bold = false)
        line.text = sb
        flowContainer.addView(line)
    }

    /** نص حرّ أحادي المسافة بلون محدد (stdout، ملاحظات). */
    private fun addTerminalNote(text: String, colorRes: Int) {
        val dp = resources.displayMetrics.density
        val line = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12f
            setPadding((8 * dp).toInt(), 0, 0, (4 * dp).toInt())
            setTextColor(ContextCompat.getColor(this@PipelineRunnerActivity, colorRes))
            setTextIsSelectable(true)
        }
        line.text = text
        flowContainer.addView(line)
    }

    /**
     * خطوة أنبوب واحدة بأسلوب مخرجات طرفية: سطر رمز الحالة + اسم الخطوة + تفاصيلها، ثم (اختياري)
     * سطر المدخل الخافت `← …`، ثم سطر القيمة الفعلية `⇒ …`، ثم (عند الفشل) سطر الخطأ الحقيقي.
     */
    private fun addTerminalStep(
        prefix: String,
        label: String,
        detail: String,
        valueText: String,
        ok: Boolean,
        isFinal: Boolean = false,
        skipped: Boolean = false,
        statusColorRes: Int? = null,
        inputText: String = "",
        errorText: String = ""
    ) {
        val dp = resources.displayMetrics.density
        val statusColor = statusColorRes ?: when {
            skipped -> R.color.pipeline_terminal_dim
            ok -> R.color.pipeline_terminal_green
            else -> R.color.pipeline_terminal_red
        }

        val headerLine = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12.5f
            setPadding(0, (2 * dp).toInt(), 0, 0)
        }
        val hsb = SpannableStringBuilder()
        appendColored(hsb, "  $prefix ", statusColor, bold = true)
        appendColored(hsb, label, R.color.pipeline_terminal_cyan, bold = true)
        if (detail.isNotBlank()) {
            appendColored(hsb, "  $detail", R.color.pipeline_terminal_dim, bold = false)
        }
        headerLine.text = hsb
        flowContainer.addView(headerLine)

        if (inputText.isNotBlank()) {
            val inLine = TextView(this).apply {
                typeface = Typeface.MONOSPACE
                textSize = 11.5f
                setPadding((30 * dp).toInt(), (1 * dp).toInt(), 0, 0)
                ellipsize = android.text.TextUtils.TruncateAt.END
                maxLines = 2
            }
            val isb = SpannableStringBuilder()
            appendColored(isb, "← ", R.color.pipeline_terminal_dim, bold = false)
            appendColored(isb, inputText, R.color.pipeline_terminal_dim, bold = false)
            inLine.text = isb
            flowContainer.addView(inLine)
        }

        if (valueText.isNotBlank()) {
            val valueLine = TextView(this).apply {
                typeface = Typeface.MONOSPACE
                textSize = 12.5f
                setPadding((30 * dp).toInt(), (1 * dp).toInt(), 0, ((if (errorText.isBlank()) 8 else 2) * dp).toInt())
                ellipsize = android.text.TextUtils.TruncateAt.END
                maxLines = 3
            }
            val vsb = SpannableStringBuilder()
            appendColored(vsb, "⇒ ", statusColor, bold = true)
            appendColored(
                vsb,
                valueText,
                if (isFinal) statusColor else R.color.pipeline_terminal_amber,
                bold = isFinal
            )
            valueLine.text = vsb
            flowContainer.addView(valueLine)
        }

        if (errorText.isNotBlank()) {
            val errLine = TextView(this).apply {
                typeface = Typeface.MONOSPACE
                textSize = 11.5f
                setPadding((30 * dp).toInt(), (1 * dp).toInt(), 0, (8 * dp).toInt())
            }
            val esb = SpannableStringBuilder()
            appendColored(esb, errorText, R.color.pipeline_terminal_red, bold = false)
            errLine.text = esb
            flowContainer.addView(errLine)
        }
    }

    /** مؤشر إدخال وامض في نهاية التتبّع، يعطي الشعور بطرفية حيّة بانتظار الأمر التالي. */
    private fun addTerminalCursor() {
        val dp = resources.displayMetrics.density
        val line = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12.5f
            setPadding(0, (4 * dp).toInt(), 0, 0)
        }
        val sb = SpannableStringBuilder()
        appendColored(sb, "$ ", R.color.pipeline_terminal_prompt, bold = true)
        appendColored(sb, "█", R.color.pipeline_terminal_cursor, bold = false)
        line.text = sb
        flowContainer.addView(line)
        cursorLine = line
    }

    private fun appendColored(sb: SpannableStringBuilder, text: String, colorRes: Int, bold: Boolean) {
        val start = sb.length
        sb.append(text)
        sb.setSpan(ForegroundColorSpan(ContextCompat.getColor(this, colorRes)), start, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (bold) sb.setSpan(StyleSpan(Typeface.BOLD), start, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private var cursorLine: TextView? = null
    private val cursorBlinkRunnable = object : Runnable {
        override fun run() {
            cursorLine?.let { it.visibility = if (it.visibility == View.VISIBLE) View.INVISIBLE else View.VISIBLE }
            mainHandler.postDelayed(this, 500)
        }
    }

    private fun startCursorBlink() {
        mainHandler.postDelayed(cursorBlinkRunnable, 500)
    }

    private fun stopCursorBlink() {
        mainHandler.removeCallbacks(cursorBlinkRunnable)
        cursorLine = null
    }

    // ------------------------------------------------------------------ source editor / misc

    private fun hideKeyboard() {
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
        imm?.hideSoftInputFromWindow(edtCode.windowToken, 0)
        edtCode.clearFocus()
    }

    /** ملخص ما رصده الماسح الساكن في الكود (قبل أي تشغيل). */
    private fun updateDetected(text: String) {
        val scan = RinPipeScanner.scan(text)
        if (!scan.hasPipes) { txtDetected.visibility = View.GONE; return }
        val sb = StringBuilder(
            getString(R.string.pipeline_detected_fmt, scan.blocks.size, scan.chainCount, scan.reckonCount, scan.stageCount)
        )
        if (scan.blocks.isNotEmpty()) {
            sb.append("\ncontainer.pipe: ").append(scan.blocks.joinToString(", ") { it.name })
        }
        txtDetected.text = sb.toString()
        txtDetected.visibility = View.VISIBLE
    }

    /** لا يُستبدل كود المستخدم بمثال إلا بطلبه الصريح، وبتأكيد إن كان في الحقل كود مختلف عن الأمثلة. */
    private fun onLoadSampleClicked() {
        val current = edtCode.text.toString()
        if (current.isBlank() || SAMPLES.contains(current)) {
            loadSample()
            return
        }
        AlertDialog.Builder(this)
            .setMessage(R.string.pipeline_replace_confirm)
            .setPositiveButton(android.R.string.ok) { _, _ -> loadSample() }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun loadSample() {
        val sample = SAMPLES[sampleIndex % SAMPLES.size]
        sampleIndex++
        edtCode.setText(sample)
        runPipeline()
    }

    private fun showConfigureDialog() {
        val labels = TIMEOUT_CHOICES.map {
            if (it == 0) getString(R.string.pipeline_timeout_none) else getString(R.string.pipeline_timeout_seconds, it)
        }.toTypedArray()
        val current = TIMEOUT_CHOICES.indexOf(timeoutSeconds()).coerceAtLeast(0)
        AlertDialog.Builder(this)
            .setTitle(R.string.pipeline_timeout_title)
            .setSingleChoiceItems(labels, current) { dialog, which ->
                getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt(KEY_TIMEOUT_S, TIMEOUT_CHOICES[which]).apply()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** تلوين المصدر أثناء الكتابة: يُزيل ألواننا السابقة فقط ثم يعيد تطبيقها (التعليق آخراً ليغلب). */
    private fun applyHighlight(e: Editable) {
        for (sp in e.getSpans(0, e.length, ForegroundColorSpan::class.java)) e.removeSpan(sp)
        for (sp in e.getSpans(0, e.length, StyleSpan::class.java)) e.removeSpan(sp)
        val text = e.toString()

        fun paint(pattern: Pattern, colorRes: Int, bold: Boolean = false, italic: Boolean = false) {
            val color = ContextCompat.getColor(this, colorRes)
            val m = pattern.matcher(text)
            while (m.find()) {
                e.setSpan(ForegroundColorSpan(color), m.start(), m.end(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                if (bold) e.setSpan(StyleSpan(Typeface.BOLD), m.start(), m.end(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                if (italic) e.setSpan(StyleSpan(Typeface.ITALIC), m.start(), m.end(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }

        paint(P_KEYWORD, R.color.pipeline_syntax_keyword)
        paint(P_CONTAINER, R.color.pipeline_syntax_container)
        paint(P_NUMBER, R.color.pipeline_syntax_number)
        paint(P_TAG, R.color.pipeline_syntax_tag)
        paint(P_PIPE_OP, R.color.pipeline_green, bold = true)
        paint(P_POSITIVE, R.color.pipeline_green, bold = true)
        paint(P_STRING, R.color.pipeline_syntax_string)
        paint(P_COMMENT, R.color.pipeline_syntax_comment, italic = true)
    }
}
