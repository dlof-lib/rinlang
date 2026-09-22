package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.webkit.MimeTypeMap
import android.widget.EditText
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "الملفات" الخاصة بمشروع واحد: تعرض مجلدات وملفات المشروع مستوى بمستوى (يمكن التنقل داخل
 * مجلد فرعي والعودة منه)، وتتيح:
 *  - "رفع ملف" (upload): استيراد أي ملف موجود بالفعل على الجهاز (تخزين محلي، Google Drive، إلخ)
 *    عبر SAF بامتداده الأصلي كما هو، فيُنسخ داخل المجلد الحالي.
 *  - "رفع وسائط" (upload media): اختيار صورة أو فيديو مباشرة من معرض الجهاز.
 *  - إنشاء ملف .rin جديد فارغ بالاسم المطلوب داخل المجلد الحالي. كتابة اسم يحوي "/" (مثل
 *    "ui/home.rin") تُنشئ المجلدات الفرعية اللازمة تلقائياً ثم الملف بداخلها.
 *  - إنشاء مجلد فرعي جديد بالاسم المطلوب (زر مخصّص منفصل).
 *  - إعادة تسمية أي ملف أو مجلد (زر قلم على كل صف).
 *  - فتح ملف، أو الدخول إلى مجلد فرعي.
 *  - حذف ملف أو مجلد (مع كل محتوياته).
 */
class FilesActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
    }

    private lateinit var project: Project
    private lateinit var rvFiles: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var txtEmptyMessage: TextView
    private lateinit var adapter: FilesAdapter

    /** المسار النسبي الحالي من جذر المشروع (فواصل "/")، فارغ يعني أننا في جذر المشروع. */
    private var currentRelDir: String = ""
    /** نص البحث الحالي في شريط "🔍 بحث في ملفات المشروع"؛ فارغ = تصفّح عادي مستوى بمستوى. */
    private var searchQuery: String = ""

    private val importFileLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) importFile(uri)
        }

    private val importZipLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) importZip(uri)
        }

    private val importMediaLauncher =
        registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
            if (uri != null) importFile(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_files)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
            ?: run { finish(); return }
        val existing = ProjectAlbumManager.findProject(this, projectName)
            ?: run {
                Toast.makeText(this, R.string.project_not_found, Toast.LENGTH_SHORT).show()
                finish(); return
            }
        project = existing
        title = project.name

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.files_screen_title)
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            visibility = View.VISIBLE
            // النقر على مسار "المشروع / مجلد1 / مجلد2" يعود مباشرة إلى جذر المشروع دفعة واحدة،
            // بدل الاضطرار للضغط على "رجوع" مرة لكل مستوى مجلد متداخل.
            setOnClickListener {
                if (currentRelDir.isNotBlank()) {
                    currentRelDir = ""
                    refresh()
                }
            }
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { handleBack() }

        rvFiles = findViewById(R.id.rvFiles)
        txtEmpty = findViewById(R.id.txtEmptyFiles)
        txtEmptyMessage = findViewById(R.id.txtEmptyFilesMessage)
        val fabAddFile: View = findViewById(R.id.fabAddFile)
        val fabNewFolder: View = findViewById(R.id.fabNewFolder)
        val fabUploadFile: View = findViewById(R.id.fabUploadFile)
        val fabUploadMedia: View = findViewById(R.id.fabUploadMedia)
        val fabZipTools: View = findViewById(R.id.fabZipTools)

        adapter = FilesAdapter(
            onOpenFile = { file -> handleOpen(file) },
            onOpenFolder = { folder -> enterFolder(folder) },
            onRenameFile = { file -> showRenameFileDialog(file) },
            onRenameFolder = { folder -> showRenameFolderDialog(folder) },
            onDeleteFile = { file -> showDeleteFileConfirm(file) },
            onDeleteFolder = { folder -> showDeleteFolderConfirm(folder) },
            onMoreFile = { file, anchor -> showMoreFileActions(file, anchor) }
        )
        rvFiles.layoutManager = LinearLayoutManager(this)
        rvFiles.adapter = adapter

        findViewById<EditText>(R.id.inputSearchFiles).addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                searchQuery = s?.toString().orEmpty()
                refresh()
            }
        })

        fabAddFile.setOnClickListener { showCreateFileDialog() }
        fabNewFolder.setOnClickListener { showCreateFolderDialog() }
        fabUploadFile.setOnClickListener {
            importFileLauncher.launch(
                arrayOf(
                    "text/plain", "application/octet-stream",
                    "image/*", "video/*", "audio/*", "font/*", "application/pdf",
                    "*/*"
                )
            )
        }
        fabUploadMedia.setOnClickListener {
            importMediaLauncher.launch(
                PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)
            )
        }
        fabZipTools.setOnClickListener { showZipToolsDialog() }
    }

    private fun handleBack() {
        if (currentRelDir.isBlank()) {
            finish()
        } else {
            currentRelDir = currentRelDir.substringBeforeLast('/', "")
            refresh()
        }
    }

    override fun onBackPressed() {
        if (currentRelDir.isBlank()) {
            super.onBackPressed()
        } else {
            handleBack()
        }
    }

    private fun enterFolder(folder: RinFolder) {
        currentRelDir = folder.relPath
        refresh()
    }

    private fun handleOpen(file: RinFile) {
        when {
            ProjectManager.isImageFile(file.name) || ProjectManager.isVideoFile(file.name) ->
                openInMediaPreview(file)
            ProjectManager.isBinaryFile(file.name) -> openExternally(file)
            else -> openInEditor(file)
        }
    }

    private fun openInMediaPreview(file: RinFile) {
        val intent = Intent(this, MediaPreviewActivity::class.java)
        intent.putExtra(MediaPreviewActivity.EXTRA_FILE_PATH, file.file.absolutePath)
        intent.putExtra(MediaPreviewActivity.EXTRA_FILE_NAME, file.name)
        startActivity(intent)
    }

    private fun openExternally(file: RinFile) {
        try {
            val uri: Uri = FileProvider.getUriForFile(this, "$packageName.fileprovider", file.file)
            val ext = file.name.substringAfterLast('.', "")
            val mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "*/*"
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, mime)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            startActivity(intent)
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(this, R.string.no_app_to_open_file, Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun showZipToolsDialog() {
        val options = arrayOf(
            getString(R.string.zip_upload_extract),
            getString(R.string.export_project_action)
        )
        AlertDialog.Builder(this)
            .setTitle(R.string.zip_tools_title)
            .setItems(options) { _, which ->
                when (which) {
                    0 -> importZipLauncher.launch(
                        arrayOf("application/zip", "application/x-zip-compressed", "*/*")
                    )
                    1 -> downloadProjectAsZip()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun importZip(uri: Uri) {
        try {
            val count = ProjectManager.importZipFromUri(this, project, uri, currentRelDir)
            refresh()
            Toast.makeText(this, getString(R.string.zip_extracted_toast, count), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.zip_extract_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun downloadProjectAsZip() {
        val mainHandler = Handler(Looper.getMainLooper())
        val progressDialog = RinDownloadProgressDialog(this, "${project.name}.rinproj")
        progressDialog.show()

        Thread {
            try {
                val zipFile = ProjectManager.exportProject(this, project)
                val artifact = RinArtifact(
                    kind = ArtifactKind.ARCHIVE_ZIP,
                    relPath = zipFile.name,
                    absoluteFile = zipFile,
                    sizeBytes = zipFile.length()
                )
                mainHandler.post {
                    RinDownloadManager.downloadToPublicDownloads(
                        activity = this,
                        artifact = artifact,
                        onProgress = { copied, total -> progressDialog.updateProgress(copied, total) },
                        onDone = { uri, error ->
                            if (error != null || uri == null) {
                                progressDialog.finishError(
                                    getString(R.string.download_error_toast, error?.message ?: "?")
                                )
                                mainHandler.postDelayed({ progressDialog.dismiss() }, 1400)
                                return@downloadToPublicDownloads
                            }
                            progressDialog.finishSuccess(getString(R.string.download_status_done))
                            Toast.makeText(
                                this,
                                getString(R.string.download_done_toast, artifact.displayName),
                                Toast.LENGTH_SHORT
                            ).show()
                            mainHandler.postDelayed({ progressDialog.dismiss() }, 800)
                        }
                    )
                }
            } catch (t: Throwable) {
                mainHandler.post {
                    progressDialog.finishError(getString(R.string.download_error_toast, t.message ?: "?"))
                    mainHandler.postDelayed({ progressDialog.dismiss() }, 1400)
                }
            }
        }.start()
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    /** يزداد مع كل refresh() جديد؛ يمنع نتيجة فحص أخطاء خلفي متأخرة (لمجلد/بحث سابق غادره
     *  المستخدم فعلاً) من الكتابة فوق نتائج القائمة الحالية. */
    private var scanToken: Int = 0

    private fun refresh() {
        if (searchQuery.isNotBlank()) {
            val results = ProjectManager.searchFiles(project, searchQuery)
            adapter.submit(emptyList(), results, searchMode = true)
            adapter.setDirtyPaths(EditorDirtyState.dirtyPaths(project.name))
            txtEmptyMessage.setText(R.string.search_no_results)
            txtEmpty.visibility = if (results.isEmpty()) View.VISIBLE else View.GONE
            scanForErrors(results)
            return
        }
        txtEmptyMessage.setText(R.string.no_files_yet)
        val (folders, files) = ProjectManager.listEntries(project, currentRelDir)
        adapter.submit(folders, files)
        adapter.setDirtyPaths(EditorDirtyState.dirtyPaths(project.name))
        txtEmpty.visibility = if (folders.isEmpty() && files.isEmpty()) View.VISIBLE else View.GONE
        scanForErrors(files)

        findViewById<TextView>(R.id.txtToolbarSubtitle).text =
            if (currentRelDir.isBlank()) project.name else "${project.name} / $currentRelDir"
    }

    /**
     * يفحص كل ملف نصّي (لا صور/فيديو/صوت...) في [files] بخيط خلفي عبر محرّك rin::Lexer/Parser
     * الحقيقي نفسه (RinNativeEditor.getDiagnostics — نفس ما يرسم الخط الأحمر المتعرّج في المحرر
     * الفعلي، هنا على نسخة معزولة مؤقتة من كل ملف بلا أي واجهة)، ويعلّم "!" فقط لما فيه تشخيص
     * ERROR فعلي — لا مؤشر وهمي أو تخمين شكل الامتداد. تُهمَل نتيجة وصلت بعد أن غادر المستخدم
     * هذه القائمة (عبر [scanToken]).
     */
    private fun scanForErrors(files: List<RinFile>) {
        val myToken = ++scanToken
        val candidates = files.filter { !ProjectManager.isBinaryFile(it.name) }
        if (candidates.isEmpty()) {
            adapter.setErrorPaths(emptySet())
            return
        }
        val mainHandler = Handler(Looper.getMainLooper())
        Thread {
            val found = mutableSetOf<String>()
            for (f in candidates) {
                try {
                    val probe = RinNativeEditor()
                    probe.setText(f.file.readText())
                    val hasError = probe.getDiagnostics()
                        .any { it.severity == RinNativeEditor.DiagnosticSeverity.ERROR }
                    probe.destroy()
                    if (hasError) found += f.relPath
                } catch (_: Throwable) {
                    // ملف لا يُقرأ كنص أو فشل الفحص لأي سبب: يُستبعَد بصمت، لا يظهر "!" خطأً
                    // (تفضيل عدم التنبيه الكاذب على تنبيه زائف).
                }
            }
            mainHandler.post {
                if (myToken == scanToken) adapter.setErrorPaths(found)
            }
        }.start()
    }

    private fun showMoreFileActions(file: RinFile, anchor: View) {
        val popup = PopupMenu(this, anchor)
        popup.menu.add(0, 1, 0, R.string.action_duplicate)
        popup.menu.add(0, 2, 1, R.string.action_move)
        popup.menu.add(0, 3, 2, R.string.action_copy_path)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> { duplicateFile(file); true }
                2 -> { showMoveFileDialog(file); true }
                3 -> { copyFilePath(file); true }
                else -> false
            }
        }
        popup.show()
    }

    private fun duplicateFile(file: RinFile) {
        try {
            val copy = ProjectManager.duplicateFile(file)
            refresh()
            Toast.makeText(this, getString(R.string.file_duplicated_toast, copy.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun copyFilePath(file: RinFile) {
        val clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText("path", file.relPath))
        Toast.makeText(this, R.string.path_copied_toast, Toast.LENGTH_SHORT).show()
    }

    /** يجمع كل مجلدات المشروع (بكل الأعماق) كوجهات نقل محتملة، ما عدا مجلد الملف الحالي نفسه. */
    private fun showMoveFileDialog(file: RinFile) {
        val allFolders = mutableListOf<RinFolder>()
        fun walk(relDir: String) {
            val (folders, _) = ProjectManager.listEntries(project, relDir)
            for (f in folders) {
                allFolders += f
                walk(f.relPath)
            }
        }
        walk("")
        val currentDir = file.relPath.substringBeforeLast('/', "")
        val candidateDirs = (listOf("") + allFolders.map { it.relPath }).filter { it != currentDir }
        if (candidateDirs.isEmpty()) {
            Toast.makeText(this, R.string.move_no_folders, Toast.LENGTH_SHORT).show()
            return
        }
        val labels = candidateDirs.map { if (it.isEmpty()) getString(R.string.move_root_option) else it }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.move_dialog_title, file.name))
            .setItems(labels) { _, which ->
                val dest = candidateDirs[which]
                try {
                    val moved = ProjectManager.moveFile(project, file, dest)
                    refresh()
                    Toast.makeText(this, getString(R.string.file_moved_toast, moved.relPath), Toast.LENGTH_SHORT).show()
                } catch (t: Throwable) {
                    Toast.makeText(this, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun importFile(uri: Uri) {
        try {
            val file = ProjectManager.importFileFromUri(this, project, uri, currentRelDir)
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
                try {
                    val file = ProjectManager.writeFile(project, currentRelDir, name, "")
                    refresh()
                    openInEditor(file)
                } catch (t: Throwable) {
                    Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showCreateFolderDialog() {
        val input = EditText(this)
        input.hint = getString(R.string.folder_name_hint)
        AlertDialog.Builder(this)
            .setTitle(R.string.new_folder_title)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                try {
                    ProjectManager.createFolder(project, currentRelDir, name)
                    refresh()
                } catch (t: Throwable) {
                    Toast.makeText(this, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showRenameFileDialog(file: RinFile) {
        val input = EditText(this)
        input.setText(file.name)
        input.setSelection(0, file.name.lastIndexOf('.').let { if (it > 0) it else file.name.length })
        AlertDialog.Builder(this)
            .setTitle(R.string.rename_file_title)
            .setView(input)
            .setPositiveButton(R.string.rename) { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isEmpty() || newName == file.name) return@setPositiveButton
                try {
                    ProjectManager.renameFile(project, file, newName)
                    refresh()
                    Toast.makeText(this, getString(R.string.file_renamed_toast, newName), Toast.LENGTH_SHORT).show()
                } catch (t: Throwable) {
                    Toast.makeText(this, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showRenameFolderDialog(folder: RinFolder) {
        val input = EditText(this)
        input.setText(folder.name)
        AlertDialog.Builder(this)
            .setTitle(R.string.rename_folder_title)
            .setView(input)
            .setPositiveButton(R.string.rename) { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isEmpty() || newName == folder.name) return@setPositiveButton
                try {
                    ProjectManager.renameFolder(folder, newName)
                    refresh()
                    Toast.makeText(this, getString(R.string.folder_renamed_toast, newName), Toast.LENGTH_SHORT).show()
                } catch (t: Throwable) {
                    Toast.makeText(this, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showDeleteFileConfirm(file: RinFile) {
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

    private fun showDeleteFolderConfirm(folder: RinFolder) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_folder_title)
            .setMessage(getString(R.string.delete_folder_confirm, folder.name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectManager.deleteFolder(folder)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun openInEditor(file: RinFile) {
        val intent = Intent(this, MainActivity::class.java)
        intent.putExtra(MainActivity.EXTRA_PROJECT_NAME, project.name)
        // نمرّر المسار النسبي الكامل (relPath) لا الاسم المجرّد وحده، وإلا فشل فتح أي ملف
        // داخل مجلد فرعي (أو فُتح ملف آخر بالخطأ بنفس الاسم في الجذر).
        intent.putExtra(MainActivity.EXTRA_FILE_NAME, file.relPath)
        startActivity(intent)
    }
}

private sealed class FileRow {
    data class FolderRow(val folder: RinFolder) : FileRow()
    data class FileRowItem(val file: RinFile) : FileRow()
}

private class FilesAdapter(
    val onOpenFile: (RinFile) -> Unit,
    val onOpenFolder: (RinFolder) -> Unit,
    val onRenameFile: (RinFile) -> Unit,
    val onRenameFolder: (RinFolder) -> Unit,
    val onDeleteFile: (RinFile) -> Unit,
    val onDeleteFolder: (RinFolder) -> Unit,
    val onMoreFile: (RinFile, View) -> Unit
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    private companion object {
        const val TYPE_FOLDER = 0
        const val TYPE_FILE = 1
    }

    private var items: List<FileRow> = emptyList()
    /** أثناء البحث في المشروع كله نعرض المسار النسبي الكامل مكان الحجم/التاريخ (تمييز ملفات
     *  متشابهة الاسم في مجلدات مختلفة)، وإلا فالتصفّح العادي مستوى بمستوى كالمعتاد. */
    private var searchMode: Boolean = false
    /** مسارات نسبية فيها خطأ صياغة حقيقي (rin::Lexer/Parser عبر FilesActivity.scanForErrors)
     *  ومسارات فيها تعديل غير محفوظ حقيقي (EditorDirtyState) — كلاهما يُحدَّث بعد الرسم الأول
     *  فور توفّر النتيجة (الخطأ يُفحَص بخيط خلفي)، فـ[notifyDataSetChanged] عند تحديثهما متوقَّع. */
    private var errorPaths: Set<String> = emptySet()
    private var dirtyPaths: Set<String> = emptySet()
    private val dateFormat = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)

    fun submit(folders: List<RinFolder>, files: List<RinFile>, searchMode: Boolean = false) {
        this.searchMode = searchMode
        items = folders.map { FileRow.FolderRow(it) } + files.map { FileRow.FileRowItem(it) }
        notifyDataSetChanged()
    }

    fun setErrorPaths(paths: Set<String>) {
        errorPaths = paths
        notifyDataSetChanged()
    }

    fun setDirtyPaths(paths: Set<String>) {
        dirtyPaths = paths
        notifyDataSetChanged()
    }

    class FolderVH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtFolderNameItem)
        val txtMeta: TextView = view.findViewById(R.id.txtFolderMeta)
        val btnRename: View = view.findViewById(R.id.btnRenameFolder)
        val btnDelete: View = view.findViewById(R.id.btnDeleteFolder)
    }

    class FileVH(view: View) : RecyclerView.ViewHolder(view) {
        val imgIcon: android.widget.ImageView = view.findViewById(R.id.imgFileIcon)
        val txtErrorBadge: View = view.findViewById(R.id.txtErrorBadge)
        val txtName: TextView = view.findViewById(R.id.txtFileNameItem)
        val txtDirtyDot: View = view.findViewById(R.id.txtDirtyDot)
        val txtMeta: TextView = view.findViewById(R.id.txtFileMeta)
        val btnMore: View = view.findViewById(R.id.btnMoreFile)
        val btnRename: View = view.findViewById(R.id.btnRenameFile)
        val btnDelete: View = view.findViewById(R.id.btnDeleteFile)
    }

    override fun getItemViewType(position: Int): Int = when (items[position]) {
        is FileRow.FolderRow -> TYPE_FOLDER
        is FileRow.FileRowItem -> TYPE_FILE
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        return if (viewType == TYPE_FOLDER) {
            FolderVH(LayoutInflater.from(parent.context).inflate(R.layout.item_folder, parent, false))
        } else {
            FileVH(LayoutInflater.from(parent.context).inflate(R.layout.item_file, parent, false))
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        when (val row = items[position]) {
            is FileRow.FolderRow -> {
                val folder = row.folder
                holder as FolderVH
                holder.txtName.text = folder.name
                holder.txtMeta.text = holder.itemView.context.getString(
                    R.string.folder_meta_format,
                    dateFormat.format(Date(folder.lastModified))
                )
                holder.itemView.setOnClickListener { onOpenFolder(folder) }
                holder.btnRename.setOnClickListener { onRenameFolder(folder) }
                holder.btnDelete.setOnClickListener { onDeleteFolder(folder) }
            }
            is FileRow.FileRowItem -> {
                val file = row.file
                holder as FileVH
                holder.txtName.text = file.name
                holder.txtMeta.text = if (searchMode) {
                    holder.itemView.context.getString(R.string.search_result_meta_format, file.relPath)
                } else {
                    holder.itemView.context.getString(
                        R.string.file_meta_format,
                        formatSize(file.sizeBytes),
                        dateFormat.format(Date(file.lastModified))
                    )
                }
                FileIconResolver.load(holder.imgIcon, file.file)
                holder.txtErrorBadge.visibility = if (file.relPath in errorPaths) View.VISIBLE else View.GONE
                holder.txtDirtyDot.visibility = if (file.relPath in dirtyPaths) View.VISIBLE else View.GONE
                holder.itemView.setOnClickListener { onOpenFile(file) }
                holder.btnMore.setOnClickListener { onMoreFile(file, holder.btnMore) }
                holder.btnRename.setOnClickListener { onRenameFile(file) }
                holder.btnDelete.setOnClickListener { onDeleteFile(file) }
            }
        }
    }

    override fun getItemCount(): Int = items.size

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> "${bytes / (1024 * 1024)} MB"
    }
}
