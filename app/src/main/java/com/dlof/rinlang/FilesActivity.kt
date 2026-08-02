package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.webkit.MimeTypeMap
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "الملفات" الخاصة بمشروع واحد: تعرض كل ملفات المشروع (ملفات .rin بالإضافة إلى أي ملف آخر
 * رُفع لاحقاً: صور، فيديو، صوت، خطوط، ملفات لغات برمجة أخرى...) داخل مجلده، وتتيح:
 *  - "رفع ملف" (upload): استيراد أي ملف موجود بالفعل على الجهاز (تخزين محلي، Google Drive، إلخ)
 *    عبر SAF بامتداده الأصلي كما هو (لم يعد يُجبَر على .rin)، فيُنسخ داخل المشروع.
 *  - "رفع وسائط" (upload media): اختيار صورة أو فيديو مباشرة من معرض الجهاز عبر منتقي الوسائط
 *    الحديث (Photo Picker)، وهو أسرع وأبسط من SAF لهذا الغرض تحديداً ولا يحتاج صلاحيات تخزين.
 *  - إنشاء ملف .rin جديد فارغ بالاسم المطلوب.
 *  - فتح ملف: .rin أو أي ملف نصّي آخر في المحرر ([MainActivity])، صورة/فيديو في معاينة داخلية
 *    ([MediaPreviewActivity])، وأي نوع ثنائي آخر (pdf/صوت/أرشيف...) بتطبيق خارجي مناسب.
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

    private val importZipLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) importZip(uri)
        }

    /** منتقي الوسائط الحديث (Photo Picker): صور أو فيديو مباشرة من المعرض، بلا أي صلاحية تخزين. */
    private val importMediaLauncher =
        registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
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
        val fabUploadMedia: View = findViewById(R.id.fabUploadMedia)
        val fabZipTools: View = findViewById(R.id.fabZipTools)

        adapter = FilesAdapter(
            onOpen = { file -> handleOpen(file) },
            onDelete = { file -> showDeleteConfirm(file) }
        )
        rvFiles.layoutManager = LinearLayoutManager(this)
        rvFiles.adapter = adapter

        fabAddFile.setOnClickListener { showCreateFileDialog() }
        fabUploadFile.setOnClickListener {
            // نقبل .rin/أي نص عادي، وأيضاً صراحةً صور/فيديو/صوت/خطوط وملفات لغات برمجية أخرى
            // (html/js/cpp/py/...) ومستندات (pdf)؛ "*/*" وحدها تكفي تقنياً لكن نذكر الأنواع
            // الشائعة صراحةً ليعرضها بعض منتقيات الملفات (SAF) كفئات مقترحة بدل قائمة واحدة مبهمة.
            // الملف يُحفَظ بامتداده الأصلي كما هو (لا يُجبَر على .rin بعد الآن).
            importFileLauncher.launch(
                arrayOf(
                    "text/plain", "application/octet-stream",
                    "image/*", "video/*", "audio/*", "font/*", "application/pdf",
                    "*/*"
                )
            )
        }
        // "رفع وسائط": منتقي صور/فيديو حديث (Photo Picker) مباشرة من المعرض، أسرع وأبسط
        // من SAF لهذا الغرض ولا يحتاج طلب أي صلاحية تخزين على أندرويد الحديث.
        fabUploadMedia.setOnClickListener {
            importMediaLauncher.launch(
                PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)
            )
        }
        fabZipTools.setOnClickListener { showZipToolsDialog() }
    }

    /** يوجّه فتح الملف حسب نوعه: .rin/نص عادي -> المحرر، صورة/فيديو -> معاينة داخلية، غير ذلك -> تطبيق خارجي. */
    private fun handleOpen(file: RinFile) {
        when {
            ProjectManager.isImageFile(file.name) || ProjectManager.isVideoFile(file.name) ->
                openInMediaPreview(file)
            ProjectManager.isBinaryFile(file.name) -> openExternally(file)
            else -> openInEditor(file)
        }
    }

    /** يفتح صورة أو فيديو في معاينة داخل التطبيق نفسه، بدون الحاجة لتطبيق خارجي. */
    private fun openInMediaPreview(file: RinFile) {
        val intent = Intent(this, MediaPreviewActivity::class.java)
        intent.putExtra(MediaPreviewActivity.EXTRA_FILE_PATH, file.file.absolutePath)
        intent.putExtra(MediaPreviewActivity.EXTRA_FILE_NAME, file.name)
        startActivity(intent)
    }

    /** يفتح ملفاً ثنائياً (pdf/صوت/أرشيف/خط...) بأي تطبيق خارجي مناسب مثبَّت على الجهاز عبر FileProvider. */
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

    /** يعرض خيارَي "رفع أرشيف ZIP وفك ضغطه" و"تنزيل المشروع كأرشيف ZIP" في حوار واحد. */
    private fun showZipToolsDialog() {
        val options = arrayOf(
            getString(R.string.zip_upload_extract),
            getString(R.string.zip_download_project)
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

    /** يفكّ ضغط أرشيف ZIP المختار داخل مجلد المشروع الحالي، ثم يحدّث القائمة. */
    private fun importZip(uri: Uri) {
        try {
            val count = ProjectManager.importZipFromUri(this, project, uri)
            refresh()
            Toast.makeText(this, getString(R.string.zip_extracted_toast, count), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.zip_extract_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    /** يضغط ملفات المشروع الحالي كأرشيف واحد وينزّله لمجلد التنزيلات العام على الجهاز. */
    private fun downloadProjectAsZip() {
        val mainHandler = Handler(Looper.getMainLooper())
        val progressDialog = RinDownloadProgressDialog(this, "${project.name}.zip")
        progressDialog.show()

        Thread {
            try {
                val zipFile = ProjectManager.exportProjectAsZip(this, project)
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
        val imgIcon: android.widget.ImageView = view.findViewById(R.id.imgFileIcon)
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
        FileIconResolver.load(holder.imgIcon, file.file)
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
