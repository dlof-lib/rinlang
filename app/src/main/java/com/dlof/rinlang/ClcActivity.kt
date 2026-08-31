package com.dlof.rinlang

import android.app.AlertDialog
import android.net.Uri
import android.os.Bundle
import android.text.InputType
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageButton
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
 * شاشة "CLC" الخاصة بمشروع واحد (يفتحها صف "CLC" داخل شاشة "المزيد" [MoreActivity]).
 *
 * قسم CLC (Rin Compact Library Container، حاويات .rcl) مبني بالكامل فوق natives المفسّر
 * المُسجَّلة فعلياً في rin_interpreter.cpp (انظر CLC_INTEGRATION.md): `clcContainerOpen/Close/
 * FileCount/FileName/MetaName/MetaVersion`، `libraryExport(outPath, srcDir)`،
 * `libraryImport(path)`. هذه الشاشة لا تضيف أي جسر JNI جديد؛ كل تفاعل معها هو ببساطة توليد مقتطف
 * كود Rin صغير وتشغيله عبر [RinEngine.runSourceStructured] (بنفس الطريقة التي يُشغَّل بها أي كود Rin
 * آخر في التطبيق)، مع `baseDir` مضبوطاً على مجلد هذا المشروع بالذات — فتُحل كل المسارات (مصدر
 * التصدير، ملف الحاوية الناتج، ملف .rcl المستورَد) نسبةً إلى مجلد المشروع، ويبقى عزل المشروعات
 * (basePath sandbox) كما هو أينما استُخدم.
 *
 * تتيح هذه الشاشة:
 *  - "تصدير مكتبة": بناء حاوية .rcl من أي مجلد فرعي داخل المشروع (libraryExport).
 *  - تصفّح كل ملفات .rcl الموجودة فعلياً داخل مجلد المشروع (بحث متكرر محدود العمق)، بالإضافة إلى
 *    "استيراد من الجهاز" (SAF) لإحضار حاوية .rcl خارجية إلى داخل المشروع أولاً (طبقة عزل المسارات
 *    تعمل على basePath المشروع، فيجب أن يكون الملف بداخله قبل أي عملية عليه).
 *  - "فحص" أي حاوية: يعرض حقول Metadata (الاسم/الإصدار) وعدد وأسماء الملفات بداخلها، بلا استيراد
 *    فعلي (عبر container.open/close ونواتها).
 *  - "استيراد وتشغيل": ينفّذ library.import فعلياً (يدمج كل ملفات .rin بداخلها في نطاق التشغيل)،
 *    مع مساحة اختيارية لكتابة كود Rin إضافي يُشغَّل بعدها مباشرة (لاستدعاء دوال المكتبة المستورَدة)،
 *    ويعرض مخرجات التشغيل الحقيقية (أو رسالة الخطأ التشخيصية عند الفشل).
 */
class ClcActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"

        /** أقصى عمق تنقيب عن مجلدات فرعية (لاختيار مصدر التصدير) أو ملفات .rcl (للاستيراد). */
        private const val MAX_SCAN_DEPTH = 6
        private const val RCL_EXTENSION = ".rcl"

        /** مجلدات داخلية للمشروع لا معنى لعرضها كمرشّح لمصدر تصدير أو نتيجة بحث عن حاويات. */
        private val INTERNAL_DIR_NAMES = setOf("rin_installed", ".rin_clc_tmp")
    }

    private lateinit var project: Project
    private lateinit var edtExportSrcDir: EditText
    private lateinit var edtExportOutName: EditText
    private lateinit var txtExportResult: TextView
    private lateinit var rvContainers: RecyclerView
    private lateinit var txtEmptyContainers: View
    private lateinit var containerAdapter: ClcContainerAdapter

    private val importContainerLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) importContainerFromDevice(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_clc)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
            ?: run { finish(); return }
        val existing = ProjectManager.listProjects(this).find { it.name == projectName }
            ?: run {
                Toast.makeText(this, R.string.project_not_found, Toast.LENGTH_SHORT).show()
                finish(); return
            }
        project = existing
        // نطاق الملفات (save/installation/file وكل natives CLC) مضبوط على مجلد هذا المشروع فقط،
        // بنفس ما يفعله المحرر وشاشة "المكتبات" — انظر RinEngine.kt للتفاصيل.
        RinEngine.init(applicationContext, project.dir.absolutePath)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.clc_screen_title)
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            text = project.name
            visibility = View.VISIBLE
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        edtExportSrcDir = findViewById(R.id.edtExportSrcDir)
        edtExportOutName = findViewById(R.id.edtExportOutName)
        txtExportResult = findViewById(R.id.txtExportResult)
        rvContainers = findViewById(R.id.rvContainers)
        txtEmptyContainers = findViewById(R.id.txtEmptyContainers)

        findViewById<ImageButton>(R.id.btnBrowseSrcDir).setOnClickListener { showFolderPicker() }
        findViewById<View>(R.id.btnExportNow).setOnClickListener { exportLibrary() }
        findViewById<View>(R.id.btnImportFromDevice).setOnClickListener {
            // .rcl امتداد مخصص بلا نوع MIME رسمي على أندرويد، فنقبل أي ملف كما تفعل شاشة "المكتبات".
            importContainerLauncher.launch(arrayOf("*/*"))
        }

        containerAdapter = ClcContainerAdapter(
            onInspect = { entry -> inspectContainer(entry) },
            onImportAndRun = { entry -> showImportAndRunDialog(entry) },
            onDelete = { entry -> showDeleteConfirm(entry) }
        )
        rvContainers.layoutManager = LinearLayoutManager(this)
        rvContainers.adapter = containerAdapter
    }

    override fun onResume() {
        super.onResume()
        refreshContainers()
    }

    // ---------------------------------------------------------------------------------------
    // تصدير مكتبة (libraryExport)
    // ---------------------------------------------------------------------------------------

    /** يجمع كل المجلدات الفرعية داخل المشروع (بعمق محدود)، لعرضها كمرشّحات لمصدر التصدير. */
    private fun collectFolderChoices(): List<String> {
        val out = mutableListOf<String>()
        fun walk(relDir: String, depth: Int) {
            if (depth <= 0) return
            val (folders, _) = ProjectManager.listEntries(project, relDir)
            for (f in folders) {
                if (f.name in INTERNAL_DIR_NAMES) continue
                out.add(f.relPath)
                walk(f.relPath, depth - 1)
            }
        }
        walk("", MAX_SCAN_DEPTH)
        return out
    }

    private fun showFolderPicker() {
        val folders = collectFolderChoices()
        val rootLabel = getString(R.string.clc_export_root_option)
        val labels = (listOf(rootLabel) + folders).toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(R.string.clc_export_src_hint)
            .setItems(labels) { _, which ->
                edtExportSrcDir.setText(if (which == 0) "" else folders[which - 1])
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun exportLibrary() {
        val srcDir = edtExportSrcDir.text.toString().trim()
        var outName = edtExportOutName.text.toString().trim()
        if (outName.isEmpty()) outName = "library.rcl"
        if (!outName.endsWith(RCL_EXTENSION)) outName += RCL_EXTENSION

        if (srcDir.isEmpty()) {
            // مجلد المصدر فارغ يعني جذر المشروع نفسه؛ نمنعه صراحة هنا لتفادي حاوية ضخمة تضم
            // ملف الحاوية الناتج نفسه ومجلدات داخلية (rin_installed، .rin_clc_tmp) بالخطأ.
            Toast.makeText(this, R.string.clc_export_src_required_toast, Toast.LENGTH_SHORT).show()
            return
        }

        txtExportResult.visibility = View.GONE
        val source = buildString {
            append("let size = libraryExport(\"").append(escapeRinString(outName)).append("\", \"")
                .append(escapeRinString(srcDir)).append("\");\n")
            append("print \"CLC_EXPORT_SIZE:\" + size;\n")
        }
        val result = RinEngine.runSourceStructured(source)
        if (result.success) {
            val sizeText = result.output.lineSequence()
                .firstOrNull { it.startsWith("CLC_EXPORT_SIZE:") }
                ?.removePrefix("CLC_EXPORT_SIZE:")?.trim()?.toDoubleOrNull()?.toLong()
            txtExportResult.text = getString(
                R.string.clc_export_success_format,
                outName,
                formatSize(sizeText ?: 0L)
            )
            txtExportResult.setTextColor(getColorCompat(R.color.status_success))
            txtExportResult.visibility = View.VISIBLE
            refreshContainers()
        } else {
            txtExportResult.text = getString(
                R.string.clc_export_error_format,
                result.diagnosticText ?: result.errorMessage ?: getString(R.string.file_save_error)
            )
            txtExportResult.setTextColor(getColorCompat(R.color.status_error))
            txtExportResult.visibility = View.VISIBLE
        }
    }

    // ---------------------------------------------------------------------------------------
    // اكتشاف حاويات .rcl الموجودة داخل المشروع + استيرادها من الجهاز
    // ---------------------------------------------------------------------------------------

    private fun refreshContainers() {
        val found = mutableListOf<ClcContainerEntry>()
        fun walk(dir: File, relDir: String, depth: Int) {
            if (depth <= 0) return
            val children = dir.listFiles() ?: return
            for (child in children) {
                if (child.isDirectory) {
                    if (child.name in INTERNAL_DIR_NAMES) continue
                    walk(child, if (relDir.isEmpty()) child.name else "$relDir/${child.name}", depth - 1)
                } else if (child.isFile && child.name.endsWith(RCL_EXTENSION)) {
                    val relPath = if (relDir.isEmpty()) child.name else "$relDir/${child.name}"
                    found.add(ClcContainerEntry(relPath, child))
                }
            }
        }
        walk(project.dir, "", MAX_SCAN_DEPTH)
        found.sortByDescending { it.file.lastModified() }

        containerAdapter.submit(found)
        txtEmptyContainers.visibility = if (found.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun importContainerFromDevice(uri: Uri) {
        try {
            val imported = ProjectManager.importFileFromUri(this, project, uri)
            refreshContainers()
            Toast.makeText(this, getString(R.string.clc_imported_toast, imported.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun showDeleteConfirm(entry: ClcContainerEntry) {
        AlertDialog.Builder(this)
            .setTitle(R.string.clc_delete_container_title)
            .setMessage(getString(R.string.clc_delete_container_confirm, entry.relPath))
            .setPositiveButton(R.string.delete) { _, _ ->
                entry.file.delete()
                refreshContainers()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    // ---------------------------------------------------------------------------------------
    // فحص حاوية (container.open/metaName/metaVersion/fileCount/fileName/close) — قراءة فقط
    // ---------------------------------------------------------------------------------------

    private fun inspectContainer(entry: ClcContainerEntry) {
        val source = buildString {
            append("let h = clcContainerOpen(\"").append(escapeRinString(entry.relPath)).append("\");\n")
            append("print \"CLC_NAME:\" + clcContainerMetaName(h);\n")
            append("print \"CLC_VERSION:\" + clcContainerMetaVersion(h);\n")
            append("let n = clcContainerFileCount(h);\n")
            append("print \"CLC_COUNT:\" + n;\n")
            append("let i = 0;\n")
            append("while (i < n) {\n")
            append("    print \"CLC_FILE:\" + clcContainerFileName(h, i);\n")
            append("    i = i + 1;\n")
            append("}\n")
            append("clcContainerClose(h);\n")
        }
        val result = RinEngine.runSourceStructured(source)
        if (!result.success) {
            AlertDialog.Builder(this)
                .setTitle(getString(R.string.clc_inspect_title_format, entry.relPath))
                .setMessage(result.diagnosticText ?: result.errorMessage ?: getString(R.string.file_open_error))
                .setPositiveButton(android.R.string.ok, null)
                .show()
            return
        }

        var name = ""
        var version = ""
        var count = 0
        val files = mutableListOf<String>()
        for (line in result.output.lineSequence()) {
            when {
                line.startsWith("CLC_NAME:") -> name = line.removePrefix("CLC_NAME:")
                line.startsWith("CLC_VERSION:") -> version = line.removePrefix("CLC_VERSION:")
                line.startsWith("CLC_COUNT:") -> count = line.removePrefix("CLC_COUNT:").trim().toDoubleOrNull()?.toInt() ?: 0
                line.startsWith("CLC_FILE:") -> files.add(line.removePrefix("CLC_FILE:"))
            }
        }

        val message = buildString {
            append(getString(R.string.clc_inspect_name)).append(' ')
            append(name.ifBlank { "—" }).append('\n')
            append(getString(R.string.clc_inspect_version)).append(' ')
            append(version.ifBlank { "—" }).append('\n')
            append(getString(R.string.clc_inspect_count)).append(' ').append(count).append('\n')
            if (files.isNotEmpty()) {
                append('\n').append(getString(R.string.clc_inspect_files_header)).append('\n')
                for (f in files) append("• ").append(f).append('\n')
            }
        }.trim()

        AlertDialog.Builder(this)
            .setTitle(getString(R.string.clc_inspect_title_format, entry.relPath))
            .setMessage(message)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    // ---------------------------------------------------------------------------------------
    // استيراد وتشغيل (libraryImport + كود Rin اختياري إضافي)
    // ---------------------------------------------------------------------------------------

    private fun showImportAndRunDialog(entry: ClcContainerEntry) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_clc_run_import, null, false)
        val edtExtraCode = view.findViewById<EditText>(R.id.edtExtraCode)
        val txtOutput = view.findViewById<TextView>(R.id.txtRunOutput)
        edtExtraCode.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE

        val dialog = AlertDialog.Builder(this)
            .setTitle(getString(R.string.clc_run_dialog_title_format, entry.relPath))
            .setView(view)
            .setPositiveButton(R.string.action_run, null) // نعترض النقر أدناه حتى لا يُغلق الحوار تلقائياً
            .setNegativeButton(R.string.cancel, null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val extra = edtExtraCode.text.toString()
                val source = buildString {
                    append("let imported = libraryImport(\"").append(escapeRinString(entry.relPath)).append("\");\n")
                    append("print \"CLC_IMPORTED:\" + len(imported);\n")
                    if (extra.isNotBlank()) append(extra).append('\n')
                }
                val result = RinEngine.runSourceStructured(source)
                txtOutput.visibility = View.VISIBLE
                if (result.success) {
                    txtOutput.setTextColor(getColorCompat(R.color.rin_on_toolbar))
                    txtOutput.text = getString(R.string.clc_run_success_prefix) + "\n" + result.output.trim()
                } else {
                    txtOutput.setTextColor(getColorCompat(R.color.status_error))
                    txtOutput.text = getString(R.string.clc_run_error_prefix) + "\n" +
                        (result.diagnosticText ?: result.errorMessage ?: "")
                }
            }
        }
        dialog.show()
    }

    // ---------------------------------------------------------------------------------------
    // أدوات مساعدة
    // ---------------------------------------------------------------------------------------

    /** يهرب علامات الاقتباس والشرطة المائلة العكسية قبل تضمين نص داخل حرفية سلسلة "..." في Rin. */
    private fun escapeRinString(raw: String): String =
        raw.replace("\\", "\\\\").replace("\"", "\\\"")

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> "${"%.1f".format(bytes / (1024.0 * 1024.0))} MB"
    }

    private fun getColorCompat(colorRes: Int): Int = androidx.core.content.ContextCompat.getColor(this, colorRes)
}

/** حاوية .rcl واحدة عُثر عليها داخل مجلد المشروع (بحث متكرر)، أو أُحضِرت لتوّها من الجهاز. */
private data class ClcContainerEntry(val relPath: String, val file: File)

private class ClcContainerAdapter(
    val onInspect: (ClcContainerEntry) -> Unit,
    val onImportAndRun: (ClcContainerEntry) -> Unit,
    val onDelete: (ClcContainerEntry) -> Unit
) : RecyclerView.Adapter<ClcContainerAdapter.VH>() {

    private var items: List<ClcContainerEntry> = emptyList()
    private val dateFormat = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)

    fun submit(newItems: List<ClcContainerEntry>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtClcNameItem)
        val txtMeta: TextView = view.findViewById(R.id.txtClcMeta)
        val btnDelete: View = view.findViewById(R.id.btnDeleteClc)
        val btnInspect: View = view.findViewById(R.id.btnInspectClc)
        val btnImportRun: View = view.findViewById(R.id.btnImportRunClc)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_clc_container, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val entry = items[position]
        holder.txtName.text = entry.relPath
        holder.txtMeta.text = holder.itemView.context.getString(
            R.string.file_meta_format,
            formatSize(entry.file.length()),
            dateFormat.format(Date(entry.file.lastModified()))
        )
        holder.btnDelete.setOnClickListener { onDelete(entry) }
        holder.btnInspect.setOnClickListener { onInspect(entry) }
        holder.btnImportRun.setOnClickListener { onImportAndRun(entry) }
    }

    override fun getItemCount(): Int = items.size

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> "${"%.1f".format(bytes / (1024.0 * 1024.0))} MB"
    }
}
