package com.dlof.rinlang

import android.Manifest
import android.app.AlertDialog
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
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
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import java.util.regex.Pattern

/**
 * RinFlow — Rin's official live execution visualizer, integrated with every concept the
 * language has (pipes, tables, data/api containers, groups, volumes, sections, translations,
 * imports/links, style/row statements, and save/installation — whose real output files can be
 * downloaded straight from this screen).
 *
 * Every value shown on screen comes from [RinFlowTracer], which really runs the source through
 * the native Rin engine — this screen is only responsible for laying the result out as a flow
 * diagram.
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
    private var lastResult: RinFlowResult? = null
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
            val future = workerPool.submit(Callable { RinFlowTracer.trace(sourceCode) })
            val result = try {
                future.get(TIMEOUT_MS, TimeUnit.MILLISECONDS)
            } catch (e: TimeoutException) {
                future.cancel(true)
                RinFlowResult(
                    kindLabel = "",
                    containerName = "",
                    nodes = emptyList(),
                    success = false,
                    errorMessage = "[Timeout]: execution exceeded ${TIMEOUT_MS / 1000} seconds"
                )
            } catch (t: Throwable) {
                null
            }
            mainHandler.post { onTraceReady(result) }
        }
    }

    private fun stopPipeline() {
        runningFuture?.cancel(true)
        btnRun.isEnabled = true
        btnStop.isEnabled = false
        setStatus(R.drawable.ic_status_stopped, getString(R.string.pipeline_status_stopped), R.drawable.bg_pipeline_status_neutral, R.color.pipeline_text_primary)
    }

    private fun onTraceReady(result: RinFlowResult?) {
        btnRun.isEnabled = true
        btnStop.isEnabled = false
        lastResult = result

        if (result == null) {
            // كود فارغ أو لم يُعِد المحرّك أي نتيجة قابلة للعرض.
            flowContainer.removeAllViews()
            txtContainerName.text = ""
            setStatus(R.drawable.ic_status_warning, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
            txtDetails.text = getString(R.string.pipeline_no_block_found)
            txtDetails.visibility = View.VISIBLE
            btnLoadSample.visibility = View.VISIBLE
            return
        }

        btnLoadSample.visibility = if (result.success) View.GONE else View.VISIBLE
        txtContainerName.text = if (result.containerName.isNotBlank())
            "@${result.kindLabel} = ${result.containerName}"
        else if (result.kindLabel.isNotBlank()) result.kindLabel else ""
        buildFlowDiagram(result)

        if (result.success) {
            setStatus(R.drawable.ic_status_success, getString(R.string.pipeline_status_success), R.drawable.bg_pipeline_status_success, R.color.pipeline_green_light_text)
            txtDetails.visibility = View.GONE
        } else {
            setStatus(R.drawable.ic_status_error, getString(R.string.pipeline_status_error), R.drawable.bg_pipeline_status_error, R.color.pipeline_red_light_text)
            txtDetails.text = result.errorMessage ?: result.rawEngineOutput
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

    private fun buildFlowDiagram(result: RinFlowResult) {
        flowContainer.removeAllViews()
        result.nodes.forEachIndexed { index, node ->
            if (index > 0) addArrow()
            addNode(node)
        }
    }

    private fun addNode(node: FlowNode) {
        val dp = resources.displayMetrics.density
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { gravity = Gravity.CENTER_HORIZONTAL }
        }

        val circle = ImageView(this).apply {
            if (node.icon != null) setImageResource(node.icon)
            scaleType = ImageView.ScaleType.CENTER
            setBackgroundResource(if (node.ok) R.drawable.bg_pipeline_node_circle else R.drawable.bg_pipeline_node_circle_error)
            imageTintList = android.content.res.ColorStateList.valueOf(
                ContextCompat.getColor(context, node.colorRes)
            )
            layoutParams = LinearLayout.LayoutParams((44 * dp).toInt(), (44 * dp).toInt())
        }
        column.addView(circle)

        val titleView = TextView(this).apply {
            text = node.title
            textSize = 12.5f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.pipeline_text_primary))
            gravity = Gravity.CENTER
            setPadding(0, (4 * dp).toInt(), 0, 0)
        }
        column.addView(titleView)

        if (node.subtitle.isNotBlank()) {
            val subtitleView = TextView(this).apply {
                text = node.subtitle
                textSize = 10f
                setTextColor(ContextCompat.getColor(context, R.color.pipeline_text_muted))
                gravity = Gravity.CENTER
                setPadding(0, (1 * dp).toInt(), 0, 0)
            }
            column.addView(subtitleView)
        }

        val valueChip = TextView(this).apply {
            text = node.valueText.ifBlank { "…" }
            textSize = 11f
            typeface = Typeface.MONOSPACE
            gravity = Gravity.CENTER
            setBackgroundResource(R.drawable.bg_pipeline_value_chip)
            setTextColor(ContextCompat.getColor(context, if (node.ok) R.color.pipeline_green_light_text else R.color.pipeline_red_light_text))
            setPadding((8 * dp).toInt(), (5 * dp).toInt(), (8 * dp).toInt(), (5 * dp).toInt())
            val maxW = (resources.displayMetrics.widthPixels * 0.7).toInt()
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                topMargin = (4 * dp).toInt()
            }
            maxWidth = maxW
        }
        column.addView(valueChip)

        // عقدة أنتجت فعلياً ملفاً على القرص (save/installation) — قابلة للتنزيل الحقيقي من هنا مباشرةً.
        val artifact = node.artifact
        if (artifact != null) {
            column.addView(buildDownloadBadge(artifact))
        }

        flowContainer.addView(column)
    }

    /** شارة صغيرة قابلة للنقر أسفل عقدة الحفظ/التثبيت، تُنزّل الملف الفعلي إلى Downloads بشريط تقدّم حقيقي. */
    private fun buildDownloadBadge(artifact: RinArtifact): View {
        val dp = resources.displayMetrics.density
        val badge = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundResource(R.drawable.bg_download_chip)
            setPadding((8 * dp).toInt(), (5 * dp).toInt(), (8 * dp).toInt(), (5 * dp).toInt())
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = (6 * dp).toInt() }
            isClickable = true
            isFocusable = true
        }
        val icon = ImageView(this).apply {
            setImageResource(R.drawable.ic_log_download)
            imageTintList = android.content.res.ColorStateList.valueOf(
                ContextCompat.getColor(context, R.color.pipeline_green)
            )
            layoutParams = LinearLayout.LayoutParams((14 * dp).toInt(), (14 * dp).toInt()).apply {
                marginEnd = (6 * dp).toInt()
            }
        }
        val label = TextView(this).apply {
            text = "${artifact.displayName} · ${RinConsoleFormatter.formatBytes(artifact.sizeBytes)}"
            textSize = 10.5f
            typeface = Typeface.MONOSPACE
            setTextColor(ContextCompat.getColor(context, R.color.pipeline_text_primary))
        }
        badge.addView(icon)
        badge.addView(label)
        badge.setOnClickListener { downloadArtifact(artifact) }
        return badge
    }

    /** نفس تجربة التنزيل الحقيقي المستخدمة في قائمة التشغيل (RinDownloadManager)، لكن من داخل RinFlow مباشرة. */
    private fun downloadArtifact(artifact: RinArtifact) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            val granted = ContextCompat.checkSelfPermission(
                this, Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
            if (!granted) {
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE), 4202)
                Toast.makeText(this, getString(R.string.download_permission_needed_toast), Toast.LENGTH_LONG).show()
                return
            }
        }

        val progressDialog = RinDownloadProgressDialog(this, artifact.displayName)
        progressDialog.show()
        RinDownloadManager.downloadToPublicDownloads(
            activity = this,
            artifact = artifact,
            onProgress = { copied, total -> progressDialog.updateProgress(copied, total) },
            onDone = { uri, error ->
                if (error != null || uri == null) {
                    progressDialog.finishError(getString(R.string.download_error_toast, error?.message ?: "?"))
                    Handler(Looper.getMainLooper()).postDelayed({ progressDialog.dismiss() }, 1400)
                    return@downloadToPublicDownloads
                }
                progressDialog.finishSuccess(getString(R.string.download_status_done))
                Toast.makeText(this, getString(R.string.download_done_toast, artifact.displayName), Toast.LENGTH_SHORT).show()
                Handler(Looper.getMainLooper()).postDelayed({
                    progressDialog.dismiss()
                    RinDownloadManager.openDownloadedFile(this, uri, artifact.kind.mime)
                }, 500)
            }
        )
    }

    private fun addArrow() {
        val arrow = android.widget.ImageView(this).apply {
            setImageResource(R.drawable.ic_pipeline_arrow_down)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { gravity = Gravity.CENTER_HORIZONTAL }
        }
        flowContainer.addView(arrow)
    }

    // ---- misc ----

    private fun showConfigureDialog() {
        val result = lastResult
        val containerName = result?.containerName?.ifBlank { "-" } ?: "-"
        val stepCount = result?.nodes?.size ?: 0
        AlertDialog.Builder(this)
            .setTitle(R.string.pipeline_configure_title)
            .setMessage(getString(R.string.pipeline_configure_message, containerName, stepCount, TIMEOUT_MS / 1000))
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
