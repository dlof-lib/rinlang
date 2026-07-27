package com.dlof.rinlang

import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.text.Editable
import android.text.TextWatcher
import android.view.ContextThemeWrapper
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.R as MaterialR
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

    companion object {
        /** اسم مشروع (اختياري) جاء من شاشة الملفات/المشاريع؛ يحدّد basePath خاصاً بهذا المشروع. */
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** اسم ملف .rin (اختياري) داخل ذلك المشروع، يُفتَح تلقائياً في المحرر. */
        const val EXTRA_FILE_NAME = "extra_file_name"
        /** اسم مكتبة .og.rin (اختياري) داخل lib/ الخاصة بذلك المشروع، تُفتَح للتعديل في نفس المحرر. */
        const val EXTRA_LIBRARY_NAME = "extra_library_name"
    }

    /** المشروع الحالي إن جاء التطبيق من شاشة الملفات، وإلا null (وضع الملف الحر عبر SAF كما كان سابقاً). */
    private var currentProject: Project? = null
    private var currentProjectFile: RinFile? = null
    /** غير null فقط عندما يكون المحرر مفتوحاً على مكتبة lib/ *.og.rin بدل ملف .rin عادي. */
    private var currentProjectLibrary: RinLibrary? = null

    private lateinit var editCode: RinEditText
    private lateinit var txtLineNumbers: TextView
    private lateinit var txtEngineVersion: TextView
    private lateinit var txtFileName: TextView
    private lateinit var rvJobs: RecyclerView
    private lateinit var progressRunning: ProgressBar
    private lateinit var findBar: LinearLayout
    private lateinit var txtFind: EditText
    private lateinit var txtReplace: EditText
    private lateinit var txtFindCount: TextView
    private lateinit var scrollEditor: ScrollView

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

    /** تستقبل سطر @import الذي اختاره المستخدم من شاشة "المكتبات" وتُدرجه عند مكان المؤشر في الكود. */
    private val librariesLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val importStatement = result.data?.getStringExtra(LibrariesActivity.EXTRA_IMPORT_STATEMENT)
            if (result.resultCode == RESULT_OK && importStatement != null) {
                editorController.insertAtCursor("$importStatement\n")
                Toast.makeText(this, getString(R.string.library_import_inserted_toast), Toast.LENGTH_SHORT).show()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        BottomNavHelper.setup(this, BottomNavTab.EDITOR)
        RinEngine.init(applicationContext) // يفعّل save/installation/file الحقيقية على تخزين التطبيق الخاص

        // إن جاء التطبيق من شاشة الملفات/المشاريع، اربط RinEngine بمجلد ذلك المشروع تحديداً
        // (basePath مستقل لكل مشروع)، حتى لا تتشارك مشاريع مختلفة نفس rin_installed/.
        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
        if (projectName != null) {
            val project = ProjectManager.listProjects(this).find { it.name == projectName }
            if (project != null) {
                currentProject = project
                RinEngine.init(applicationContext, project.dir.absolutePath)
            }
        }

        editCode = findViewById(R.id.editCode)
        txtLineNumbers = findViewById(R.id.txtLineNumbers)
        txtEngineVersion = findViewById(R.id.txtEngineVersion)
        txtFileName = findViewById(R.id.txtFileName)
        rvJobs = findViewById(R.id.rvJobs)
        progressRunning = findViewById(R.id.progressRunning)
        findBar = findViewById(R.id.findBar)
        txtFind = findViewById(R.id.txtFind)
        txtReplace = findViewById(R.id.txtReplace)
        txtFindCount = findViewById(R.id.txtFindCount)
        scrollEditor = findViewById(R.id.scrollEditor)

        applyStoredEditorSettings()

        // أزرار الوصول السريع (أيقونة فقط، صغيرة جداً) في الصف الأول من الشريط العلوي
        val btnRun: ImageButton = findViewById(R.id.btnRun)
        val btnProjects: ImageButton = findViewById(R.id.btnProjects)

        // شريط البحث والاستبدال (يُفتح من قائمة Edit)
        val btnFindPrev: Button = findViewById(R.id.btnFindPrev)
        val btnFindNext: Button = findViewById(R.id.btnFindNext)
        val btnFindCase: ImageButton = findViewById(R.id.btnFindCase)
        val btnReplaceOne: Button = findViewById(R.id.btnReplaceOne)
        val btnReplaceAll: Button = findViewById(R.id.btnReplaceAll)
        val btnFindClose: ImageButton = findViewById(R.id.btnFindClose)
        val btnClearConsole: Button = findViewById(R.id.btnClearConsole)

        // شريط القوائم المصغّر بأسلوب الكمبيوتر: File / Edit / View / Run
        val btnMenuFile: Button = findViewById(R.id.btnMenuFile)
        val btnMenuEdit: Button = findViewById(R.id.btnMenuEdit)
        val btnMenuView: Button = findViewById(R.id.btnMenuView)
        val btnMenuRun: Button = findViewById(R.id.btnMenuRun)
        val btnMenuLibraries: Button = findViewById(R.id.btnMenuLibraries)

        if (savedInstanceState == null) {
            val project = currentProject
            val libraryName = intent.getStringExtra(EXTRA_LIBRARY_NAME)
            val fileName = intent.getStringExtra(EXTRA_FILE_NAME)
            val libraryToOpen = if (project != null && libraryName != null) {
                ProjectManager.listLibraries(project).find { it.name == libraryName }
            } else null
            val fileToOpen = if (libraryToOpen == null && project != null && fileName != null) {
                ProjectManager.listFiles(project).find { it.name == fileName }
            } else null
            when {
                libraryToOpen != null -> {
                    currentProjectLibrary = libraryToOpen
                    editCode.setText(ProjectManager.readLibrary(libraryToOpen))
                    txtFileName.text = getString(R.string.library_file_name_format, libraryToOpen.name)
                }
                fileToOpen != null -> {
                    currentProjectFile = fileToOpen
                    editCode.setText(ProjectManager.readFile(fileToOpen))
                    txtFileName.text = fileToOpen.name
                }
                else -> editCode.setText(getString(R.string.sample_program))
            }
        }

        // تلوين الصيغة النحوية (syntax highlighting) أثناء الكتابة
        RinSyntaxHighlighter.attach(this, editCode)
        // أرقام الأسطر + تراجع/إعادة + مسافة بادئة تلقائية + أقواس مغلقة تلقائياً + تظليل الأقواس/السطر الحالي
        editorController = CodeEditorController(this, editCode, txtLineNumbers, scrollEditor)

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

        // ----- أزرار الوصول السريع (الصف الأول) -----
        btnRun.setOnClickListener { runProgram() }
        btnProjects.setOnClickListener {
            startActivity(android.content.Intent(this, ProjectsActivity::class.java))
        }

        btnClearConsole.setOnClickListener { RinJobScheduler.clear() }

        // ----- شريط البحث والاستبدال -----
        btnFindClose.setOnClickListener {
            findBar.visibility = android.view.View.GONE
            editorController.clearMatchHighlights()
        }
        btnFindPrev.setOnClickListener {
            val found = editorController.findPrevious(txtFind.text.toString())
            if (!found) {
                Toast.makeText(this, getString(R.string.find_not_found_toast, txtFind.text.toString()), Toast.LENGTH_SHORT).show()
            }
            updateFindCount()
        }
        btnFindNext.setOnClickListener {
            val found = editorController.findNext(txtFind.text.toString())
            if (!found) {
                Toast.makeText(this, getString(R.string.find_not_found_toast, txtFind.text.toString()), Toast.LENGTH_SHORT).show()
            }
            updateFindCount()
        }
        btnFindCase.setOnClickListener {
            editorController.caseSensitiveSearch = !editorController.caseSensitiveSearch
            val msg = if (editorController.caseSensitiveSearch) R.string.find_case_sensitive_on_toast else R.string.find_case_sensitive_off_toast
            Toast.makeText(this, getString(msg), Toast.LENGTH_SHORT).show()
            editorController.highlightMatches(txtFind.text.toString())
            updateFindCount()
        }
        btnReplaceOne.setOnClickListener {
            editorController.replaceOne(txtFind.text.toString(), txtReplace.text.toString())
            updateFindCount()
        }
        btnReplaceAll.setOnClickListener {
            val count = editorController.replaceAll(txtFind.text.toString(), txtReplace.text.toString())
            Toast.makeText(this, getString(R.string.replaced_count_toast, count), Toast.LENGTH_SHORT).show()
            editorController.highlightMatches(txtFind.text.toString())
            updateFindCount()
        }
        txtFind.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                editorController.highlightMatches(s?.toString().orEmpty())
                updateFindCount()
            }
        })

        // ----- شريط القوائم المصغّر: File / Edit / View / Run -----
        // كل زر صغير جداً يفتح PopupMenu بأسلوب قوائم الكمبيوتر (File/Edit/View/Run)،
        // فيتوفّر عدد أكبر من الخيارات دون تكديس عشرات الأزرار على شاشة الموبايل.
        btnMenuFile.setOnClickListener { showFileMenu(it) }
        btnMenuEdit.setOnClickListener { showEditMenu(it) }
        btnMenuView.setOnClickListener { showViewMenu(it) }
        btnMenuRun.setOnClickListener { showRunMenu(it) }
        btnMenuLibraries.setOnClickListener { openLibrariesScreen() }
    }

    /** يبني PopupMenu بمظهر داكن يتناسق مع بقية التطبيق. */
    private fun darkPopupMenu(anchor: android.view.View): PopupMenu {
        val themedContext = ContextThemeWrapper(this, MaterialR.style.ThemeOverlay_MaterialComponents_Dark)
        return PopupMenu(themedContext, anchor)
    }

    private fun showFileMenu(anchor: android.view.View) {
        val popup = darkPopupMenu(anchor)
        popup.menu.add(0, 1, 0, R.string.menu_file_new)
        popup.menu.add(0, 2, 1, R.string.menu_file_open)
        popup.menu.add(0, 3, 2, R.string.menu_file_save)
        popup.menu.add(0, 4, 3, R.string.menu_file_projects)
        popup.menu.add(0, 5, 4, R.string.menu_file_libraries)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> newFile()
                2 -> openDocumentLauncher.launch(arrayOf("text/plain", "application/octet-stream", "*/*"))
                3 -> saveFile()
                4 -> startActivity(android.content.Intent(this, ProjectsActivity::class.java))
                5 -> openLibrariesScreen()
            }
            true
        }
        popup.show()
    }

    /**
     * يفتح شاشة "المكتبات" (رفع/إنشاء/تعديل مكتبات lib/ *.og.rin + إدراج @import في الكود).
     * تحتاج مكتبات المستخدم مشروعاً حقيقياً (basePath) تُحفَظ أسفله، لذا تُطلَب أولاً هنا لو لم
     * يكن المحرر مفتوحاً من داخل مشروع بعد (وضع "فتح ملف حر" عبر SAF).
     */
    private fun openLibrariesScreen() {
        val project = currentProject
        if (project == null) {
            Toast.makeText(this, getString(R.string.libraries_need_project_toast), Toast.LENGTH_LONG).show()
            startActivity(android.content.Intent(this, ProjectsActivity::class.java))
            return
        }
        val intent = android.content.Intent(this, LibrariesActivity::class.java)
        intent.putExtra(LibrariesActivity.EXTRA_PROJECT_NAME, project.name)
        librariesLauncher.launch(intent)
    }

    private fun showEditMenu(anchor: android.view.View) {
        val popup = darkPopupMenu(anchor)
        popup.menu.add(0, 1, 0, R.string.menu_edit_undo)
        popup.menu.add(0, 2, 1, R.string.menu_edit_redo)
        popup.menu.add(0, 3, 2, R.string.menu_edit_find)
        popup.menu.add(0, 4, 3, R.string.menu_edit_select_all)
        popup.menu.add(0, 5, 4, R.string.menu_edit_clear_all)
        popup.menu.add(0, 6, 5, R.string.menu_edit_toggle_comment)
        popup.menu.add(0, 7, 6, R.string.menu_edit_duplicate_line)
        popup.menu.add(0, 8, 7, R.string.menu_edit_delete_line)
        popup.menu.add(0, 9, 8, R.string.menu_edit_move_line_up)
        popup.menu.add(0, 10, 9, R.string.menu_edit_move_line_down)
        popup.menu.add(0, 11, 10, R.string.menu_edit_indent)
        popup.menu.add(0, 12, 11, R.string.menu_edit_unindent)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> editorController.undo()
                2 -> editorController.redo()
                3 -> toggleFindBar()
                4 -> editCode.selectAll()
                5 -> editCode.setText("")
                6 -> editorController.toggleLineComment()
                7 -> editorController.duplicateCurrentLine()
                8 -> {
                    editorController.deleteCurrentLine()
                    Toast.makeText(this, getString(R.string.line_deleted_toast), Toast.LENGTH_SHORT).show()
                }
                9 -> editorController.moveLineUp()
                10 -> editorController.moveLineDown()
                11 -> editorController.indentSelection()
                12 -> editorController.unindentSelection()
            }
            true
        }
        popup.show()
    }

    private fun showViewMenu(anchor: android.view.View) {
        val popup = darkPopupMenu(anchor)
        popup.menu.add(0, 1, 0, R.string.menu_view_zoom_in)
        popup.menu.add(0, 2, 1, R.string.menu_view_zoom_out)
        popup.menu.add(0, 3, 2, R.string.menu_view_toggle_lines)
        popup.menu.add(0, 4, 3, R.string.menu_view_clear_console)
        popup.menu.add(0, 5, 4, R.string.menu_view_go_to_line)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> changeEditorFontSize(1f)
                2 -> changeEditorFontSize(-1f)
                3 -> toggleLineNumbers()
                4 -> RinJobScheduler.clear()
                5 -> showGoToLineDialog()
            }
            true
        }
        popup.show()
    }

    private fun showRunMenu(anchor: android.view.View) {
        val popup = darkPopupMenu(anchor)
        popup.menu.add(0, 1, 0, R.string.menu_run_run)
        popup.menu.add(0, 2, 1, R.string.menu_run_check_brackets)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> runProgram()
                2 -> checkBrackets()
            }
            true
        }
        popup.show()
    }

    private fun runProgram() {
        val source = editCode.text.toString()
        RinJobScheduler.submit(source)

        // لا نشغّل الأنبوب فعلياً هنا (ذلك يحدث داخل شاشة RinFlow نفسها عبر PipelineTracer)؛
        // فقط نتحقّق بسرعة هل يحتوي الكود على كتلة @container.pipe لنعرض خيار الانتقال إليها.
        if (PipelineTracer.containsPipeline(source)) {
            com.google.android.material.snackbar.Snackbar
                .make(rvJobs, getString(R.string.rinflow_detected_snackbar), com.google.android.material.snackbar.Snackbar.LENGTH_LONG)
                .setAction(getString(R.string.rinflow_open_action)) { openPipeline() }
                .setActionTextColor(ContextCompat.getColor(this, R.color.rin_accent))
                .show()
        }
    }

    private fun openPipeline() {
        val source = editCode.text.toString()
        val intent = android.content.Intent(this, PipelineRunnerActivity::class.java)
        intent.putExtra(PipelineRunnerActivity.EXTRA_CODE, source)
        startActivity(intent)
    }

    private fun newFile() {
        editCode.setText("")
        currentUri = null
        currentProjectFile = null
        currentProjectLibrary = null
        txtFileName.text = getString(R.string.new_file_name)
        Toast.makeText(this, getString(R.string.new_file_toast), Toast.LENGTH_SHORT).show()
    }

    private fun saveFile() {
        val projectLibrary = currentProjectLibrary
        val projectFile = currentProjectFile
        val existingUri = currentUri
        when {
            projectLibrary != null -> saveToProjectLibrary(projectLibrary)
            projectFile != null -> saveToProjectFile(projectFile)
            existingUri != null -> writeToUri(existingUri)
            else -> createDocumentLauncher.launch(suggestedFileName())
        }
    }

    private fun toggleFindBar() {
        val showing = findBar.visibility != android.view.View.VISIBLE
        findBar.visibility = if (showing) android.view.View.VISIBLE else android.view.View.GONE
        if (!showing) editorController.clearMatchHighlights()
    }

    /** يحدّث شارة "N/M" بجانب حقل البحث حسب مطابقات النص الحالي وموضع المؤشر. */
    private fun updateFindCount() {
        val (current, total) = editorController.matchInfo(txtFind.text.toString())
        txtFindCount.text = if (total == 0) getString(R.string.find_count_none)
        else getString(R.string.find_count_format, current, total)
    }

    /** يفتح حواراً بسيطاً لإدخال رقم سطر والقفز إليه مباشرة، مع تمرير المحرر لإظهاره. */
    private fun showGoToLineDialog() {
        val input = EditText(this).apply {
            inputType = android.text.InputType.TYPE_CLASS_NUMBER
            hint = getString(R.string.go_to_line_hint)
            setTextColor(ContextCompat.getColor(this@MainActivity, R.color.rin_on_toolbar))
            setHintTextColor(ContextCompat.getColor(this@MainActivity, R.color.rin_editor_hint))
            setPadding(48, 24, 48, 24)
        }
        val themedContext = ContextThemeWrapper(this, MaterialR.style.ThemeOverlay_MaterialComponents_Dark)
        AlertDialog.Builder(themedContext)
            .setTitle(R.string.go_to_line_title)
            .setView(input)
            .setPositiveButton(R.string.go_to_line_action) { dialog, _ ->
                val line = input.text.toString().toIntOrNull()
                if (line == null) {
                    Toast.makeText(this, getString(R.string.go_to_line_invalid_toast), Toast.LENGTH_SHORT).show()
                } else {
                    editorController.goToLine(line)
                }
                dialog.dismiss()
            }
            .setNegativeButton(R.string.go_to_line_cancel) { dialog, _ -> dialog.dismiss() }
            .show()
    }

    /** يتحقق من توازن الأقواس في كامل الكود، ويعرض النتيجة كرسالة سريعة. */
    private fun checkBrackets() {
        val problemLine = editorController.checkBracketBalance()
        if (problemLine == null) {
            Toast.makeText(this, getString(R.string.brackets_balanced_toast), Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, getString(R.string.brackets_unbalanced_toast, problemLine), Toast.LENGTH_LONG).show()
            editorController.goToLine(problemLine)
        }
    }

    private var lineNumbersVisible = true

    /** يقرأ إعدادات المحرر المحفوظة من شاشة "الإعدادات" (حجم الخط، أرقام الأسطر) ويطبّقها فور فتح المحرر. */
    private fun applyStoredEditorSettings() {
        val savedSp = AppSettings.getEditorFontSizeSp(this)
        editCode.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, savedSp)
        txtLineNumbers.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, savedSp)

        lineNumbersVisible = AppSettings.getShowLineNumbers(this)
        txtLineNumbers.visibility = if (lineNumbersVisible) android.view.View.VISIBLE else android.view.View.GONE
    }

    private fun toggleLineNumbers() {
        lineNumbersVisible = !lineNumbersVisible
        txtLineNumbers.visibility = if (lineNumbersVisible) android.view.View.VISIBLE else android.view.View.GONE
        AppSettings.setShowLineNumbers(this, lineNumbersVisible)
    }

    private fun changeEditorFontSize(deltaSp: Float) {
        val currentSp = editCode.textSize / resources.displayMetrics.scaledDensity
        val newSp = (currentSp + deltaSp).coerceIn(AppSettings.MIN_FONT_SIZE_SP, AppSettings.MAX_FONT_SIZE_SP)
        editCode.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, newSp)
        txtLineNumbers.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, newSp)
        AppSettings.setEditorFontSizeSp(this, newSp)
    }

    /** يحفظ محتوى المحرر مباشرة داخل ملف المشروع الحالي (بدون المرور بحوار SAF). */
    private fun saveToProjectFile(file: RinFile) {
        val project = currentProject ?: return
        try {
            val updated = ProjectManager.writeFile(project, file.name, editCode.text.toString())
            currentProjectFile = updated
            Toast.makeText(this, getString(R.string.file_saved_toast, updated.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    /** يحفظ محتوى المحرر مباشرة داخل ملف المكتبة الحالي (lib/ *.og.rin) بدون المرور بحوار SAF. */
    private fun saveToProjectLibrary(library: RinLibrary) {
        val project = currentProject ?: return
        try {
            val updated = ProjectManager.writeLibrary(project, library, editCode.text.toString())
            currentProjectLibrary = updated
            Toast.makeText(this, getString(R.string.file_saved_toast, updated.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
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
            currentProjectFile = null
            currentProjectLibrary = null
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
