package com.dlof.rinlang

import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter

/**
 * IDE-style activity for the Rin language:
 *  - A code editor with line numbers, syntax highlighting, undo/redo,
 *    auto-indent and find/replace ([CodeEditorController], [RinSyntaxHighlighter]).
 *  - A "Run" button that hands source off to the native C++ engine through
 *    [RinEngine], scheduled and tracked by [RinJobScheduler] so runs never
 *    block the UI and every execution is kept as its own timestamped entry
 *    (mirroring how the Rin language itself organizes data into containers).
 *  - Open/Save buttons that read and write `.rin` files via SAF.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var editCode: EditText
    private lateinit var txtLineNumbers: TextView
    private lateinit var txtEngineVersion: TextView
    private lateinit var txtFileName: TextView
    private lateinit var rvJobs: RecyclerView
    private lateinit var progressRunning: ProgressBar
    private lateinit var findBar: LinearLayout
    private lateinit var txtFind: EditText
    private lateinit var txtReplace: EditText

    private lateinit var editorController: CodeEditorController
    private lateinit var jobAdapter: RinJobAdapter

    /** URI of the file currently open, if any. Null means "unsaved / new file". */
    private var currentUri: Uri? = null

    // --- Storage Access Framework launchers ---

    private val openDocumentLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) openFile(uri)
        }

    private val createDocumentLauncher =
        registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
            if (uri != null) writeToUri(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        editCode = findViewById(R.id.editCode)
        txtLineNumbers = findViewById(R.id.txtLineNumbers)
        txtEngineVersion = findViewById(R.id.txtEngineVersion)
        txtFileName = findViewById(R.id.txtFileName)
        rvJobs = findViewById(R.id.rvJobs)
        progressRunning = findViewById(R.id.progressRunning)
        findBar = findViewById(R.id.findBar)
        txtFind = findViewById(R.id.txtFind)
        txtReplace = findViewById(R.id.txtReplace)

        val btnPipeline: Button = findViewById(R.id.btnPipeline)
        val btnRun: Button = findViewById(R.id.btnRun)
        val btnClear: Button = findViewById(R.id.btnClear)
        val btnOpen: Button = findViewById(R.id.btnOpen)
        val btnSave: Button = findViewById(R.id.btnSave)
        val btnUndo: Button = findViewById(R.id.btnUndo)
        val btnRedo: Button = findViewById(R.id.btnRedo)
        val btnFind: Button = findViewById(R.id.btnFind)
        val btnFindNext: Button = findViewById(R.id.btnFindNext)
        val btnReplaceOne: Button = findViewById(R.id.btnReplaceOne)
        val btnReplaceAll: Button = findViewById(R.id.btnReplaceAll)
        val btnFindClose: ImageButton = findViewById(R.id.btnFindClose)
        val btnClearConsole: Button = findViewById(R.id.btnClearConsole)

        if (savedInstanceState == null) {
            editCode.setText(getString(R.string.sample_program))
        }

        // تلوين الصيغة النحوية (syntax highlighting) أثناء الكتابة
        RinSyntaxHighlighter.attach(this, editCode)
        // أرقام الأسطر + تراجع/إعادة + مسافة بادئة تلقائية
        editorController = CodeEditorController(editCode, txtLineNumbers)

        // قائمة التشغيل المجدولة (job queue): كل عملية Run بطاقة مستقلة
        jobAdapter = RinJobAdapter(this)
        rvJobs.layoutManager = LinearLayoutManager(this)
        rvJobs.adapter = jobAdapter
        RinJobScheduler.onJobsChanged = { jobs ->
            jobAdapter.submit(jobs)
            if (jobs.isNotEmpty()) rvJobs.scrollToPosition(jobs.size - 1)
            progressRunning.visibility =
                if (jobs.any { it.status == JobStatus.RUNNING }) android.view.View.VISIBLE
                else android.view.View.GONE
        }

        txtEngineVersion.text = try {
            RinEngine.engineVersion()
        } catch (t: Throwable) {
            "engine unavailable"
        }

        btnRun.setOnClickListener {
            val source = editCode.text.toString()
            RinJobScheduler.submit(source)
        }

        btnPipeline.setOnClickListener {
            val source = editCode.text.toString()
            val intent = android.content.Intent(this, PipelineRunnerActivity::class.java)
            intent.putExtra(PipelineRunnerActivity.EXTRA_CODE, source)
            startActivity(intent)
        }

        btnClear.setOnClickListener {
            editCode.setText("")
        }

        btnClearConsole.setOnClickListener {
            RinJobScheduler.clear()
        }

        btnUndo.setOnClickListener { editorController.undo() }
        btnRedo.setOnClickListener { editorController.redo() }

        btnFind.setOnClickListener {
            findBar.visibility =
                if (findBar.visibility == android.view.View.VISIBLE) android.view.View.GONE
                else android.view.View.VISIBLE
        }
        btnFindClose.setOnClickListener { findBar.visibility = android.view.View.GONE }
        btnFindNext.setOnClickListener {
            val found = editorController.findNext(txtFind.text.toString())
            if (!found) {
                Toast.makeText(this, getString(R.string.find_not_found_toast, txtFind.text.toString()), Toast.LENGTH_SHORT).show()
            }
        }
        btnReplaceOne.setOnClickListener {
            editorController.replaceOne(txtFind.text.toString(), txtReplace.text.toString())
        }
        btnReplaceAll.setOnClickListener {
            val count = editorController.replaceAll(txtFind.text.toString(), txtReplace.text.toString())
            Toast.makeText(this, getString(R.string.replaced_count_toast, count), Toast.LENGTH_SHORT).show()
        }

        btnOpen.setOnClickListener {
            // نقبل .rin كامتداد، لكن نسمح أيضاً بأي نص عادي لأن أندرويد لا يربط
            // MIME type رسمياً بامتداد .rin غير المعروف لديه.
            openDocumentLauncher.launch(arrayOf("text/plain", "application/octet-stream", "*/*"))
        }

        btnSave.setOnClickListener {
            val existing = currentUri
            if (existing != null) {
                writeToUri(existing)
            } else {
                createDocumentLauncher.launch(suggestedFileName())
            }
        }
    }

    private fun suggestedFileName(): String = "program.rin"

    private fun openFile(uri: Uri) {
        try {
            contentResolver.openInputStream(uri)?.use { input ->
                BufferedReader(InputStreamReader(input)).use { reader ->
                    val text = reader.readText()
                    editCode.setText(text)
                }
            }
            currentUri = uri
            val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "opened.rin"
            txtFileName.text = name
            Toast.makeText(this, getString(R.string.file_opened_toast, name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun writeToUri(uri: Uri) {
        try {
            contentResolver.openOutputStream(uri, "wt")?.use { output ->
                OutputStreamWriter(output).use { writer ->
                    writer.write(editCode.text.toString())
                }
            }
            currentUri = uri
            val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "program.rin"
            txtFileName.text = name
            Toast.makeText(this, getString(R.string.file_saved_toast, name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        return try {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) cursor.getString(idx) else null
                } else null
            }
        } catch (t: Throwable) {
            null
        }
    }
}
