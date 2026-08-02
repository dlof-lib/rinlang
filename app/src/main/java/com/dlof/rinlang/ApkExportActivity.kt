package com.dlof.rinlang

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.graphics.Typeface
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.dlof.rinlang.apk.RinApkExporter
import java.util.concurrent.Executors

/**
 * شاشة "تصدير APK": تأخذ مشروع Rin محدداً (EXTRA_PROJECT_NAME) وتبنيه عبر [RinApkExporter]
 * كحزمة APK حقيقية موقّعة (انظر توثيق تلك الكائن لتفاصيل الآلية)، بنفس أسلوب الطرفية
 * المستخدم في RinFlow لعرض خطوات البناء والتوقيع سطراً سطراً.
 */
class ApkExportActivity : AppCompatActivity() {

    private val mainHandler = Handler(Looper.getMainLooper())
    private val buildExecutor = Executors.newSingleThreadExecutor()

    private lateinit var project: Project
    private lateinit var logContainer: LinearLayout
    private lateinit var edtAppName: EditText
    private lateinit var spinnerEntry: Spinner
    private lateinit var btnBuild: Button
    private lateinit var progress: ProgressBar
    private lateinit var txtStatus: TextView
    private lateinit var statusBanner: LinearLayout
    private lateinit var resultActionsRow: LinearLayout
    private lateinit var btnInstall: Button
    private lateinit var btnSave: Button
    private lateinit var btnShare: Button

    private var lastResult: RinApkExporter.ExportResult? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_apk_export)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
        val found = projectName?.let { name -> ProjectManager.listProjects(this).find { it.name == name } }
        if (found == null) {
            Toast.makeText(this, getString(R.string.more_row_needs_project_toast), Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        project = found

        logContainer = findViewById(R.id.apkExportLog)
        edtAppName = findViewById(R.id.edtApkExportAppName)
        spinnerEntry = findViewById(R.id.spinnerApkExportEntry)
        btnBuild = findViewById(R.id.btnApkExportBuild)
        progress = findViewById(R.id.progressApkExport)
        txtStatus = findViewById(R.id.txtApkExportStatus)
        statusBanner = findViewById(R.id.apkExportStatusBanner)
        resultActionsRow = findViewById(R.id.rowApkExportResultActions)
        btnInstall = findViewById(R.id.btnApkExportInstall)
        btnSave = findViewById(R.id.btnApkExportSave)
        btnShare = findViewById(R.id.btnApkExportShare)

        findViewById<TextView>(R.id.txtApkExportProjectName).text = "project: ${project.name}"
        findViewById<ImageButton>(R.id.btnApkExportClose).setOnClickListener { finish() }

        edtAppName.setText(project.name)

        val rinFiles = ProjectManager.listFiles(project).filter { it.name.endsWith(".rin") }
        val entryNames = rinFiles.map { it.name }.ifEmpty { listOf("main.rin") }
        spinnerEntry.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, entryNames)
        val mainIndex = entryNames.indexOf("main.rin")
        if (mainIndex >= 0) spinnerEntry.setSelection(mainIndex)

        btnBuild.setOnClickListener { startBuild(rinFiles.isNotEmpty()) }
        btnInstall.setOnClickListener { lastResult?.let { installApk(it.apkFile) } }
        btnSave.setOnClickListener { lastResult?.let { saveToDownloads(it.apkFile) } }
        btnShare.setOnClickListener { lastResult?.let { shareApk(it.apkFile) } }
    }

    override fun onDestroy() {
        super.onDestroy()
        buildExecutor.shutdownNow()
    }

    private fun startBuild(hasEntryFiles: Boolean) {
        if (!hasEntryFiles) {
            Toast.makeText(this, getString(R.string.apk_export_no_entry_error), Toast.LENGTH_SHORT).show()
            return
        }
        logContainer.removeAllViews()
        resultActionsRow.visibility = View.GONE
        lastResult = null
        btnBuild.isEnabled = false
        progress.visibility = View.VISIBLE
        txtStatus.text = getString(R.string.apk_export_status_building)

        val appName = edtAppName.text.toString().trim().ifBlank { project.name }
        val entry = spinnerEntry.selectedItem as? String ?: "main.rin"

        buildExecutor.execute {
            RinApkExporter.export(
                context = applicationContext,
                project = project,
                appDisplayName = appName,
                entryFile = entry
            ) { p ->
                mainHandler.post { handleProgress(p) }
            }
        }
    }

    private fun handleProgress(p: RinApkExporter.Progress) {
        when (p) {
            is RinApkExporter.Progress.Log -> appendLogLine(p.text, p.ok)
            is RinApkExporter.Progress.Done -> {
                lastResult = p.result
                btnBuild.isEnabled = true
                progress.visibility = View.GONE
                txtStatus.text = getString(R.string.apk_export_status_success)
                resultActionsRow.visibility = View.VISIBLE
            }
            is RinApkExporter.Progress.Failed -> {
                appendLogLine("✗ ${p.message}", ok = false)
                btnBuild.isEnabled = true
                progress.visibility = View.GONE
                txtStatus.text = getString(R.string.apk_export_status_failed)
            }
        }
    }

    private fun appendLogLine(text: String, ok: Boolean) {
        val dp = resources.displayMetrics.density
        val line = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12.5f
            setPadding(0, (2 * dp).toInt(), 0, (2 * dp).toInt())
        }
        val colorRes = if (ok) R.color.pipeline_terminal_text else R.color.pipeline_terminal_red
        val sb = SpannableStringBuilder(text)
        sb.setSpan(ForegroundColorSpan(ContextCompat.getColor(this, colorRes)), 0, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (!ok) sb.setSpan(StyleSpan(Typeface.BOLD), 0, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        line.text = sb
        logContainer.addView(line)
    }

    // ---- result actions ----

    private fun apkUri(file: java.io.File): Uri =
        androidx.core.content.FileProvider.getUriForFile(this, "$packageName.fileprovider", file)

    private fun installApk(file: java.io.File) {
        val uri = apkUri(file)
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        startActivity(intent)
    }

    private fun shareApk(file: java.io.File) {
        val uri = apkUri(file)
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "application/vnd.android.package-archive"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        startActivity(Intent.createChooser(intent, getString(R.string.apk_export_action_share)))
    }

    private fun saveToDownloads(file: java.io.File) {
        val artifact = RinArtifact(
            kind = ArtifactKind.APK,
            relPath = file.name,
            absoluteFile = file,
            sizeBytes = file.length()
        )
        RinDownloadManager.downloadToPublicDownloads(
            activity = this,
            artifact = artifact,
            onProgress = { _, _ -> },
            onDone = { uri, error ->
                if (error != null || uri == null) {
                    Toast.makeText(this, error?.message ?: "error", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, getString(R.string.apk_export_action_save), Toast.LENGTH_SHORT).show()
                }
            }
        )
    }

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
    }
}
