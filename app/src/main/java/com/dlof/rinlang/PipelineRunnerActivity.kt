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
import android.view.Gravity
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
        if (trace.sourceExpr.isEmpty()) return

        addNode(
            icon = R.drawable.ic_node_source,
            title = getString(R.string.pipeline_node_source),
            subtitle = trace.sourceExpr,
            valueText = trace.sourceValueText,
            ok = true
        )

        for (stage in trace.stages) {
            addArrow()
            addNode(
                icon = iconForStage(stage.label),
                title = stage.label,
                subtitle = stage.call,
                valueText = stage.valueText,
                ok = trace.success
            )
        }

        addArrow()
        addNode(
            icon = if (trace.success) R.drawable.ic_status_success else R.drawable.ic_status_error,
            title = getString(R.string.pipeline_node_output),
            subtitle = if (trace.success) "print • ${trace.totalDurationMs}ms" else "print",
            valueText = if (trace.success) trace.finalValueText else "—",
            ok = trace.success
        )
    }

    private fun iconForStage(name: String): Int {
        val n = name.lowercase()
        return when {
            "aggregat" in n || "mean" in n || "sum" in n || "reduce" in n || "total" in n || "count" in n
                || "median" in n || "variance" in n || "stddev" in n || "mode" in n -> R.drawable.ic_node_aggregate
            "sort" in n -> R.drawable.ic_node_sort
            "filter" in n -> R.drawable.ic_node_filter
            else -> R.drawable.ic_node_transform
        }
    }

    /**
     * بطاقة عقدة RinFlow بأسلوب لوحات CI/CD الاحترافية: شريحة لون حالة على الحافة اليسرى،
     * عنوان مع أيقونة، سطر الكود الفرعي، شريحة القيمة الفعلية، ثم سطر حالة سفلي
     * (✓ Passed / ✕ Failed) — بدل الدائرة البسيطة المعزولة في التصميم القديم.
     */
    private fun addNode(icon: Int, title: String, subtitle: String, valueText: String, ok: Boolean) {
        val dp = resources.displayMetrics.density
        val accentColor = ContextCompat.getColor(this, if (ok) R.color.pipeline_green else R.color.pipeline_red)

        val card = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            background = ContextCompat.getDrawable(context, R.drawable.bg_pipeline_node_card)
            elevation = 2 * dp
            layoutParams = LinearLayout.LayoutParams((188 * dp).toInt(), LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                gravity = Gravity.CENTER_VERTICAL
            }
        }

        val stripe = View(this).apply {
            setBackgroundColor(accentColor)
            layoutParams = LinearLayout.LayoutParams((4 * dp).toInt(), LinearLayout.LayoutParams.MATCH_PARENT)
        }
        card.addView(stripe)

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((10 * dp).toInt(), (10 * dp).toInt(), (10 * dp).toInt(), (10 * dp).toInt())
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }

        val titleRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val titleIcon = android.widget.ImageView(this).apply {
            setImageResource(icon)
            imageTintList = android.content.res.ColorStateList.valueOf(accentColor)
            layoutParams = LinearLayout.LayoutParams((16 * dp).toInt(), (16 * dp).toInt()).apply {
                marginEnd = (6 * dp).toInt()
            }
        }
        titleRow.addView(titleIcon)
        val titleView = TextView(this).apply {
            text = title
            textSize = 12.5f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.pipeline_text_primary))
            maxLines = 1
            ellipsize = android.text.TextUtils.TruncateAt.END
        }
        titleRow.addView(titleView)
        content.addView(titleRow)

        if (subtitle.isNotBlank()) {
            val subtitleView = TextView(this).apply {
                text = subtitle
                textSize = 10f
                typeface = Typeface.MONOSPACE
                setTextColor(ContextCompat.getColor(context, R.color.pipeline_text_muted))
                maxLines = 2
                ellipsize = android.text.TextUtils.TruncateAt.END
                setPadding(0, (5 * dp).toInt(), 0, 0)
            }
            content.addView(subtitleView)
        }

        val valueChip = TextView(this).apply {
            text = valueText.ifBlank { "…" }
            textSize = 11f
            typeface = Typeface.MONOSPACE
            setBackgroundResource(if (ok) R.drawable.bg_pipeline_value_chip else R.drawable.bg_pipeline_value_chip_error)
            setTextColor(ContextCompat.getColor(context, if (ok) R.color.pipeline_green_light_text else R.color.pipeline_red_light_text))
            setPadding((7 * dp).toInt(), (4 * dp).toInt(), (7 * dp).toInt(), (4 * dp).toInt())
            maxLines = 2
            ellipsize = android.text.TextUtils.TruncateAt.END
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = (7 * dp).toInt() }
        }
        content.addView(valueChip)

        val statusRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = (8 * dp).toInt() }
        }
        val statusIcon = android.widget.ImageView(this).apply {
            setImageResource(if (ok) R.drawable.ic_status_success else R.drawable.ic_status_error)
            imageTintList = android.content.res.ColorStateList.valueOf(accentColor)
            layoutParams = LinearLayout.LayoutParams((11 * dp).toInt(), (11 * dp).toInt()).apply {
                marginEnd = (5 * dp).toInt()
            }
        }
        statusRow.addView(statusIcon)
        val statusText = TextView(this).apply {
            text = getString(if (ok) R.string.pipeline_node_status_ok else R.string.pipeline_node_status_error)
            textSize = 10.5f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(accentColor)
        }
        statusRow.addView(statusText)
        content.addView(statusRow)

        card.addView(content)
        flowContainer.addView(card)
    }

    /** سهم أفقي يصل بين بطاقتين متتاليتين في التدفق (بدل السهم الرأسي في التصميم القديم). */
    private fun addArrow() {
        val dp = resources.displayMetrics.density
        val arrow = android.widget.ImageView(this).apply {
            setImageResource(R.drawable.ic_pipeline_arrow_down)
            rotation = -90f
            layoutParams = LinearLayout.LayoutParams(
                (20 * dp).toInt(),
                (14 * dp).toInt()
            ).apply {
                gravity = Gravity.CENTER_VERTICAL
                leftMargin = (6 * dp).toInt()
                rightMargin = (6 * dp).toInt()
            }
        }
        flowContainer.addView(arrow)
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
