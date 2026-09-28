package com.dlof.rinlang.store

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup
import java.io.File

/**
 * تعديل مكتبة منشورة (للناشر فقط): الاسم/الإصدار/الوصف/التصنيف، README.md (بالمحرر أو باستبداله
 * من ملف)، وإدارة ملفات الحزمة كاملةً — تصفّح المجلدات، إنشاء مجلدات وملفات، إضافة ملفات، إعادة
 * تسمية/حذف، وفتح أي ملف نصي في محرر الكود الأصلي ([PackageFileEditorActivity]).
 *
 * تُفكّ الحزمة إلى مساحة عمل مؤقتة ([PackageWorkspace]) وتُعدَّل محلياً، ولا يُرفع شيء إلى المتجر
 * إلا عند «حفظ ونشر التعديلات» — فيُعاد تجميع zip ويُحدَّث سجل الحزمة نفسه عبر
 * [PackageRepository.updatePackage] (فتبقى الإعجابات/التقييمات/التنزيلات كما هي).
 */
class EditPackageActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_PACKAGE_ID = "extra_edit_package_id"

        fun start(context: Context, packageId: String) {
            context.startActivity(
                Intent(context, EditPackageActivity::class.java).putExtra(EXTRA_PACKAGE_ID, packageId)
            )
        }
    }

    private lateinit var pkg: RinPackage
    private var workspace: PackageWorkspace? = null
    private var currentPath: String = ""
    /** true بعد أي تغيير على ملفات الحزمة (إنشاء/حذف/إضافة/تعديل...). */
    private var filesDirty = false
    private var selectedCategory: String = ""
    private var ready = false
    private var saving = false

    private lateinit var edtName: EditText
    private lateinit var edtVersion: EditText
    private lateinit var edtDescription: EditText
    private lateinit var chipGroup: ChipGroup
    private lateinit var containerFiles: LinearLayout
    private lateinit var containerBreadcrumb: LinearLayout
    private lateinit var txtEmpty: TextView
    private lateinit var btnSave: Button
    private lateinit var progress: ProgressBar

    private val pickReadmeLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) replaceReadmeFrom(uri)
        }

    private val pickFilesLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isNotEmpty()) importFiles(uris)
        }

    private val fileEditorLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == RESULT_OK) filesDirty = true
            if (ready) renderFiles()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val uid = AuthRepository.currentUid()
        val packageId = intent.getStringExtra(EXTRA_PACKAGE_ID)
        if (uid == null || packageId.isNullOrBlank()) { finish(); return }

        setContentView(R.layout.activity_edit_package)
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.edit_package_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { onBackPressedDispatcher.onBackPressed() }

        edtName = findViewById(R.id.edtEditName)
        edtVersion = findViewById(R.id.edtEditVersion)
        edtDescription = findViewById(R.id.edtEditDescription)
        chipGroup = findViewById(R.id.chipGroupEditCategory)
        containerFiles = findViewById(R.id.containerEditFiles)
        containerBreadcrumb = findViewById(R.id.containerEditBreadcrumb)
        txtEmpty = findViewById(R.id.txtEditFilesEmpty)
        btnSave = findViewById(R.id.btnSaveChanges)
        progress = findViewById(R.id.progressEditPackage)

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                when {
                    saving -> Unit
                    currentPath.isNotEmpty() -> {
                        currentPath = currentPath.substringBeforeLast('/', "")
                        renderFiles()
                    }
                    hasChanges() -> confirmDiscard { leave() }
                    else -> leave()
                }
            }
        })

        // كل الأزرار معطّلة حتى تجهز مساحة العمل.
        setBusy(true)
        if (!isOnline()) { showOfflineOverlay(); finish(); return }

        PackageRepository.fetchPackage(packageId) { fetched ->
            if (fetched == null || fetched.publisherUid != uid) {
                Toast.makeText(this, R.string.edit_package_load_failed, Toast.LENGTH_LONG).show()
                finish()
                return@fetchPackage
            }
            pkg = fetched
            // فكّ zip وترميز base64 لملفات كبيرة (حتى عدة ميجابايت) لا يجوز على خيط الواجهة.
            Thread {
                try {
                    val ws = PackageWorkspace.create(applicationContext, fetched)
                    runOnUiThread {
                        if (isFinishing || isDestroyed) return@runOnUiThread
                        workspace = ws
                        initUi()
                    }
                } catch (t: Throwable) {
                    runOnUiThread {
                        Toast.makeText(this, t.message ?: getString(R.string.edit_package_load_failed), Toast.LENGTH_LONG).show()
                        finish()
                    }
                }
            }.start()
        }
    }

    // ------------------------------------------------------------------ تهيئة الواجهة

    private fun initUi() {
        edtName.setText(pkg.name)
        edtVersion.setText(pkg.version)
        edtDescription.setText(pkg.description)

        selectedCategory = pkg.category.ifBlank { PublishPackageActivity.CATEGORIES.first() }
        val categories = (PublishPackageActivity.CATEGORIES + selectedCategory).distinct()
        categories.forEach { category ->
            val chip = Chip(this).apply {
                text = category
                isCheckable = true
                isChecked = category == selectedCategory
                setOnClickListener {
                    selectedCategory = category
                    for (i in 0 until chipGroup.childCount) (chipGroup.getChildAt(i) as? Chip)?.isChecked = false
                    isChecked = true
                }
            }
            chipGroup.addView(chip)
        }

        findViewById<View>(R.id.btnEditReadme).setOnClickListener { openReadmeInEditor() }
        findViewById<View>(R.id.btnReplaceReadme).setOnClickListener {
            pickReadmeLauncher.launch(arrayOf("text/markdown", "text/plain", "text/*"))
        }
        findViewById<View>(R.id.btnNewFolder).setOnClickListener {
            promptName(R.string.edit_package_folder_name_title, "") { name -> createFolder(name) }
        }
        findViewById<View>(R.id.btnNewFile).setOnClickListener {
            promptName(R.string.edit_package_file_name_title, "") { name -> createFile(name) }
        }
        findViewById<View>(R.id.btnAddFiles).setOnClickListener { pickFilesLauncher.launch(arrayOf("*/*")) }
        btnSave.setOnClickListener { onSaveClicked() }

        ready = true
        setBusy(false)
        renderFiles()
    }

    private fun setBusy(busy: Boolean) {
        progress.visibility = if (busy) View.VISIBLE else View.GONE
        btnSave.isEnabled = !busy
        listOf(R.id.btnEditReadme, R.id.btnReplaceReadme, R.id.btnNewFolder, R.id.btnNewFile, R.id.btnAddFiles)
            .forEach { findViewById<View>(it).isEnabled = !busy }
    }

    private fun hasChanges(): Boolean {
        if (!ready) return false
        return filesDirty ||
            edtName.text.toString().trim() != pkg.name ||
            edtVersion.text.toString().trim() != pkg.version ||
            edtDescription.text.toString().trim() != pkg.description ||
            selectedCategory != pkg.category.ifBlank { PublishPackageActivity.CATEGORIES.first() }
    }

    private fun confirmDiscard(onDiscard: () -> Unit) {
        AlertDialog.Builder(this)
            .setTitle(R.string.edit_package_discard_title)
            .setMessage(R.string.edit_package_discard_message)
            .setPositiveButton(R.string.edit_package_discard) { _, _ -> onDiscard() }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun leave() {
        workspace?.destroy()
        finish()
    }

    // ------------------------------------------------------------------ مستكشف الملفات

    private fun renderFiles() {
        val ws = workspace ?: return
        renderBreadcrumb()
        containerFiles.removeAllViews()
        val nodes = ws.list(currentPath)
        txtEmpty.visibility = if (nodes.isEmpty()) View.VISIBLE else View.GONE
        containerFiles.visibility = if (nodes.isEmpty()) View.GONE else View.VISIBLE

        val inflater = LayoutInflater.from(this)
        for (node in nodes) {
            val row = inflater.inflate(R.layout.item_edit_file_row, containerFiles, false)
            val icon = row.findViewById<ImageView>(R.id.imgEditRowIcon)
            val meta = row.findViewById<TextView>(R.id.txtEditRowMeta)
            row.findViewById<TextView>(R.id.txtEditRowName).text = node.name

            if (node.isDir) {
                icon.setImageResource(R.drawable.ic_folder_solid)
                icon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.rin_star_gold))
                meta.text = getString(R.string.edit_package_folder_meta, node.fileCount)
                row.setOnClickListener { currentPath = node.relPath; renderFiles() }
            } else {
                icon.setImageResource(PackagingUtils.iconResFor(node.name))
                icon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(PackagingUtils.iconColorResFor(node.name)))
                meta.text = getString(R.string.edit_package_file_meta, formatSize(node.sizeBytes))
                row.setOnClickListener { openFile(node.relPath) }
            }
            row.findViewById<View>(R.id.btnEditRowMore).setOnClickListener { showRowMenu(it, node) }
            containerFiles.addView(row)
            if (node !== nodes.last()) containerFiles.addView(divider())
        }
    }

    private fun divider(): View = View(this).apply {
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1)
            .apply { marginStart = dp(48f) }
        setBackgroundColor(getColor(R.color.rin_divider))
    }

    private fun renderBreadcrumb() {
        containerBreadcrumb.removeAllViews()
        val parts = currentPath.split("/").filter { it.isNotEmpty() }
        addCrumb(pkg.name, isCurrent = parts.isEmpty()) { currentPath = ""; renderFiles() }
        for (i in parts.indices) {
            containerBreadcrumb.addView(TextView(this).apply {
                text = "/"
                textSize = 12.5f
                setTextColor(getColor(R.color.rin_editor_hint))
                setPadding(dp(2f), 0, dp(2f), 0)
            })
            val path = parts.subList(0, i + 1).joinToString("/")
            addCrumb(parts[i], isCurrent = i == parts.lastIndex) { currentPath = path; renderFiles() }
        }
    }

    private fun addCrumb(label: String, isCurrent: Boolean, onTap: () -> Unit) {
        containerBreadcrumb.addView(TextView(this).apply {
            text = label
            textSize = 12.5f
            typeface = android.graphics.Typeface.MONOSPACE
            if (isCurrent) {
                setTextColor(getColor(R.color.rin_on_toolbar))
                setTypeface(typeface, android.graphics.Typeface.BOLD)
            } else {
                setTextColor(getColor(R.color.rin_accent))
                setPadding(dp(4f), dp(4f), dp(4f), dp(4f))
                setOnClickListener { onTap() }
            }
        })
    }

    private fun showRowMenu(anchor: View, node: WorkspaceNode) {
        val menu = PopupMenu(this, anchor)
        if (!node.isDir) menu.menu.add(0, 1, 0, R.string.edit_package_open)
        menu.menu.add(0, 2, 1, R.string.edit_package_rename)
        menu.menu.add(0, 3, 2, R.string.edit_package_delete)
        menu.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> openFile(node.relPath)
                2 -> promptName(R.string.edit_package_rename_title, node.name) { newName -> renameNode(node, newName) }
                3 -> confirmDelete(node)
            }
            true
        }
        menu.show()
    }

    private fun openFile(relPath: String) {
        val intent = Intent(this, PackageFileEditorActivity::class.java)
            .putExtra(PackageFileEditorActivity.EXTRA_PACKAGE_ID, pkg.id)
            .putExtra(PackageFileEditorActivity.EXTRA_REL_PATH, relPath)
        fileEditorLauncher.launch(intent)
    }

    // ------------------------------------------------------------------ عمليات الملفات

    /** مربع حوار بحقل نصي واحد؛ يستدعي [onOk] بالاسم بعد قصّه. */
    private fun promptName(titleRes: Int, initial: String, onOk: (String) -> Unit) {
        val input = EditText(this).apply {
            setText(initial)
            hint = getString(R.string.edit_package_name_hint)
            setSingleLine(true)
            setSelection(text.length)
            typeface = android.graphics.Typeface.MONOSPACE
        }
        val holder = LinearLayout(this).apply {
            setPadding(dp(20f), dp(8f), dp(20f), 0)
            addView(input, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        }
        val dialog = AlertDialog.Builder(this)
            .setTitle(titleRes)
            .setView(holder)
            .setPositiveButton(R.string.edit_package_create) { _, _ -> onOk(input.text.toString().trim()) }
            .setNegativeButton(R.string.cancel, null)
            .create()
        dialog.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE)
        dialog.show()
    }

    private fun createFolder(name: String) {
        val err = workspace?.createFolder(currentPath, name)
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        filesDirty = true
        renderFiles()
    }

    private fun createFile(name: String) {
        val ws = workspace ?: return
        val err = ws.createFile(currentPath, name)
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        filesDirty = true
        renderFiles()
        // فتحه فوراً بالمحرر: الغرض المعتاد من إنشاء ملف جديد هو كتابة محتواه.
        openFile((if (currentPath.isEmpty()) "" else "$currentPath/") + name.trim())
    }

    private fun renameNode(node: WorkspaceNode, newName: String) {
        if (newName == node.name) return
        val err = workspace?.rename(node.relPath, newName)
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        filesDirty = true
        renderFiles()
    }

    private fun confirmDelete(node: WorkspaceNode) {
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.edit_package_delete_confirm, node.name))
            .setPositiveButton(R.string.edit_package_delete) { _, _ ->
                workspace?.delete(node.relPath)
                filesDirty = true
                renderFiles()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun importFiles(uris: List<Uri>) {
        val ws = workspace ?: return
        val target = currentPath
        setBusy(true)
        Thread {
            var added = 0
            var lastError: String? = null
            for (uri in uris) {
                try { ws.importUri(applicationContext, target, uri); added++ }
                catch (t: Throwable) { lastError = t.message }
            }
            runOnUiThread {
                setBusy(false)
                if (added > 0) {
                    filesDirty = true
                    Toast.makeText(this, getString(R.string.edit_package_files_added, added), Toast.LENGTH_SHORT).show()
                }
                lastError?.let { Toast.makeText(this, it, Toast.LENGTH_LONG).show() }
                renderFiles()
            }
        }.start()
    }

    // ------------------------------------------------------------------ README

    /** مسار README الفعلي (بأي حجم أحرف) في جذر الحزمة، أو null. */
    private fun findReadmePath(): String? =
        workspace?.list("")?.filter { !it.isDir && DocumentationContainer.isReadmeName(it.name) }
            ?.let { found -> (found.firstOrNull { DocumentationContainer.isDocumentationFile(it.name) } ?: found.firstOrNull())?.relPath }

    private fun openReadmeInEditor() {
        val ws = workspace ?: return
        var path = findReadmePath()
        if (path == null) {
            ws.createFile("", "README.md", "# ${edtName.text.toString().trim().ifBlank { pkg.name }}\n\n")
            path = "README.md"
            filesDirty = true
            Toast.makeText(this, R.string.edit_package_readme_created, Toast.LENGTH_SHORT).show()
            renderFiles()
        }
        openFile(path)
    }

    private fun replaceReadmeFrom(uri: Uri) {
        val ws = workspace ?: return
        try {
            val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return
            if (bytes.size > PackageWorkspace.MAX_EDITABLE_BYTES * 4) {
                Toast.makeText(this, R.string.edit_package_not_editable, Toast.LENGTH_LONG).show(); return
            }
            ws.writeText(findReadmePath() ?: "README.md", bytes.toString(Charsets.UTF_8))
            filesDirty = true
            Toast.makeText(this, R.string.edit_package_replace_readme_done, Toast.LENGTH_SHORT).show()
            renderFiles()
        } catch (t: Throwable) {
            Toast.makeText(this, t.message ?: getString(R.string.edit_package_open_failed), Toast.LENGTH_LONG).show()
        }
    }

    // ------------------------------------------------------------------ الحفظ والنشر

    private fun onSaveClicked() {
        val ws = workspace ?: return
        if (saving) return
        if (!hasChanges()) { Toast.makeText(this, R.string.edit_package_no_changes, Toast.LENGTH_SHORT).show(); return }
        if (!isOnline()) { showOfflineOverlay(); return }

        val name = edtName.text.toString().trim()
        val version = edtVersion.text.toString().trim().ifBlank { pkg.version }
        val description = edtDescription.text.toString().trim()

        // نفس سياسات النشر الأصلية (محلية أولاً، قبل أي اتصال).
        for (check in listOf(
            PublishPolicy.validateName(name),
            PublishPolicy.validateDescription(description),
            PublishPolicy.validateVersionFormat(version)
        )) {
            if (check is PublishPolicy.PolicyResult.Denied) {
                Toast.makeText(this, check.message, Toast.LENGTH_LONG).show(); return
            }
        }
        if (filesDirty && ws.libraryFiles().isEmpty()) {
            Toast.makeText(this, R.string.edit_package_need_library, Toast.LENGTH_LONG).show(); return
        }

        saving = true
        setBusy(true)

        val nameChanged = name != pkg.name
        val versionChanged = version != pkg.version

        fun fail(message: String) {
            saving = false
            setBusy(false)
            Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        }

        fun proceed() = buildAndUpload(name, version, description)

        fun checkVersionThen() {
            if (!versionChanged) { proceed(); return }
            // الإصدار الجديد يجب أن يكون أحدث من كل الإصدارات المنشورة بهذا الاسم (شاملاً الحالي).
            PackageRepository.fetchExistingVersions(name) { existing ->
                val progression = PublishPolicy.validateVersionProgression(version, existing)
                if (progression is PublishPolicy.PolicyResult.Denied) fail(progression.message) else proceed()
            }
        }

        if (nameChanged) {
            PackageRepository.isNameAvailable(name, pkg.id) { available ->
                if (!available) fail(getString(R.string.error_package_name_taken)) else checkVersionThen()
            }
        } else {
            checkVersionThen()
        }
    }

    private fun buildAndUpload(name: String, version: String, description: String) {
        val ws = workspace ?: return
        val uid = AuthRepository.currentUid() ?: return

        fun fail(message: String) {
            saving = false
            setBusy(false)
            Toast.makeText(this, message, Toast.LENGTH_LONG).show()
        }

        // التجميع والترميز خارج خيط الواجهة.
        Thread {
            try {
                val updates = HashMap<String, Any?>()
                updates["name"] = name
                updates["version"] = version
                updates["description"] = description
                updates["category"] = selectedCategory.ifBlank { "عام" }
                updates["fileName"] = "$name.${PackagingUtils.PACKAGE_EXTENSION}"

                if (filesDirty) {
                    val zip = ws.pack(File(File(cacheDir, "store_edit_pack").apply { mkdirs() }, "${pkg.id}.zip"))
                    val sizeCheck = PublishPolicy.validateSize(zip.length())
                    if (sizeCheck is PublishPolicy.PolicyResult.Denied) {
                        runOnUiThread { fail(sizeCheck.message) }
                        return@Thread
                    }
                    val base64 = PackagingUtils.encodeFileToBase64(zip)
                    zip.delete()
                    updates["base64Data"] = base64
                    updates["sizeBytes"] = base64.length * 3L / 4
                }

                runOnUiThread {
                    PackageRepository.updatePackage(pkg.id, uid, updates) { success, error ->
                        if (success) {
                            Toast.makeText(this, R.string.edit_package_saved, Toast.LENGTH_SHORT).show()
                            setResult(RESULT_OK)
                            leave()
                        } else {
                            fail(error ?: getString(R.string.edit_package_save_failed))
                        }
                    }
                }
            } catch (t: Throwable) {
                runOnUiThread { fail(t.message ?: getString(R.string.edit_package_save_failed)) }
            }
        }.start()
    }

    // ------------------------------------------------------------------ أدوات

    private fun dp(value: Float): Int = (value * resources.displayMetrics.density).toInt()

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> String.format("%.1f MB", bytes / (1024.0 * 1024.0))
    }
}
