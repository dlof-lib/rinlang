package com.dlof.rinlang

import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.text.Editable
import android.text.TextWatcher
import android.view.ContextThemeWrapper
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.PopupMenu
// RinSpinner (مؤشر التحميل الدائري المخصَّص بهوية العلامة) في نفس الحزمة، لا حاجة لاستيراده صراحةً.
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.GravityCompat
import androidx.core.view.isVisible
import androidx.drawerlayout.widget.DrawerLayout
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.R as MaterialR
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import kotlin.math.roundToInt

/**
 * IDE-style activity for the Rin language:
 *  - A code editor with line numbers, syntax highlighting, undo/redo,
 *    auto-indent and find/replace — implemented from scratch in pure Kotlin
 *    ([RinCodeEditorView], [RinCodeEditorController], [RinNativeEditor], [rin::Lexer]).
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

        /** مهلة التوقف عن الكتابة قبل التشغيل التلقائي (إعداد "التشغيل التلقائي"). */
        private const val AUTO_RUN_DELAY_MS = 1_200L
        /** مهلة دفع التعديلات إلى المعاينة الحية بعد آخر ضغطة مفتاح. */
        private const val LIVE_PREVIEW_DELAY_MS = 200L
    }

    /** المشروع الحالي إن جاء التطبيق من شاشة الملفات، وإلا null (وضع الملف الحر عبر SAF كما كان سابقاً). */
    private var currentProject: Project? = null
    private var currentProjectFile: RinFile? = null
    /** غير null فقط عندما يكون المحرر مفتوحاً على مكتبة lib/ *.og.rin بدل ملف .rin عادي. */
    private var currentProjectLibrary: RinLibrary? = null

    private lateinit var editCode: RinCodeEditorView
    /** شريط رموز البرمجة/الأسهم أسفل المحرر (activity_main.xml)؛ يظهر فقط عند تركيز [editCode]. */
    private lateinit var editorKeyboardPanel: LinearLayout
    private lateinit var txtLineNumbers: TextView
    private lateinit var txtEngineVersion: TextView
    private lateinit var txtFileName: TextView
    private lateinit var rvJobs: RecyclerView
    private lateinit var progressRunning: RinSpinner
    private lateinit var findBar: LinearLayout
    private lateinit var txtFind: EditText
    private lateinit var txtReplace: EditText
    private lateinit var txtFindCount: TextView
    private lateinit var scrollEditor: ScrollView
    private lateinit var txtCursorPosition: TextView
    private lateinit var txtDocumentInfo: TextView

    private lateinit var editorController: RinCodeEditorController
    private lateinit var jobAdapter: RinJobAdapter

    // --- درج "مستكشف المشروع" المدمج في المحرر (view_project_explorer.xml داخل DrawerLayout
    // في activity_main.xml): شجرة ملفات/مجلدات + شجرة بنية الحاويات، بدل شاشة FilesActivity
    // منفصلة أو حوار AlertDialog منفصل لكل منهما. انظر setupExplorerDrawer(). ---
    private lateinit var drawerLayout: DrawerLayout
    private lateinit var btnExplorer: ImageButton
    private lateinit var txtExplorerProjectName: TextView
    private lateinit var btnExplorerClose: ImageButton
    private lateinit var btnExplorerTabFiles: TextView
    private lateinit var btnExplorerTabOutline: TextView
    private lateinit var rvExplorerFiles: RecyclerView
    private lateinit var rvExplorerOutline: RecyclerView
    private lateinit var txtExplorerEmpty: TextView
    private lateinit var btnExplorerOpenProjects: TextView
    private lateinit var explorerFilesAdapter: ProjectExplorerAdapter
    /** مسارات المجلدات (relPath) الموسَّعة حالياً في شجرة المستكشف؛ تُصفَّر بإعادة إنشاء الشاشة. */
    private val expandedExplorerFolders = mutableSetOf<String>()
    /** true = تبويب "الملفات" ظاهر حالياً في الدرج، false = تبويب "البنية" (الحاويات). */
    private var explorerShowingFiles = true

    /** URI of the file currently open, if any. Null means "unsaved / new file". */
    private var currentUri: Uri? = null

    // --- حالة التعديل والمهام المؤجَّلة (حفظ تلقائي / تشغيل تلقائي / معاينة حية) ---
    private val uiHandler = Handler(Looper.getMainLooper())
    private var livePreviewTask: Runnable? = null
    private var autoSaveTask: Runnable? = null
    private var autoRunTask: Runnable? = null
    private var lastAutoRunSource: String? = null

    /** نص المحرر لحظة آخر تحميل أو حفظ؛ أي اختلاف عنه = تعديلات غير محفوظة. */
    private var savedSnapshot: String = ""

    /** true أثناء تغيير النص برمجيًا (فتح ملف، ملف جديد، تنسيق عند الحفظ) كي لا يُعامَل كتعديل من المستخدم. */
    private var programmaticChange = false

    private lateinit var backCallback: OnBackPressedCallback

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
        // RIN logo is redrawn progressively and acts as the editor loading bar.
        RinLogoLoadingOverlay.show(this, 1050L)
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
        txtCursorPosition = findViewById(R.id.txtCursorPosition)
        txtDocumentInfo = findViewById(R.id.txtDocumentInfo)

        drawerLayout = findViewById(R.id.drawerLayout)
        btnExplorer = findViewById(R.id.btnExplorer)
        txtExplorerProjectName = findViewById(R.id.txtExplorerProjectName)
        btnExplorerClose = findViewById(R.id.btnExplorerClose)
        btnExplorerTabFiles = findViewById(R.id.btnExplorerTabFiles)
        btnExplorerTabOutline = findViewById(R.id.btnExplorerTabOutline)
        rvExplorerFiles = findViewById(R.id.rvExplorerFiles)
        rvExplorerOutline = findViewById(R.id.rvExplorerOutline)
        txtExplorerEmpty = findViewById(R.id.txtExplorerEmpty)
        btnExplorerOpenProjects = findViewById(R.id.btnExplorerOpenProjects)

        applyStoredEditorSettings()
        RinLogoLoadingOverlay.setProgress(0.34f)

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

        var initialLanguageExtension = "rin"
        if (savedInstanceState == null) {
            val project = currentProject
            val libraryName = intent.getStringExtra(EXTRA_LIBRARY_NAME)
            val fileName = intent.getStringExtra(EXTRA_FILE_NAME)
            val libraryToOpen = if (project != null && libraryName != null) {
                ProjectManager.listLibraries(project).find { it.name == libraryName }
            } else null
            val fileToOpen = if (libraryToOpen == null && project != null && fileName != null) {
                ProjectManager.findFileByRelPath(project, fileName)
            } else null
            when {
                libraryToOpen != null -> {
                    currentProjectLibrary = libraryToOpen
                    editCode.setText(ProjectManager.readLibrary(libraryToOpen))
                    txtFileName.text = getString(R.string.library_file_name_format, libraryToOpen.name)
                    initialLanguageExtension = "rin"
                }
                fileToOpen != null -> {
                    currentProjectFile = fileToOpen
                    editCode.setText(ProjectManager.readFile(fileToOpen))
                    txtFileName.text = fileToOpen.name
                    initialLanguageExtension = extensionOf(fileToOpen.name)
                }
                else -> editCode.setText(getString(R.string.sample_program))
            }
        }
        markClean()

        // أرقام الأسطر + تراجع/إعادة + مسافة بادئة تلقائية + أقواس مغلقة تلقائياً + تظليل الأقواس/السطر الحالي
        // + تلوين نحوي حقيقي (Kotlin خالص عبر RinSyntax، بلا أي C++/JNI) يتبدّل تلقائيًا حسب امتداد الملف.
        editorController = RinCodeEditorController(this, editCode, txtLineNumbers, scrollEditor)
        editorController.setLanguage(initialLanguageExtension)
        editorController.addCursorStateChangeListener { updateStatusBar() }
        updateStatusBar()
        // حركة القرص (pinch-to-zoom) داخل المحرر تُغيّر حجم خطه مباشرة؛ نُزامن هنا عمود أرقام
        // الأسطر (لا يملكه RinCodeEditorView) ونحفظ القيمة الجديدة، تمامًا مثل أزرار +/- الحالية.
        editCode.onFontSizeChangeListener = { newSp ->
            txtLineNumbers.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, newSp)
            AppSettings.setEditorFontSizeSp(this, newSp)
        }
        RinLogoLoadingOverlay.setProgress(0.78f)

        // كل تعديل في المحرر يمرّ من هنا مرة واحدة: دفع للمعاينة الحية، ثم الحفظ التلقائي، ثم التشغيل التلقائي
        // (انظر onEditorTextChanged) — بدل مراقب مستقل لكل ميزة.
        editCode.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = onEditorTextChanged()
        })

        // قائمة التشغيل المجدولة (job queue): كل عملية Run بطاقة مستقلة
        jobAdapter = RinJobAdapter(this)
        jobAdapter.onCancelRequested = { number -> RinJobScheduler.cancel(number) }
        jobAdapter.onPinToggleRequested = { number -> RinJobScheduler.togglePin(number) }
        rvJobs.layoutManager = LinearLayoutManager(this)
        rvJobs.adapter = jobAdapter
        // Wired through RinExecutionManager (Queue -> Structured Events -> Run Session) rather
        // than reading RinJobScheduler directly, so the adapter's per-run stats footer
        // (RinExecutionManager.toSession(job).stats) reflects the same real data pipeline the
        // rest of the run-history UI uses, instead of two code paths reading the scheduler.
        RinExecutionManager.attach { sessions ->
            val jobs = sessions.map { it.job }
            jobAdapter.submit(jobs)
            if (jobs.isNotEmpty()) rvJobs.scrollToPosition(jobs.size - 1)
            val anyRunning = jobs.any { it.status == JobStatus.RUNNING }
            progressRunning.visibility = if (anyRunning) android.view.View.VISIBLE else android.view.View.GONE
            if (anyRunning) progressRunning.start() else progressRunning.stop()
        }

        RinLogoLoadingOverlay.setProgress(0.94f)
        window.decorView.post { RinLogoLoadingOverlay.finish() }

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
            refreshFindModeIndicator(btnFindCase)
            editorController.highlightMatches(txtFind.text.toString())
            updateFindCount()
        }
        // اضغط مطولاً على زر "حساسية الأحرف" لتبديل وضع البحث بتعبير نمطي (regex) — بلا حاجة لزر
        // إضافي في شريط ضيّق أصلاً على شاشة الهاتف؛ الوضعان مستقلّان (يمكن الجمع بينهما).
        btnFindCase.setOnLongClickListener {
            editorController.regexSearch = !editorController.regexSearch
            val msg = if (editorController.regexSearch) R.string.find_regex_on_toast else R.string.find_regex_off_toast
            Toast.makeText(this, getString(msg), Toast.LENGTH_SHORT).show()
            refreshFindModeIndicator(btnFindCase)
            editorController.highlightMatches(txtFind.text.toString())
            updateFindCount()
            true
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

        setupEditorKeyboardPanel()
        setupExplorerDrawer()

        // زر الرجوع: يسأل قبل المغادرة فقط عند تفعيل "تأكيد قبل الخروج" ووجود تعديلات غير محفوظة.
        backCallback = object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = onBackRequested()
        }
        onBackPressedDispatcher.addCallback(this, backCallback)
    }

    // ---- لوحة مفاتيح المحرر: شريط رموز برمجة شائعة + أسهم تنقل ------------------------------

    /**
     * يربط كل زر في [R.id.editorKeyboardPanel] (activity_main.xml) إمّا بإدراج نصّه عند مؤشر
     * [editCode] مباشرة ([insertEditorSymbol]، بلا إغلاق أقواس تلقائي — تمامًا كالكتابة اليدوية)
     * أو بمحاكاة ضغطة مفتاح فعلي ([sendEditorKey]) لمفاتيح التنقل/Tab/Backspace التي يتعامل معها
     * [RinCodeEditorView.onKeyDown] أصلاً (نفس مسار لوحة مفاتيح خارجية بلوتوث). اللوحة نفسها
     * تظهر/تختفي تبعًا لتركيز [editCode] فقط: أزرارها كلها focusable="false" حتى لا تسرق التركيز
     * منه عند اللمس (وإلا اختفت اللوحة قبل تنفيذ الضغطة).
     */
    private fun setupEditorKeyboardPanel() {
        editorKeyboardPanel = findViewById(R.id.editorKeyboardPanel)

        val symbolButtons = listOf(
            R.id.btnKeyBraceOpen to "{", R.id.btnKeyBraceClose to "}",
            R.id.btnKeyParenOpen to "(", R.id.btnKeyParenClose to ")",
            R.id.btnKeyBracketOpen to "[", R.id.btnKeyBracketClose to "]",
            R.id.btnKeySemicolon to ";", R.id.btnKeyColon to ":",
            R.id.btnKeyDquote to "\"", R.id.btnKeySquote to "'",
            R.id.btnKeyLt to "<", R.id.btnKeyGt to ">",
            R.id.btnKeyEq to "=", R.id.btnKeyPlus to "+", R.id.btnKeyMinus to "-",
            R.id.btnKeyStar to "*", R.id.btnKeySlash to "/", R.id.btnKeyPercent to "%",
            R.id.btnKeyBang to "!", R.id.btnKeyAmp to "&", R.id.btnKeyPipe to "|",
            R.id.btnKeyBackslash to "\\", R.id.btnKeyBacktick to "`", R.id.btnKeyTilde to "~",
            R.id.btnKeyHash to "#", R.id.btnKeyAt to "@", R.id.btnKeyDollar to "$",
            R.id.btnKeyCaret to "^", R.id.btnKeyUnderscore to "_",
            R.id.btnKeyDot to ".", R.id.btnKeyComma to ","
        )
        symbolButtons.forEach { (id, symbol) ->
            findViewById<Button>(id).setOnClickListener { insertEditorSymbol(symbol) }
        }

        val navButtons = listOf(
            R.id.btnKeyTab to KeyEvent.KEYCODE_TAB,
            R.id.btnKeyHome to KeyEvent.KEYCODE_MOVE_HOME,
            R.id.btnKeyLeft to KeyEvent.KEYCODE_DPAD_LEFT,
            R.id.btnKeyUp to KeyEvent.KEYCODE_DPAD_UP,
            R.id.btnKeyDown to KeyEvent.KEYCODE_DPAD_DOWN,
            R.id.btnKeyRight to KeyEvent.KEYCODE_DPAD_RIGHT,
            R.id.btnKeyEnd to KeyEvent.KEYCODE_MOVE_END,
            R.id.btnKeyBackspace to KeyEvent.KEYCODE_DEL
        )
        navButtons.forEach { (id, keyCode) ->
            findViewById<Button>(id).setOnClickListener { sendEditorKey(keyCode) }
        }

        // تراجع/إعادة (Undo/Redo): عبر دالتي editCode.undo()/redo() الحقيقيتين مباشرة (نفس ما
        // يستدعيه btnMenuEdit)، لا محاكاة KeyEvent — أبسط وأدق لأنهما ليستا KeyEvent أصلاً.
        findViewById<Button>(R.id.btnKeyUndo).setOnClickListener {
            if (!editCode.hasFocus()) editCode.requestFocus()
            editCode.undo()
        }
        findViewById<Button>(R.id.btnKeyRedo).setOnClickListener {
            if (!editCode.hasFocus()) editCode.requestFocus()
            editCode.redo()
        }

        // صف ثانٍ خاص بلغة Rin نفسها: حبّة لكل كلمة محجوزة حقيقية في rin_lexer.cpp (نفس مصدر
        // توثيق hover في RinKeywordDocs)، بدل الاكتفاء برموز ترقيم عامة كأي محرر نصوص. تُنشأ
        // برمجيًا لا في XML حتى تبقى قائمة الأزرار متزامنة دومًا مع RinKeywordDocs.allKeywords
        // بلا تكرار يدوي قد يفوته تحديث مستقبلي للغة.
        val keywordsRow: LinearLayout = findViewById(R.id.editorKeyboardKeywordsRow)
        val chipHeightPx = (30 * resources.displayMetrics.density).roundToInt()
        val chipMarginEndPx = (5 * resources.displayMetrics.density).roundToInt()
        RinKeywordDocs.allKeywords.forEach { keyword ->
            val chip = Button(this, null, 0, R.style.RinEditorKeywordChip)
            chip.text = keyword
            chip.layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, chipHeightPx
            ).apply { marginEnd = chipMarginEndPx }
            // مسافة زائدة بعد الكلمة (وليس قبلها) تسهّل متابعة الكتابة مباشرة بعد إدراجها.
            chip.setOnClickListener { insertEditorSymbol("$keyword ") }
            keywordsRow.addView(chip)
        }

        // اللوحة تظهر فقط أثناء تركيز المحرر (أي أثناء ظهور لوحة مفاتيح النظام أو التركيز
        // البرمجي)، بدل شغل مساحة دائمة على شاشة هاتف صغيرة. بفضل windowSoftInputMode=
        // "adjustResize" لهذا الـActivity وكونها آخر عنصر في العمود الرأسي الجذر، تطفو تلقائياً
        // مباشرة فوق لوحة مفاتيح النظام دون أي حساب WindowInsets يدوي.
        editCode.setOnFocusChangeListener { _, hasFocus ->
            editorKeyboardPanel.visibility = if (hasFocus) View.VISIBLE else View.GONE
        }
        if (editCode.hasFocus()) editorKeyboardPanel.visibility = View.VISIBLE
    }

    private fun insertEditorSymbol(symbol: String) {
        if (!editCode.hasFocus()) editCode.requestFocus()
        editCode.insertAtCursor(symbol)
    }

    private fun sendEditorKey(keyCode: Int) {
        if (!editCode.hasFocus()) editCode.requestFocus()
        val now = SystemClock.uptimeMillis()
        editCode.dispatchKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_DOWN, keyCode, 0))
        editCode.dispatchKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_UP, keyCode, 0))
    }

    override fun onResume() {
        super.onResume()
        // الإعدادات تتغيّر من شاشة الإعدادات والمحرر ما زال حيًا في المكدّس (singleTop)؛ نعيد تطبيقها هنا.
        applyStoredEditorSettings()
        editCode.invalidate()
    }

    override fun onPause() {
        super.onPause()
        // المهام المؤجَّلة لا معنى لها والمحرر خارج الشاشة: الحفظ يتم هنا مباشرة، والتشغيل التلقائي يُلغى.
        autoSaveTask?.let { uiHandler.removeCallbacks(it) }
        autoRunTask?.let { uiHandler.removeCallbacks(it) }
        if (AppSettings.isSaveOnPause(this) && hasSaveTarget() && hasUnsavedChanges()) saveSilently()
    }

    // ---- تتبّع التعديل والحفظ/التشغيل التلقائيان --------------------------------------------

    private fun hasUnsavedChanges(): Boolean = editCode.text.toString() != savedSnapshot

    private fun markClean() {
        savedSnapshot = editCode.text.toString()
        // انصهار كل مسارات الحفظ في نقطة واحدة: أي حفظ ناجح (يدوي/تلقائي/onPause) يمر من هنا،
        // فهذا المكان الوحيد الكافي لإزالة علامة "تعديل غير محفوظ" الحقيقية من مستكشف المشروع.
        val proj = currentProject
        val file = currentProjectFile
        if (proj != null && file != null) EditorDirtyState.unmark(proj.name, file.relPath)
    }

    /** هل للمحرر وجهة حفظ معروفة (ملف مشروع/مكتبة/URI)؟ الملف الجديد بلا وجهة يحتاج حوار SAF فلا يُحفَظ صامتًا. */
    private fun hasSaveTarget(): Boolean =
        currentProjectLibrary != null || currentProjectFile != null || currentUri != null

    /** يحمّل نصًا في المحرر برمجيًا (لا يُحتسب تعديلًا من المستخدم) ثم يعدّه "نظيفًا". */
    private fun loadIntoEditor(text: String) {
        programmaticChange = true
        try { editCode.setText(text) } finally { programmaticChange = false }
        markClean()
        lastAutoRunSource = null
    }

    private fun onEditorTextChanged() {
        scheduleLivePreviewPush()
        if (programmaticChange) return
        // تعديل حقيقي من المستخدم (لا فتح ملف برمجياً): علِّم الملف "غير محفوظ" فوراً لمستكشف
        // المشروع (EditorDirtyState)، حتى قبل أن يمرّ التهدئة القصيرة للحفظ التلقائي.
        val proj = currentProject
        val file = currentProjectFile
        if (proj != null && file != null) EditorDirtyState.mark(proj.name, file.relPath)
        scheduleAutoSave()
        scheduleAutoRun()
    }

    /** دفع التعديلات (بعد تهدئة قصيرة) إلى جلسة المعاينة الحية إن كانت مفتوحة (IndsinPreviewManager.pushLiveEdit). */
    private fun scheduleLivePreviewPush() {
        if (!IndsinPreviewManager.isRunning) return
        livePreviewTask?.let { uiHandler.removeCallbacks(it) }
        val source = editCode.text.toString()
        val task = Runnable { IndsinPreviewManager.pushLiveEdit(source) }
        livePreviewTask = task
        uiHandler.postDelayed(task, LIVE_PREVIEW_DELAY_MS)
    }

    private fun scheduleAutoSave() {
        autoSaveTask?.let { uiHandler.removeCallbacks(it) }
        val delayMs = AppSettings.getAutoSaveDelayMs(this)
        if (delayMs <= 0 || !hasSaveTarget()) return
        val task = Runnable { if (hasUnsavedChanges()) saveSilently() }
        autoSaveTask = task
        uiHandler.postDelayed(task, delayMs.toLong())
    }

    private fun scheduleAutoRun() {
        autoRunTask?.let { uiHandler.removeCallbacks(it) }
        if (!AppSettings.isAutoRunEnabled(this)) return
        val task = Runnable { autoRunNow() }
        autoRunTask = task
        uiHandler.postDelayed(task, AUTO_RUN_DELAY_MS)
    }

    /** تشغيل صامت للكود الحالي: لا معاينة ولا Snackbar، ولا يُشغَّل كود غير مكتمل أو لم يتغيّر منذ آخر تشغيل تلقائي. */
    private fun autoRunNow() {
        val source = editCode.text.toString()
        if (source.isBlank() || source == lastAutoRunSource) return
        // أقواس/وسوم غير متوازنة = كود ما زال قيد الكتابة؛ تشغيله يملأ الكونسول بأخطاء لا فائدة منها.
        if (editorController.checkBracketBalance() != null || editorController.checkTagBalance() != null) return
        lastAutoRunSource = source
        if (AppSettings.isClearConsoleOnRun(this)) RinJobScheduler.clear()
        RinJobScheduler.submit(source) // الطابور ممتلئ → يُتجاهَل بصمت (هذا تشغيل لم يطلبه المستخدم صراحةً)
    }

    // ---- درج "مستكشف المشروع" المدمج: شجرة الملفات/المجلدات + شجرة بنية الحاويات -----------

    /**
     * يهيّئ درج "مستكشف المشروع" (view_project_explorer.xml داخل DrawerLayout في
     * activity_main.xml) مرة واحدة: شجرة ملفات حقيقية عبر [ProjectExplorerAdapter] في تبويب
     * "الملفات"، وشجرة بنية الحاويات (نفس [OutlineDialogAdapter] الذي كان يُستخدَم سابقاً داخل
     * حوار [showOutlineDialog] فقط) في تبويب "البنية" — كلاهما الآن داخل المحرر نفسه بلا أي
     * Activity أو Dialog منفصل.
     */
    private fun setupExplorerDrawer() {
        txtExplorerProjectName.text = currentProject?.name ?: getString(R.string.file_unsaved)

        explorerFilesAdapter = ProjectExplorerAdapter(
            onFolderToggled = { folder ->
                if (!expandedExplorerFolders.add(folder.relPath)) expandedExplorerFolders.remove(folder.relPath)
                refreshExplorerFiles()
            },
            onFileClicked = { file -> openProjectFileFromExplorer(file) }
        )
        rvExplorerFiles.layoutManager = LinearLayoutManager(this)
        rvExplorerFiles.adapter = explorerFilesAdapter
        rvExplorerOutline.layoutManager = LinearLayoutManager(this)

        btnExplorer.setOnClickListener { openExplorerDrawer(preferOutlineTab = false) }
        btnExplorerClose.setOnClickListener { drawerLayout.closeDrawer(GravityCompat.START) }
        btnExplorerTabFiles.setOnClickListener { switchExplorerTab(showFiles = true) }
        btnExplorerTabOutline.setOnClickListener { switchExplorerTab(showFiles = false) }
        btnExplorerOpenProjects.setOnClickListener {
            startActivity(android.content.Intent(this, ProjectsActivity::class.java))
        }

        // يعيد بناء التبويب الظاهر حالياً في كل مرة يُفتَح فيها الدرج (زر شريط الأدوات، أو
        // سحبة إصبع من حافة الشاشة)، حتى يعكس أي تعديل حدث بعد آخر فتح (كتابة تجعل ملفاً "متسخاً"،
        // حفظ يزيل العلامة، إلخ) دون الحاجة لتحديث حيّ مكلف مع كل ضغطة مفتاح.
        drawerLayout.addDrawerListener(object : DrawerLayout.SimpleDrawerListener() {
            override fun onDrawerOpened(drawerView: View) {
                if (explorerShowingFiles) refreshExplorerFiles() else refreshExplorerOutline()
            }
        })
    }

    private fun openExplorerDrawer(preferOutlineTab: Boolean) {
        switchExplorerTab(showFiles = !preferOutlineTab)
        drawerLayout.openDrawer(GravityCompat.START)
    }

    private fun switchExplorerTab(showFiles: Boolean) {
        explorerShowingFiles = showFiles
        rvExplorerFiles.isVisible = showFiles
        rvExplorerOutline.isVisible = !showFiles
        btnExplorerTabFiles.setBackgroundResource(if (showFiles) R.drawable.bg_explorer_tab_selected else R.drawable.bg_menubar_btn)
        btnExplorerTabFiles.setTextColor(ContextCompat.getColor(this, if (showFiles) R.color.rin_on_toolbar else R.color.rin_on_toolbar_dim))
        btnExplorerTabOutline.setBackgroundResource(if (!showFiles) R.drawable.bg_explorer_tab_selected else R.drawable.bg_menubar_btn)
        btnExplorerTabOutline.setTextColor(ContextCompat.getColor(this, if (!showFiles) R.color.rin_on_toolbar else R.color.rin_on_toolbar_dim))
        if (showFiles) refreshExplorerFiles() else refreshExplorerOutline()
    }

    /**
     * يعيد بناء شجرة الملفات/المجلدات من القرص فعلياً في كل مرة (لا كاش وسيط) حتى تعكس أي تعديل
     * خارجي (رفع ملف، إنشاء مجلد من شاشة "الملفات" الكاملة...) فوراً عند فتح الدرج. في وضع
     * "الملف الحر" (بلا مشروع، عبر SAF) تُستبدَل الشجرة برسالة توضيحية بدل قائمة فارغة صامتة.
     */
    private fun refreshExplorerFiles() {
        val project = currentProject
        txtExplorerEmpty.isVisible = false
        btnExplorerOpenProjects.isVisible = false
        if (project == null) {
            rvExplorerFiles.isVisible = false
            txtExplorerEmpty.text = getString(R.string.explorer_no_project)
            txtExplorerEmpty.isVisible = true
            btnExplorerOpenProjects.isVisible = true
            return
        }
        val nodes = ProjectExplorerTree.build(project, expandedExplorerFolders)
        explorerFilesAdapter.submit(nodes, currentProjectFile?.relPath, EditorDirtyState.dirtyPaths(project.name))
        if (nodes.isEmpty()) {
            rvExplorerFiles.isVisible = false
            txtExplorerEmpty.text = getString(R.string.explorer_empty_folder)
            txtExplorerEmpty.isVisible = true
        } else {
            rvExplorerFiles.isVisible = true
        }
    }

    /** يعيد بناء شجرة بنية الحاويات ([RinContainerTags.buildOutline]) لنص المحرر الحالي — بنفس
     *  البيانات التي كانت تُعرَض سابقاً داخل حوار [showOutlineDialog] فقط. */
    private fun refreshExplorerOutline() {
        val entries = editorController.buildOutline()
        btnExplorerOpenProjects.isVisible = false
        if (entries.isEmpty()) {
            rvExplorerOutline.isVisible = false
            txtExplorerEmpty.text = getString(R.string.explorer_outline_empty)
            txtExplorerEmpty.isVisible = true
            return
        }
        txtExplorerEmpty.isVisible = false
        rvExplorerOutline.isVisible = true
        rvExplorerOutline.adapter = OutlineDialogAdapter(
            entries = entries,
            lineLabel = { entry ->
                val end = entry.endLine
                if (end != null && end > entry.lineNumber) {
                    "(" + getString(R.string.outline_line_range_format, entry.lineNumber, end) + ")"
                } else {
                    "(" + getString(R.string.outline_line_format, entry.lineNumber) + ")"
                }
            },
            onEntryClick = { entry ->
                editorController.goToLine(entry.lineNumber)
                drawerLayout.closeDrawer(GravityCompat.START)
            }
        )
    }

    /**
     * يفتح ملف مشروع آخر مباشرة في نفس نسخة [MainActivity] الحالية — هذا صلب طلب الدمج: قبل هذا
     * التعديل كان أي نقر على ملف في شاشة "الملفات" يفتح Activity جديدة بالكامل عبر Intent
     * ([FilesActivity] → EXTRA_PROJECT_NAME/EXTRA_FILE_NAME). يسأل أولاً عن التعديلات غير
     * المحفوظة بنفس حوار [onBackRequested] وخياراته الثلاثة (إلغاء/تجاهل/حفظ)، ثم يحمّل نص
     * الملف الجديد ويحدّث اسم/لغة المحرر وشريط الحالة، ويصفّر [currentProjectLibrary] لأن
     * المستكشف يفتح ملفات .rin عادية فقط (لا مكتبات lib/*.og.rin).
     */
    private fun openProjectFileFromExplorer(file: RinFile) {
        if (currentProjectLibrary == null && currentProjectFile?.relPath == file.relPath) {
            drawerLayout.closeDrawer(GravityCompat.START)
            return
        }
        fun proceed() {
            currentProjectLibrary = null
            currentProjectFile = file
            loadIntoEditor(ProjectManager.readFile(file))
            txtFileName.text = file.name
            editorController.setLanguage(extensionOf(file.name))
            updateStatusBar()
            drawerLayout.closeDrawer(GravityCompat.START)
            refreshExplorerFiles()
        }
        if (hasUnsavedChanges() && hasSaveTarget()) {
            AlertDialog.Builder(this)
                .setTitle(R.string.exit_unsaved_title)
                .setMessage(R.string.exit_unsaved_message)
                .setNegativeButton(R.string.exit_unsaved_stay, null)
                .setPositiveButton(R.string.exit_unsaved_discard) { _, _ -> proceed() }
                .setNeutralButton(R.string.exit_unsaved_save) { _, _ -> if (saveSilently()) proceed() }
                .show()
        } else {
            proceed()
        }
    }

    private fun onBackRequested() {
        if (drawerLayout.isDrawerOpen(GravityCompat.START)) {
            drawerLayout.closeDrawer(GravityCompat.START)
            return
        }
        if (!AppSettings.isConfirmExit(this) || !hasUnsavedChanges()) { leaveEditor(); return }
        val builder = AlertDialog.Builder(this)
            .setTitle(R.string.exit_unsaved_title)
            .setMessage(R.string.exit_unsaved_message)
            .setNegativeButton(R.string.exit_unsaved_stay, null)
            .setPositiveButton(R.string.exit_unsaved_discard) { _, _ -> leaveEditor() }
        if (hasSaveTarget()) {
            builder.setNeutralButton(R.string.exit_unsaved_save) { _, _ -> if (saveSilently()) leaveEditor() }
        }
        builder.show()
    }

    /** ينفّذ الرجوع الافتراضي للنظام (نعطّل معالجنا أولًا كي لا يعيد الطلب إلى نفسه). */
    private fun leaveEditor() {
        backCallback.isEnabled = false
        onBackPressedDispatcher.onBackPressed()
    }

    /** يبني PopupMenu ببطاقة داكنة دائرية الزوايا وظل واضح (bg_popup_menu)، بدل مستطيل النظام
     *  المسطّح الافتراضي، لتتطابق قوائم File/Edit/View/Run مع هوية التطبيق الاحترافية. */
    private fun darkPopupMenu(anchor: android.view.View): PopupMenu {
        val themedContext = ContextThemeWrapper(this, R.style.ThemeOverlay_RinLang_PopupMenu)
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
        popup.menu.add(0, 13, 12, R.string.menu_edit_insert_snippet)
        popup.menu.add(0, 14, 13, R.string.menu_edit_sort_lines)
        popup.menu.add(0, 15, 14, R.string.menu_edit_join_line)
        popup.menu.add(0, 16, 15, R.string.menu_edit_trim_now)
        popup.menu.add(0, 17, 16, R.string.menu_edit_convert_tabs_now)
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
                13 -> showSnippetsDialog()
                14 -> toastChanged(editorController.sortLines(), R.string.lines_sorted_toast)
                15 -> toastChanged(editorController.joinCurrentLine(), R.string.lines_joined_toast)
                16 -> toastChanged(editorController.trimTrailingWhitespaceNow(), R.string.trailing_trimmed_toast)
                17 -> toastChanged(editorController.convertTabsToSpacesNow(AppSettings.getTabSize(this)), R.string.tabs_converted_toast)
            }
            true
        }
        popup.show()
    }

    /** رسالة سريعة موحَّدة لأفعال Edit الفورية: [changed] يحدّد أي رسالة تُعرَض (نجاح أم "لا تغييرات"). */
    private fun toastChanged(changed: Boolean, successMessage: Int) {
        val message = if (changed) successMessage else R.string.no_changes_toast
        Toast.makeText(this, getString(message), Toast.LENGTH_SHORT).show()
    }

    /**
     * يعرض قائمة مقتطفات لغة الحاويات في Rin ([RinSnippets.all])، ويُدرج المقتطف المختار عند
     * المؤشر مع ترك المؤشر داخل جسم الحاوية مباشرة (انظر [RinCodeEditorController.insertSnippetAtCursor]).
     */
    private fun showSnippetsDialog() {
        val snippets = RinSnippets.all
        val titles = snippets.map { it.title }.toTypedArray()
        val themedContext = ContextThemeWrapper(this, MaterialR.style.ThemeOverlay_MaterialComponents_Dark)
        AlertDialog.Builder(themedContext)
            .setTitle(R.string.snippets_dialog_title)
            .setItems(titles) { dialog, which ->
                editorController.insertSnippetAtCursor(snippets[which].template)
                dialog.dismiss()
            }
            .setNegativeButton(R.string.go_to_line_cancel) { dialog, _ -> dialog.dismiss() }
            .show()
    }

    private fun showViewMenu(anchor: android.view.View) {
        val popup = darkPopupMenu(anchor)
        popup.menu.add(0, 1, 0, R.string.menu_view_zoom_in)
        popup.menu.add(0, 2, 1, R.string.menu_view_zoom_out)
        popup.menu.add(0, 3, 2, R.string.menu_view_toggle_lines)
        popup.menu.add(0, 4, 3, R.string.menu_view_clear_console)
        popup.menu.add(0, 5, 4, R.string.menu_view_go_to_line)
        popup.menu.add(0, 6, 5, R.string.menu_view_outline)
        popup.menu.add(0, 7, 6, R.string.menu_view_fold_current)
        popup.menu.add(0, 8, 7, R.string.menu_view_fold_all)
        popup.menu.add(0, 9, 8, R.string.menu_view_unfold_all)
        popup.menu.add(0, 10, 9, R.string.menu_view_toggle_whitespace)
        popup.menu.add(0, 11, 10, R.string.menu_view_settings)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> changeEditorFontSize(1f)
                2 -> changeEditorFontSize(-1f)
                3 -> toggleLineNumbers()
                4 -> RinJobScheduler.clear()
                5 -> showGoToLineDialog()
                6 -> showOutlineDialog()
                7 -> editCode.foldCurrentLevel()
                8 -> editCode.foldAll()
                9 -> editCode.unfoldAll()
                10 -> toggleWhitespace()
                11 -> startActivity(android.content.Intent(this, SettingsActivity::class.java))
            }
            true
        }
        popup.show()
    }

    /**
     * يعرض "بنية الملف": كل حاويات/أقسام Rin (@container، Section، Translations...) مع رقم
     * سطرها وعمق تعشيشها، والنقر على أي عنصر يقفز إليه مباشرة عبر
     * [RinCodeEditorController.goToLine] — تنقّل أسرع من التمرير اليدوي في الملفات الطويلة.
     *
     * كانت هذه الشجرة تُعرَض سابقاً داخل [AlertDialog] منفصل قائم بذاته. الآن تُعرَض داخل تبويب
     * "البنية" في درج "مستكشف المشروع" المدمج في المحرر نفسه (انظر [openExplorerDrawer]،
     * [refreshExplorerOutline]) بدل نافذة منبثقة منفصلة، فيبقى المستخدم داخل سياق المحرر دائماً.
     */
    private fun showOutlineDialog() {
        openExplorerDrawer(preferOutlineTab = true)
    }

    private fun showRunMenu(anchor: android.view.View) {
        val popup = darkPopupMenu(anchor)
        popup.menu.add(0, 1, 0, R.string.menu_run_run)
        popup.menu.add(0, 2, 1, R.string.menu_run_check_brackets)
        popup.menu.add(0, 3, 2, R.string.menu_run_live_preview)
        popup.menu.add(0, 4, 3, R.string.menu_run_check_tags)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> runProgram()
                2 -> checkBrackets()
                3 -> openLivePreviewManually()
                4 -> checkContainerTags()
            }
            true
        }
        popup.show()
    }

    private fun runProgram() {
        val source = editCode.text.toString()
        if (AppSettings.isClearConsoleOnRun(this)) RinJobScheduler.clear()
        val job = RinJobScheduler.submit(source)
        if (job == null) {
            Toast.makeText(this, getString(R.string.job_queue_full_toast), Toast.LENGTH_SHORT).show()
            return
        }

        // لا نشغّل الأنبوب فعلياً هنا (ذلك يحدث داخل شاشة RinFlow نفسها عبر PipelineTracer)؛
        // فقط نتحقّق بسرعة هل يحتوي الكود على كتلة @container.pipe لنعرض خيار الانتقال إليها.
        if (PipelineTracer.containsPipeline(source)) {
            com.google.android.material.snackbar.Snackbar
                .make(rvJobs, getString(R.string.rinflow_detected_snackbar), com.google.android.material.snackbar.Snackbar.LENGTH_LONG)
                .setAction(getString(R.string.rinflow_open_action)) { openPipeline() }
                .setActionTextColor(ContextCompat.getColor(this, R.color.rin_accent))
                .show()
        }

        // وبالمثل: أي @view/@loop root حقيقي في الكود يفتح شاشة "المعاينة الحية" مباشرةً —
        // كل تشغيل (Run) هو إعادة تنفيذ كاملة عمداً (تماماً كبطاقة عمل جديدة في قائمة RinJobScheduler)،
        // بينما التعديلات اللاحقة أثناء الكتابة تُحدَّث حيّاً عبر IndsinPreviewManager.pushLiveEdit
        // دون فقدان حالة Warp (كعدّاد ضُغط عليه).
        if (IndsinViewTracer.containsView(source)) {
            openLivePreview(source)
        }
    }

    /** يفتح المعاينة الحية ويبدأ/يعيد تشغيل جلستها بالكود الحالي للمحرر. */
    private fun openLivePreview(source: String) {
        // نترك IndsinPreviewActivity نفسها تستدعي IndsinPreviewManager.start() (في onCreate أو
        // onNewIntent حسب الحال) — فهي التي تعرف عرض الجهاز (rootWidth) الحالي المختار هناك؛
        // استدعاؤه هنا أيضاً كان سيعيد التشغيل مرتين بلا داعٍ.
        val intent = android.content.Intent(this, IndsinPreviewActivity::class.java)
        intent.putExtra(IndsinPreviewActivity.EXTRA_CODE, source)
        intent.putExtra(IndsinPreviewActivity.EXTRA_FILE_NAME, currentProjectFile?.name ?: "main.rin")
        intent.addFlags(android.content.Intent.FLAG_ACTIVITY_SINGLE_TOP)
        startActivity(intent)
    }

    /** فتح يدوي من قائمة التشغيل (زر "معاينة حية"): يتحقق من وجود @view أو @loop root. */
    private fun openLivePreviewManually() {
        val source = editCode.text.toString()
        if (!IndsinViewTracer.containsView(source)) {
            Toast.makeText(this, getString(R.string.indsin_no_view_toast), Toast.LENGTH_SHORT).show()
            return
        }
        openLivePreview(source)
    }

    private fun openPipeline() {
        val source = editCode.text.toString()
        val intent = android.content.Intent(this, PipelineRunnerActivity::class.java)
        intent.putExtra(PipelineRunnerActivity.EXTRA_CODE, source)
        startActivity(intent)
    }

    private fun newFile() {
        loadIntoEditor("")
        currentUri = null
        currentProjectFile = null
        currentProjectLibrary = null
        txtFileName.text = getString(R.string.new_file_name)
        editorController.setLanguage("rin")
        Toast.makeText(this, getString(R.string.new_file_toast), Toast.LENGTH_SHORT).show()
    }

    private fun saveFile() {
        if (!hasSaveTarget()) { createDocumentLauncher.launch(suggestedFileName()); return }
        saveSilently()
    }

    /** يحفظ إلى الوجهة المعروفة الحالية (ملف مشروع/مكتبة/URI) بلا حوار SAF. false إن لم توجد وجهة أو فشل الحفظ. */
    private fun saveSilently(): Boolean {
        val projectLibrary = currentProjectLibrary
        val projectFile = currentProjectFile
        val existingUri = currentUri
        return when {
            projectLibrary != null -> saveToProjectLibrary(projectLibrary)
            projectFile != null -> saveToProjectFile(projectFile)
            existingUri != null -> writeToUri(existingUri)
            else -> false
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

    /**
     * يتحقق من توازن وسوم لغة الحاويات في Rin (`@container=x ... .end/container`, `Section`,
     * `Translations`, `@view.*`, `@theme`...)، تماماً كـ [checkBrackets] لكن للوسوم النصية بدل
     * الأقواس — مفيد خاصة في ملفات @container.doc/@container.pipe الطويلة متعددة المستويات.
     */
    private fun checkContainerTags() {
        val problemLine = editorController.checkTagBalance()
        if (problemLine == null) {
            Toast.makeText(this, getString(R.string.tags_balanced_toast), Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, getString(R.string.tags_unbalanced_toast, problemLine), Toast.LENGTH_LONG).show()
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

        applyInterfaceSettings()
    }

    /** شريط الأدوات، الكونسول، تخطيط المحرر، وإبقاء الشاشة مضاءة — كلها من شاشة الإعدادات. */
    private fun applyInterfaceSettings() {
        // "شريط الأدوات" = صف الهوية (شعار/اسم الملف/اختصارات) فقط. صف القوائم File/Edit/View/Run يبقى دائمًا
        // كي لا يفقد المستخدم الحفظ والتشغيل حين يُخفي الشريط.
        val header = if (AppSettings.isShowToolbar(this)) View.VISIBLE else View.GONE
        findViewById<View>(R.id.editorHeaderRow).visibility = header
        findViewById<View>(R.id.editorHeaderDivider).visibility = header

        // قياسي: محرر 3 : كونسول 2 — مضغوط: 4 : 1 — تركيز: بلا كونسول أصلًا (وكذلك إن عُطِّل "إظهار الطرفية").
        val layout = AppSettings.getEditorLayout(this)
        val console = if (AppSettings.isShowConsole(this) && layout != AppSettings.LAYOUT_FOCUS) View.VISIBLE else View.GONE
        for (id in listOf(R.id.editorConsoleDivider, R.id.editorConsoleRoot, R.id.rvJobs)) {
            findViewById<View>(id).visibility = console
        }
        val compact = layout == AppSettings.LAYOUT_COMPACT
        setLayoutWeight(R.id.editorSurface, if (compact) 4f else 3f)
        setLayoutWeight(R.id.rvJobs, if (compact) 1f else 2f)

        if (AppSettings.isKeepScreenOn(this)) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun setLayoutWeight(viewId: Int, weight: Float) {
        val view = findViewById<View>(viewId)
        val params = view.layoutParams as LinearLayout.LayoutParams
        if (params.weight != weight) {
            params.weight = weight
            view.layoutParams = params
        }
    }

    private fun toggleWhitespace() {
        AppSettings.setBoolean(this, AppSettings.Key.SHOW_WHITESPACE, !AppSettings.isShowWhitespace(this))
        editCode.invalidate()
    }

    /** يلوّن زر "حساسية الأحرف/regex" بخلفية بارزة عندما يكون أي من الوضعين مفعَّلًا، ليكون
     *  للمستخدم مؤشر بصري دائم على حالة البحث الحالية، لا مجرد Toast يختفي. */
    private fun refreshFindModeIndicator(button: ImageButton) {
        val active = editorController.caseSensitiveSearch || editorController.regexSearch
        button.setBackgroundResource(if (active) R.drawable.bg_icon_btn_accent else R.drawable.bg_toolbar_btn_ghost)
    }

    /** يحدّث شريط الحالة الرفيع أسفل المحرر: موضع المؤشر، طول التحديد إن وُجد، وعدد أسطر المستند. */
    private fun updateStatusBar() {
        val info = editorController.statusInfo()
        txtCursorPosition.text = if (info.selectedChars > 0) {
            getString(R.string.status_cursor_position_selection, info.line, info.col, info.selectedChars)
        } else {
            getString(R.string.status_cursor_position, info.line, info.col)
        }
        txtDocumentInfo.text = getString(R.string.status_line_count, info.totalLines)
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

    /**
     * يجهّز محتوى المحرر للحفظ: يطبّق تحويلات "تنسيق عند الحفظ" (حسب إعدادات الحفظ الحالية —
     * انظر [AppSettings.saveOptions] و[EditorTextTransforms]) ويحدّث المحرر نفسه إن تغيّر شيء،
     * حتى يرى المستخدم فورًا ما كُتِب فعليًا في الملف بدل فرق صامت بين الشاشة والقرص.
     */
    private fun contentForSave(): String {
        val original = editCode.text.toString()
        val transformed = EditorTextTransforms.applyOnSave(original, AppSettings.saveOptions(this))
        if (transformed != original) loadIntoEditor(transformed)
        return transformed
    }

    /** يحفظ محتوى المحرر مباشرة داخل ملف المشروع الحالي (بدون المرور بحوار SAF). يُرجع نجاح العملية. */
    private fun saveToProjectFile(file: RinFile): Boolean {
        return try {
            // نكتب مباشرة إلى مسار الملف الحقيقي (file.file) بدل إعادة بنائه من اسمه المجرّد في
            // جذر المشروع — وإلا كان حفظ ملف داخل مجلد فرعي يُنشئ نسخة جديدة في الجذر (أو، الأسوأ،
            // يستبدل محتوى ملف آخر غير مرتبط يحمل نفس الاسم هناك) بدل تحديث الملف الأصلي مكانه.
            file.file.writeText(contentForSave())
            val updated = RinFile(file.name, file.file, file.file.length(), file.file.lastModified(), file.relPath)
            currentProjectFile = updated
            markClean()
            Toast.makeText(this, getString(R.string.file_saved_toast, updated.name), Toast.LENGTH_SHORT).show()
            true
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
            false
        }
    }

    /** يحفظ محتوى المحرر مباشرة داخل ملف المكتبة الحالي (lib/ *.og.rin) بدون المرور بحوار SAF. يُرجع نجاح العملية. */
    private fun saveToProjectLibrary(library: RinLibrary): Boolean {
        val project = currentProject ?: return false
        return try {
            val updated = ProjectManager.writeLibrary(project, library, contentForSave())
            currentProjectLibrary = updated
            markClean()
            Toast.makeText(this, getString(R.string.file_saved_toast, updated.name), Toast.LENGTH_SHORT).show()
            true
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
            false
        }
    }

    private fun suggestedFileName(): String = "program.rin"

    /** يستخرج امتداد الملف (بلا نقطة، بحروف صغيرة) من اسمه، أو "rin" إن لم يوجد امتداد. */
    private fun extensionOf(name: String): String {
        val ext = name.substringAfterLast('.', "")
        return ext.ifEmpty { "rin" }.lowercase()
    }

    private fun openFile(uri: Uri) {
        try {
            contentResolver.openInputStream(uri)?.use { input ->
                BufferedReader(InputStreamReader(input)).use { reader ->
                    val text = reader.readText()
                    loadIntoEditor(text)
                }
            }
            currentUri = uri
            currentProjectFile = null
            currentProjectLibrary = null
            val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "opened.rin"
            txtFileName.text = name
            editorController.setLanguage(extensionOf(name))
            Toast.makeText(this, getString(R.string.file_opened_toast, name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun writeToUri(uri: Uri): Boolean {
        return try {
            contentResolver.openOutputStream(uri, "wt")?.use { output ->
                OutputStreamWriter(output).use { writer ->
                    writer.write(contentForSave())
                }
            }
            currentUri = uri
            markClean()
            val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "program.rin"
            txtFileName.text = name
            Toast.makeText(this, getString(R.string.file_saved_toast, name), Toast.LENGTH_SHORT).show()
            true
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_save_error)}: ${t.message}", Toast.LENGTH_LONG).show()
            false
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

    override fun onDestroy() {
        // RinJobScheduler (and RinExecutionManager, which wraps it) is a process-wide singleton
        // that outlives this Activity; without this the lambda above would keep the destroyed
        // Activity reachable (and every view it holds) for as long as the process stays alive.
        RinExecutionManager.detach()
        uiHandler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }
}
