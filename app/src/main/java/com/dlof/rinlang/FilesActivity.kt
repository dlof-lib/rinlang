package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "الملفات" الخاصة بمشروع واحد: تعرض كل ملفات .rin داخل مجلده،
 * وتتيح:
 *  - "إضافة/رفع ملف" (upload): استيراد ملف .rin موجود بالفعل على الجهاز
 *    (تخزين محلي، Google Drive، إلخ) عبر SAF، فيُنسخ داخل المشروع.
 *  - إنشاء ملف جديد فارغ بالاسم المطلوب.
 *  - فتح ملف في المحرر ([MainActivity]) للتعديل والتشغيل.
 *  - حذف ملف.
 */
class FilesActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
    }

    private lateinit var project: Project
    private lateinit var rvFiles: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var adapter: FilesAdapter

    private val importFileLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) importFile(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_files)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
            ?: run { finish(); return }
        val existing = ProjectManager.listProjects(this).find { it.name == projectName }
            ?: run {
                Toast.makeText(this, R.string.project_not_found, Toast.LENGTH_SHORT).show()
                finish(); return
            }
        project = existing
        title = project.name

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.files_screen_title)
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            text = project.name
            visibility = View.VISIBLE
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvFiles = findViewById(R.id.rvFiles)
        txtEmpty = findViewById(R.id.txtEmptyFiles)
        val fabAddFile: View = findViewById(R.id.fabAddFile)
        val fabUploadFile: View = findViewById(R.id.fabUploadFile)

        adapter = FilesAdapter(
            onOpen = { file -> openInEditor(file) },
            onDelete = { file -> showDeleteConfirm(file) }
        )
        rvFiles.layoutManager = LinearLayoutManager(this)
        rvFiles.adapter = adapter

        fabAddFile.setOnClickListener { showCreateFileDialog() }
        fabUploadFile.setOnClickListener {
            // نقبل .rin أو أي نص عادي، لأن أندرويد لا يربط MIME type رسمياً بامتداد .rin.
            importFileLauncher.launch(arrayOf("text/plain", "application/octet-stream", "*/*"))
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val files = ProjectManager.listFiles(project)
        adapter.submit(files)
        txtEmpty.visibility = if (files.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun importFile(uri: Uri) {
        try {
            val file = ProjectManager.importFileFromUri(this, project, uri)
            refresh()
            Toast.makeText(this, getString(R.string.file_uploaded_toast, file.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun showCreateFileDialog() {
        val input = EditText(this)
        input.hint = getString(R.string.file_name_hint)
        AlertDialog.Builder(this)
            .setTitle(R.string.new_file_title)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                val file = ProjectManager.writeFile(project, name, "")
                refresh()
                openInEditor(file)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showDeleteConfirm(file: RinFile) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_file_title)
            .setMessage(getString(R.string.delete_file_confirm, file.name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectManager.deleteFile(file)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun openInEditor(file: RinFile) {
        val intent = Intent(this, MainActivity::class.java)
        intent.putExtra(MainActivity.EXTRA_PROJECT_NAME, project.name)
        intent.putExtra(MainActivity.EXTRA_FILE_NAME, file.name)
        startActivity(intent)
    }
}

private class FilesAdapter(
    val onOpen: (RinFile) -> Unit,
    val onDelete: (RinFile) -> Unit
) : RecyclerView.Adapter<FilesAdapter.VH>() {

    private var items: List<RinFile> = emptyList()
    private val dateFormat = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)

    fun submit(newItems: List<RinFile>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtFileNameItem)
        val txtMeta: TextView = view.findViewById(R.id.txtFileMeta)
        val btnDelete: View = view.findViewById(R.id.btnDeleteFile)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_file, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val file = items[position]
        holder.txtName.text = file.name
        holder.txtMeta.text = holder.itemView.context.getString(
            R.string.file_meta_format,
            formatSize(file.sizeBytes),
            dateFormat.format(Date(file.lastModified))
        )
        holder.itemView.setOnClickListener { onOpen(file) }
        holder.btnDelete.setOnClickListener { onDelete(file) }
    }

    override fun getItemCount(): Int = items.size

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> "${bytes / (1024 * 1024)} MB"
    }
}
