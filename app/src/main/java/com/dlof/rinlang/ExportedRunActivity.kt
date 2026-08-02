package com.dlof.rinlang

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.graphics.Typeface
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * نقطة تشغيل حزمة APK مُصدَّرة من ميزة "تصدير APK" (انظر [RinApkExporter] و[ApkExportActivity]):
 * تقرأ `assets/rin_export_manifest.json` + `assets/rin_export_project/` المُحقَنَين داخل الحزمة،
 * تستخرجهما مرة واحدة إلى تخزين خاص، ثم تشغّل ملف البداية عبر [RinEngine] الحقيقي وتعرض مخرجاته
 * بأسلوب طرفية بسيط — بلا محرر أو واجهة IDE الاعتيادية، فتبدو الحزمة كتطبيق مستقل خاص بالمشروع.
 *
 * [SplashActivity] هو من يوجّه هنا تلقائياً عند رصد وجود هذا البيان في assets.
 */
class ExportedRunActivity : AppCompatActivity() {

    private val mainHandler = Handler(Looper.getMainLooper())
    private val runExecutor = Executors.newSingleThreadExecutor()

    private lateinit var logContainer: LinearLayout
    private lateinit var progress: ProgressBar
    private lateinit var btnRestart: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_exported_run)

        logContainer = findViewById(R.id.exportedRunLog)
        progress = findViewById(R.id.progressExportedRun)
        btnRestart = findViewById(R.id.btnExportedRunRestart)
        btnRestart.setOnClickListener { runProject() }

        runProject()
    }

    override fun onDestroy() {
        super.onDestroy()
        runExecutor.shutdownNow()
    }

    private fun runProject() {
        logContainer.removeAllViews()
        btnRestart.visibility = View.GONE
        progress.visibility = View.VISIBLE

        runExecutor.execute {
            try {
                val manifestJson = assets.open(MANIFEST_ASSET_PATH).bufferedReader().use { it.readText() }
                val manifest = JSONObject(manifestJson)
                val displayName = manifest.optString("display_name", getString(R.string.app_name))
                val entry = manifest.optString("entry", "main.rin")

                mainHandler.post { title = displayName }

                val projectDir = File(filesDir, "exported_project")
                extractAssetDir(PROJECT_ASSET_PREFIX, projectDir)

                RinEngine.init(applicationContext, projectDir.absolutePath)
                val entryFile = File(projectDir, entry)
                val source = if (entryFile.exists()) entryFile.readText() else {
                    throw IllegalStateException("$entry not found in exported project")
                }
                val output = RinEngine.runSource(source)

                mainHandler.post {
                    progress.visibility = View.GONE
                    btnRestart.visibility = View.VISIBLE
                    RinConsoleFormatter.formatLines(output).forEach { appendLine(it.text, isError = it.kind == LogKind.ERROR) }
                    if (output.isBlank()) appendLine("(no output)", isError = false)
                }
            } catch (t: Throwable) {
                mainHandler.post {
                    progress.visibility = View.GONE
                    btnRestart.visibility = View.VISIBLE
                    appendLine("${getString(R.string.exported_run_error_prefix)} ${t.message}", isError = true)
                }
            }
        }
    }

    private fun extractAssetDir(assetPath: String, target: File) {
        target.mkdirs()
        val children = assets.list(assetPath) ?: return
        for (child in children) {
            val childAssetPath = if (assetPath.isEmpty()) child else "$assetPath/$child"
            val subChildren = assets.list(childAssetPath)
            if (subChildren != null && subChildren.isNotEmpty()) {
                extractAssetDir(childAssetPath, File(target, child))
            } else {
                assets.open(childAssetPath).use { input ->
                    File(target, child).outputStream().use { output -> input.copyTo(output) }
                }
            }
        }
    }

    private fun appendLine(text: String, isError: Boolean) {
        val dp = resources.displayMetrics.density
        val line = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 13f
            setPadding(0, (3 * dp).toInt(), 0, (3 * dp).toInt())
        }
        val colorRes = if (isError) R.color.pipeline_terminal_red else R.color.pipeline_terminal_text
        val sb = SpannableStringBuilder(text)
        sb.setSpan(ForegroundColorSpan(ContextCompat.getColor(this, colorRes)), 0, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        line.text = sb
        logContainer.addView(line)
    }

    companion object {
        private const val MANIFEST_ASSET_PATH = "rin_export_manifest.json"
        private const val PROJECT_ASSET_PREFIX = "rin_export_project"

        /** يفحص وجود بيان تصدير داخل assets الحزمة الحالية (تُستدعى من [SplashActivity]). */
        fun hasExportedProject(context: android.content.Context): Boolean =
            try {
                context.assets.open(MANIFEST_ASSET_PATH).close()
                true
            } catch (e: Exception) {
                false
            }
    }
}
