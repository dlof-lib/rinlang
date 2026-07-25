package com.dlof.rinlang

import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter

/**
 * Simple IDE-style activity: a plain-text code editor for the Rin
 * language (with live syntax highlighting), a "Run" button that hands
 * the source off to the native C++ engine through [RinEngine], a
 * console that shows the result, and Open/Save buttons that read and
 * write `.rin` files from the device's storage via the Storage Access
 * Framework (SAF).
 */
class MainActivity : AppCompatActivity() {

    private lateinit var editCode: EditText
    private lateinit var txtOutput: TextView
    private lateinit var txtEngineVersion: TextView
    private lateinit var txtFileName: TextView

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
        txtOutput = findViewById(R.id.txtOutput)
        txtEngineVersion = findViewById(R.id.txtEngineVersion)
        txtFileName = findViewById(R.id.txtFileName)

        val btnRun: Button = findViewById(R.id.btnRun)
        val btnClear: Button = findViewById(R.id.btnClear)
        val btnOpen: Button = findViewById(R.id.btnOpen)
        val btnSave: Button = findViewById(R.id.btnSave)

        if (savedInstanceState == null) {
            editCode.setText(getString(R.string.sample_program))
        }

        // تلوين الصيغة النحوية (syntax highlighting) أثناء الكتابة
        RinSyntaxHighlighter.attach(this, editCode)

        txtEngineVersion.text = try {
            RinEngine.engineVersion()
        } catch (t: Throwable) {
            "engine unavailable"
        }

        btnRun.setOnClickListener {
            val source = editCode.text.toString()
            txtOutput.text = try {
                RinEngine.runSource(source)
            } catch (t: Throwable) {
                "[Fatal error]: ${t.message}"
            }
        }

        btnClear.setOnClickListener {
            editCode.setText("")
            txtOutput.text = ""
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

    private fun suggestedFileName(): String {
        val firstLine = editCode.text.toString().lineSequence().firstOrNull { it.isNotBlank() }
        return "program.rin"
    }

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
