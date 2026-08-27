package com.dlof.rinlang

import android.app.AlertDialog
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.view.View
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import java.util.regex.Pattern

/**
 * RinFlow — Rin's official live pipeline visualizer for a single `@container.pipe` block.
 *
 * Every value shown on screen comes from [PipelineTracer], which really runs
 * the source through the native Rin engine — this screen is only responsible
 * for laying the result out as a flow diagram.
 */
class PipelineRunnerActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_CODE = "code"
        private const val TIMEOUT_MS = 15_000L
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
    }

    private lateinit var txtContainerName: TextView
    private lateinit var txtCode: TextView
    private lateinit var flowContainer: LinearLayout
    private lateinit var statusBanner: LinearLayout
    private lateinit var txtStatusIcon: android.widget.ImageView
    private lateinit var progressStatusRunning: ProgressBar
    private lateinit var txtStatus: TextView
    private lateinit var txtDetails: TextView
    private lateinit var btnRun: android.widget.Button
    private lateinit var btnStop: android.widget.Button
    private lateinit var btnConfigure: android.widget.Button
    private lateinit var btnLoadSample: android.widget.Button

    private val queueExecutor = Executors.newSingleThreadExecutor()
    private val workerPool = Executors.newCachedThreadPool()
    private val mainHandler = Handler(Looper.getMainLooper())
    private var runningFuture: Future<*>? = null
    private var lastTrace: PipelineTracer.PipelineTrace? = null
    private var sourceCode: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_pipeline_runner)
        RinEngine.init(applicationContext)

        txtContainerName = findViewById(R.id.txtPipelineContainerName)
        txtCode = findViewById(R.id.txtPipelineCode)
        flowContainer = findViewById(R.id.flowContainer)
        statusBanner = findViewById(R.id.statusBanner)
        txtStatusIcon = findViewById(R.id.txtPipelineStatusIcon)
        progressStatusRunning = findViewById(R.id.progressPipelineRunning)
        txtStatus = findViewById(R.id.txtPipelineStatus)
        txtDetails = findViewById(R.id.txtPipelineDetails)
        btnRun = findViewById(R.id.btnPipelineRun)
        btnStop = findViewById(R.id.btnPipelineStop)
        btnConfigure = findViewById(R.id.btnPipelineConfigure)
        btnLoadSample = findViewById(R.id.btnPipelineLoadSample)
        val btnClose: ImageButton = findViewById(R.id.btnPipelineClose)

        sourceCode = intent.getStringExtra(EXTRA_CODE) ?: ""
        txtCode.text = highlightSource(sourceCode)

        btnClose.setOnClickListener { finish() }
        btnRun.setOnClickListener { runPipeline() }
        btnStop.setOnClickListener { stopPipeline() }
        btnConfigure.setOnClickListener { showConfigureDialog() }
        btnLoadSample.setOnClickListener { loadSample() }

        runPipeline()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopCursorBlink()
        runningFuture?.cancel(true)
        queueExecutor.shutdownNow()
        workerPool.shutdownNow()
    }

    // ---- execution ----

    private fun runPipeline() {
        btnRun.isEnabled = false
        btnStop.isEnabled = true
        setStatus(
            R.drawable.ic_status_running,
            getString(R.string.pipeline_status_running),
            R.drawable.bg_pipeline_status_neutral,
            R.color.pipeline_text_primary,
            running = true
        )
        txtDetails.visibility = View.GONE
        btnLoadSample.visibility = View.GONE
        stopCursorBlink()
        flowContainer.removeAllViews()

        runningFuture = queueExecutor.submit {
            val future = workerPool.submit(Callable { PipelineTracer.findPipeline(sourceCode) })
            val trace = try {
                future.get(TIMEOUT_MS, TimeUnit.MILLISECONDS)
            } catch (e: TimeoutException) {
                future.cancel(true)
                PipelineTracer.PipelineTrace(
                    containerName = "",
                    sourceExpr = "",
                    success = false,
                    errorMessage = "[Timeout]: execution exceeded ${TIMEOUT_MS / 1000} seconds"
                )
            } catch (t: Throwable) {
                null
            }
            mainHandler.post { onTraceReady(trace) }
        }
    }

    private fun stopPipeline() {
        runningFuture?.cancel(true)
        btnRun.isEnabled = true
        btnStop.isEnabled = false
        setStatus(R.drawable.ic_status_stopped, getString(R.string.pipeline_status_stopped), R.drawable.bg_pipeline_status_neutral, R.color.pipeline_text_primary)
    }

    private fun onTraceReady(trace: PipelineTracer.PipelineTrace?) {
        btnRun.isEnabled = true
        btnStop.isEnabled = false
        lastTrace = trace

        if (trace == null) {
            // لم يتم التعرّف على أي `@container.pipe` داخل كود المستخدم الفعلي.
            // لا نستبدل كوده بصمت بمثال جاهز؛ نُخبره بوضوح ونترك له خيار تحميل مثال.
            flowContainer.removeAllViews()
            txtContainerName.text = ""
            setStatus(R.drawable.ic_status_warning, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
            txtDetails.text = getString(R.string.pipeline_no_block_found)
            txtDetails.visibility = View.VISIBLE
            btnLoadSample.visibility = View.VISIBLE
            return
        }

        btnLoadSample.visibility = if (trace.success) View.GONE else View.VISIBLE
        txtContainerName.text = "@container.pipe = ${trace.containerName}"
        buildFlowDiagram(trace)

        if (trace.success) {
            setStatus(
                R.drawable.ic_status_success,
                getString(R.string.pipeline_status_success_timed, trace.totalDurationMs),
                R.drawable.bg_pipeline_status_success,
                R.color.pipeline_green_light_text
            )
            txtDetails.visibility = View.GONE
        } else {
            setStatus(R.drawable.ic_status_error, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
            txtDetails.text = trace.errorMessage ?: trace.rawEngineOutput
            txtDetails.visibility = View.VISIBLE
        }
    }

    /** يُستدعى فقط عند طلب المستخدم صراحةً مثالاً توضيحياً — لا يُستدعى تلقائياً أبداً. */
    private fun loadSample() {
        sourceCode = SAMPLE_PIPELINE
        txtCode.text = highlightSource(sourceCode)
        btnLoadSample.visibility = View.GONE
        runPipeline()
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

    // ---- diagram building ----

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

        // Per-stage status (section 2/8): a stage that never ran because an earlier one failed
        // is shown distinctly from the stage that actually failed, instead of every stage after
        // the first error looking identically "✗" the way a single trace.success flag would.
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

    // ---- terminal-style line rendering (new RinFlow pipe presentation) ----

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

    /**
     * خطوة أنبوب واحدة بأسلوب مخرجات طرفية: سطر رمز الحالة + اسم الخطوة + تفاصيلها،
     * يتبعه سطر مُزاح للداخل بالقيمة الفعلية `⇒ ...` — بدل بطاقة CI/CD المعزولة سابقاً.
     */
    private fun addTerminalStep(
        prefix: String,
        label: String,
        detail: String,
        valueText: String,
        ok: Boolean,
        isFinal: Boolean = false,
        skipped: Boolean = false
    ) {
        val dp = resources.displayMetrics.density
        val statusColor = when {
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

        if (valueText.isNotBlank()) {
            val valueLine = TextView(this).apply {
                typeface = Typeface.MONOSPACE
                textSize = 12.5f
                setPadding((30 * dp).toInt(), (1 * dp).toInt(), 0, (8 * dp).toInt())
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

    // ---- misc ----

    private fun showConfigureDialog() {
        val trace = lastTrace
        val containerName = trace?.containerName ?: "-"
        val stageCount = trace?.stages?.size ?: 0
        AlertDialog.Builder(this)
            .setTitle(R.string.pipeline_configure_title)
            .setMessage(getString(R.string.pipeline_configure_message, containerName, stageCount, TIMEOUT_MS / 1000))
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    private fun highlightSource(source: String): CharSequence {
        val sb = SpannableStringBuilder(source)
        fun colorOf(res: Int) = ContextCompat.getColor(this, res)

        fun apply(pattern: Pattern, color: Int, italic: Boolean = false) {
            val m = pattern.matcher(source)
            while (m.find()) {
                sb.setSpan(ForegroundColorSpan(color), m.start(), m.end(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                if (italic) sb.setSpan(StyleSpan(Typeface.ITALIC), m.start(), m.end(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }

        apply(Pattern.compile("\\b(let|print|if|else|while|fun|return|true|false|nil|and|or)\\b"), colorOf(R.color.pipeline_syntax_keyword))
        apply(Pattern.compile("\\b(container|Containers|Group|Volume|Section|end|pipe)\\b"), colorOf(R.color.pipeline_syntax_container))
        apply(Pattern.compile("\\b\\d+(\\.\\d+)?\\b"), colorOf(R.color.pipeline_syntax_number))
        apply(Pattern.compile("@[A-Za-z][A-Za-z0-9_.]*|\\.end/[A-Za-z0-9_.]*"), colorOf(R.color.pipeline_syntax_tag))
        apply(Pattern.compile("\"[^\"]*\""), colorOf(R.color.pipeline_syntax_string))
        apply(Pattern.compile("//[^\\n]*"), colorOf(R.color.pipeline_syntax_comment), italic = true)

        return sb
    }
}
