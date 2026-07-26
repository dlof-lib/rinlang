package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.text.InputType
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
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "المكتبات" الخاصة بمشروع واحد (يفتحها زر "المكتبات" داخل المحرر [MainActivity]).
 *
 * تتيح لأي مستخدم:
 *  - "رفع مكتبة" (upload): استيراد ملف مكتبة .og.rin موجود بالفعل على الجهاز عبر SAF.
 *  - إنشاء مكتبة جديدة فارغة (بقالب بسيط جاهز) وفتحها للتعديل مباشرة في المحرر.
 *  - تعديل/حذف أي مكتبة من مكتباته الخاصة.
 *  - تصفّح المكتبات القياسية الخمس الجاهزة (مدمجة داخل المفسّر، تعمل فوراً بلا رفع).
 *  - "إدراج" سطر @import المناسب مباشرة داخل كود المحرر بضغطة واحدة (تُغلق هذه الشاشة
 *    وتُعيد السطر إلى [MainActivity] عبر setResult، فيُدرَج عند مكان المؤشر في الكود).
 */
class LibrariesActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** النتيجة المُعادة إلى المحرر: نص سطر @import الجاهز للإدراج عند المؤشر. */
        const val EXTRA_IMPORT_STATEMENT = "extra_import_statement"
    }

    private lateinit var project: Project
    private lateinit var rvUserLibraries: RecyclerView
    private lateinit var rvBuiltinLibraries: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var userAdapter: UserLibraryAdapter

    private val importLibraryLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) uploadLibrary(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_libraries)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
            ?: run { finish(); return }
        val existing = ProjectManager.listProjects(this).find { it.name == projectName }
            ?: run {
                Toast.makeText(this, R.string.project_not_found, Toast.LENGTH_SHORT).show()
                finish(); return
            }
        project = existing
        title = getString(R.string.libraries_title_format, project.name)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.libraries_screen_title)
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            text = project.name
            visibility = View.VISIBLE
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvUserLibraries = findViewById(R.id.rvUserLibraries)
        rvBuiltinLibraries = findViewById(R.id.rvBuiltinLibraries)
        txtEmpty = findViewById(R.id.txtEmptyLibraries)
        val fabUploadLibrary: View = findViewById(R.id.fabUploadLibrary)
        val fabNewLibrary: View = findViewById(R.id.fabNewLibrary)

        userAdapter = UserLibraryAdapter(
            onEdit = { lib -> openLibraryInEditor(lib) },
            onDelete = { lib -> showDeleteConfirm(lib) },
            onInsert = { lib -> finishWithImport("@import \"${ProjectManagerLibPrefix}${lib.name}\";") }
        )
        rvUserLibraries.layoutManager = LinearLayoutManager(this)
        rvUserLibraries.adapter = userAdapter

        val builtinAdapter = BuiltinLibraryAdapter { info ->
            finishWithImport("@import \"${info.importPath}\";")
        }
        rvBuiltinLibraries.layoutManager = LinearLayoutManager(this)
        rvBuiltinLibraries.adapter = builtinAdapter
        builtinAdapter.submit(BuiltinLibraries.all)

        fabUploadLibrary.setOnClickListener {
            // نقبل .og.rin أو أي نص عادي، لأن أندرويد لا يربط MIME type رسمياً بهذا الامتداد المخصص.
            importLibraryLauncher.launch(arrayOf("text/plain", "application/octet-stream", "*/*"))
        }
        fabNewLibrary.setOnClickListener { showCreateLibraryDialog() }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val libraries = ProjectManager.listLibraries(project)
        userAdapter.submit(libraries)
        txtEmpty.visibility = if (libraries.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun uploadLibrary(uri: Uri) {
        try {
            val lib = ProjectManager.importLibraryFromUri(this, project, uri)
            refresh()
            Toast.makeText(this, getString(R.string.library_uploaded_toast, lib.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun showCreateLibraryDialog() {
        val input = EditText(this)
        input.hint = getString(R.string.library_name_hint)
        input.inputType = InputType.TYPE_CLASS_TEXT
        AlertDialog.Builder(this)
            .setTitle(R.string.new_library_title)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                try {
                    val lib = ProjectManager.createLibrary(project, name)
                    refresh()
                    openLibraryInEditor(lib)
                } catch (t: Throwable) {
                    Toast.makeText(this, t.message ?: getString(R.string.file_save_error), Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showDeleteConfirm(lib: RinLibrary) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_library_title)
            .setMessage(getString(R.string.delete_library_confirm, lib.name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectManager.deleteLibrary(lib)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يفتح مكتبة المستخدم في نفس محرّر الشيفرة (MainActivity)، للاستفادة من التلوين النحوي والتراجع/الإعادة. */
    private fun openLibraryInEditor(lib: RinLibrary) {
        val intent = Intent(this, MainActivity::class.java)
        intent.putExtra(MainActivity.EXTRA_PROJECT_NAME, project.name)
        intent.putExtra(MainActivity.EXTRA_LIBRARY_NAME, lib.name)
        startActivity(intent)
    }

    /** يُعيد سطر @import الجاهز إلى المحرر الذي فتح هذه الشاشة، ثم يغلقها. */
    private fun finishWithImport(importStatement: String) {
        val result = Intent()
        result.putExtra(EXTRA_IMPORT_STATEMENT, importStatement)
        setResult(RESULT_OK, result)
        finish()
    }
}

/** بادئة المسار الذي تُحفَظ تحته كل مكتبات المستخدم فعلياً، لبناء سطر @import الصحيح. */
private const val ProjectManagerLibPrefix = "lib/"

private class UserLibraryAdapter(
    val onEdit: (RinLibrary) -> Unit,
    val onDelete: (RinLibrary) -> Unit,
    val onInsert: (RinLibrary) -> Unit
) : RecyclerView.Adapter<UserLibraryAdapter.VH>() {

    private var items: List<RinLibrary> = emptyList()
    private val dateFormat = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)

    fun submit(newItems: List<RinLibrary>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtLibraryNameItem)
        val txtMeta: TextView = view.findViewById(R.id.txtLibraryMeta)
        val btnDelete: View = view.findViewById(R.id.btnDeleteLibrary)
        val btnEdit: View = view.findViewById(R.id.btnEditLibrary)
        val btnInsert: View = view.findViewById(R.id.btnInsertLibrary)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_library, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val lib = items[position]
        holder.txtName.text = lib.name
        holder.txtMeta.text = holder.itemView.context.getString(
            R.string.file_meta_format,
            formatSize(lib.sizeBytes),
            dateFormat.format(Date(lib.lastModified))
        )
        holder.btnEdit.setOnClickListener { onEdit(lib) }
        holder.btnDelete.setOnClickListener { onDelete(lib) }
        holder.btnInsert.setOnClickListener { onInsert(lib) }
    }

    override fun getItemCount(): Int = items.size

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> "${bytes / (1024 * 1024)} MB"
    }
}

private class BuiltinLibraryAdapter(
    val onInsert: (BuiltinLibraryInfo) -> Unit
) : RecyclerView.Adapter<BuiltinLibraryAdapter.VH>() {

    private var items: List<BuiltinLibraryInfo> = emptyList()

    fun submit(newItems: List<BuiltinLibraryInfo>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtBuiltinNameItem)
        val txtDescription: TextView = view.findViewById(R.id.txtBuiltinDescription)
        val txtFunctions: TextView = view.findViewById(R.id.txtBuiltinFunctions)
        val btnInsert: View = view.findViewById(R.id.btnInsertBuiltin)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_builtin_library, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val info = items[position]
        holder.txtName.text = info.importPath
        holder.txtDescription.text = info.description
        holder.txtFunctions.text = info.sampleFunctions
        holder.btnInsert.setOnClickListener { onInsert(info) }
    }

    override fun getItemCount(): Int = items.size
}
