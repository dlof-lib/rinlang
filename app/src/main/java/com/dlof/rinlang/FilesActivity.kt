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
import android.view.ScaleGestureDetector
import android.widget.EditText
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import androidx.core.view.isVisible
import androidx.recyclerview.widget.ItemTouchHelper
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
 *  - تحديد متعدد: ضغطة مستمرة على صف تبدأ وضع التحديد، والسحب ↑↓ بعدها يحدّد نطاقاً، ثم شريط
 *    إجراءات (تحديد الكل/عكس/تكرار/نقل/نسخ المسار/حذف). انظر FileSelection.kt.
 *  - إيماءات الصف: سحب لليمين = إعادة تسمية، سحب لليسار = حذف (مع تأكيد).
 */
class FilesActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** بحث مبدئي قادم من شريط البحث في المحرر (يُنفَّذ كبحث محتوى في المشروع). */
        const val EXTRA_SEARCH_QUERY = "extra_search_query"
        const val EXTRA_SEARCH_CASE = "extra_search_case"
        const val EXTRA_SEARCH_WORD = "extra_search_word"
        const val EXTRA_SEARCH_REGEX = "extra_search_regex"
    }

    private lateinit var project: Project
    private lateinit var rvFiles: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var txtEmptyMessage: TextView
    private lateinit var adapter: FilesAdapter

    // --- التحديد المتعدد ---
    private val selection = SelectionModel()
    private lateinit var selectionBar: View
    private lateinit var txtSelectionCount: TextView
    private lateinit var fabContainer: View

    // --- البحث المتقدم (FileSearch.kt): يُحسب في خيط خلفي مع تأخير بسيط أثناء الكتابة ---
    private var searchOpts = SearchOptions(query = "")
    private var folderScope = false
    private var lastSearch: SearchResult? = null
    @Volatile private var searchToken = 0
    private val searchHandler = Handler(Looper.getMainLooper())
    private var pendingSearch: Runnable? = null

    /** المسار النسبي الحالي من جذر المشروع (فواصل "/")، فارغ يعني أننا في جذر المشروع. */
    private var currentRelDir: String = ""
    /** نص البحث الحالي في شريط "🔍 بحث في ملفات المشروع"؛ فارغ = تصفّح عادي مستوى بمستوى. */
    private var searchQuery: String = ""
    /** شريحة التصفية السريعة الحالية (الكل / أخطاء / غير محفوظة / مثبّتة). */
    private var statusMode: StatusMode = StatusMode.ALL

    private val importFileLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) importFile(uri)
        }

    private val importZipLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isNotEmpty()) ZipImport.fromUris(this, project, currentRelDir, uris, protectedPaths()) { refresh() }
        }

    private val importMediaLauncher =
        registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
            if (uri != null) importFile(uri)
        }

    private val importMultiLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isNotEmpty()) Uploader.files(this, project, currentRelDir, uris, protectedPaths()) { refresh() }
        }

    private val importTreeLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) Uploader.tree(this, project, currentRelDir, uri, protectedPaths()) { refresh() }
        }

    private val browseZipLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) ZipTools.browseUri(this, project, currentRelDir, uri, protectedPaths()) { refresh() }
        }

    private val importMultiMediaLauncher =
        registerForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris ->
            if (uris.isNotEmpty()) Uploader.files(this, project, currentRelDir, uris, protectedPaths()) { refresh() }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_files)
        onBackPressedDispatcher.addCallback(this, backCallback)

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
        // ضغطة مستمرة على «رجوع» = الخروج مباشرة من المشروع إلى قائمة المشاريع.
        findViewById<View>(R.id.btnToolbarBack).setOnLongClickListener { finish(); true }
        // ضغطة مستمرة على مسار المجلد = صعود مستوى واحد (النقر العادي يعود للجذر).
        findViewById<View>(R.id.txtToolbarSubtitle).setOnLongClickListener {
            if (currentRelDir.isNotBlank()) { currentRelDir = currentRelDir.substringBeforeLast('/', ""); refresh() }
            true
        }

        rvFiles = findViewById(R.id.rvFiles)
        txtEmpty = findViewById(R.id.txtEmptyFiles)
        txtEmptyMessage = findViewById(R.id.txtEmptyFilesMessage)
        adapter = FilesAdapter(
            onOpenFile = { file -> handleOpen(file) },
            onOpenFolder = { folder -> enterFolder(folder) },
            onRenameFile = { file -> showRenameFileDialog(file) },
            onRenameFolder = { folder -> showRenameFolderDialog(folder) },
            onDeleteFile = { file -> showDeleteFileConfirm(file) },
            onDeleteFolder = { folder -> showDeleteFolderConfirm(folder) },
            onMore = { entry, anchor -> showRowMenu(entry, anchor) },
            selection = selection,
            onSelectionChanged = { onSelectionChanged() }
        )
        rvFiles.layoutManager = LinearLayoutManager(this)
        rvFiles.adapter = adapter
        setupSelectionAndGestures()
        setupProChrome()

        findViewById<EditText>(R.id.inputSearchFiles).addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                searchQuery = s?.toString().orEmpty()
                updateSearchChrome()
                if (searchQuery.isBlank()) refresh() else runSearch(250L)
            }
        })

        intent.getStringExtra(EXTRA_SEARCH_QUERY)?.lineSequence()?.firstOrNull()?.takeIf { it.isNotBlank() }?.let { q ->
            searchOpts = searchOpts.copy(
                mode = SearchMode.CONTENT,
                caseSensitive = intent.getBooleanExtra(EXTRA_SEARCH_CASE, searchOpts.caseSensitive),
                wholeWord = intent.getBooleanExtra(EXTRA_SEARCH_WORD, searchOpts.wholeWord),
                regex = intent.getBooleanExtra(EXTRA_SEARCH_REGEX, searchOpts.regex)
            )
            renderSearchOptions()
            findViewById<EditText>(R.id.inputSearchFiles).setText(q)
        }

        setupFabMenu()
    }

    // ---- القائمة العائمة السريعة (speed-dial) ----

    private var fabOpen = false

    /** الزر الرئيسي يفتح/يغلق قائمة الإجراءات؛ ضغطة مستمرة عليه = مجلد جديد مباشرة (كما كان). */
    private fun setupFabMenu() {
        val main: View = findViewById(R.id.fabAddFile)
        main.setOnClickListener { setFabOpen(!fabOpen) }
        main.setOnLongClickListener {
            setFabOpen(false)
            FolderCreator.show(this, project, currentRelDir) { refresh() }
            true
        }
        findViewById<View>(R.id.fabScrim).setOnClickListener { setFabOpen(false) }

        fun item(rowId: Int, fabId: Int, action: () -> Unit) {
            val go = View.OnClickListener { setFabOpen(false); action() }
            findViewById<View>(rowId).setOnClickListener(go)
            findViewById<View>(fabId).setOnClickListener(go)
        }
        item(R.id.rowFabFile, R.id.fabNewFile) {
            FileCreator.show(this, project, currentRelDir, onSingle = { showCreateFileDialog() }) { refresh() }
        }
        item(R.id.rowFabFolder, R.id.fabNewFolder) { FolderCreator.show(this, project, currentRelDir) { refresh() } }
        item(R.id.rowFabUpload, R.id.fabUploadFile) { showUploadHub() }
        item(R.id.rowFabMedia, R.id.fabUploadMedia) { showMediaHub() }
        item(R.id.rowFabZip, R.id.fabZipTools) { showZipToolsDialog() }
        item(R.id.rowFabPaste, R.id.fabPaste) {
            FileClipboard.paste(this, project, currentRelDir, protectedPaths()) { refresh() }
        }
        item(R.id.rowFabSort, R.id.fabSort) { FileSort.showDialog(this) { refresh() } }
    }

    /** يفتح/يغلق القائمة بحركة متتابعة: العناصر تصعد من الزر الرئيسي، والزر يدور + ويعتّم الخلفية. */
    private fun setFabOpen(open: Boolean, animate: Boolean = true) {
        val menu = findViewById<ViewGroup>(R.id.fabMenu)
        val scrim = findViewById<View>(R.id.fabScrim)
        val main = findViewById<com.google.android.material.floatingactionbutton.FloatingActionButton>(R.id.fabAddFile)
        if (open == fabOpen && (open == menu.isVisible)) return
        fabOpen = open
        val d = resources.displayMetrics.density
        val dur = if (animate) 1L else 0L
        main.animate().cancel()
        scrim.animate().cancel()
        // العناصر الظاهرة فقط (اللصق يظهر عند وجود محتوى في الحافظة)، الأقرب للزر الرئيسي أولاً.
        findViewById<View>(R.id.rowFabPaste).isVisible = FileClipboard.hasContent
        val rows = (0 until menu.childCount).map { menu.getChildAt(it) }
            .filter { it.isVisible }.reversed()
        rows.forEach { it.animate().cancel() }

        if (open) {
            menu.visibility = View.VISIBLE
            scrim.visibility = View.VISIBLE
            scrim.animate().alpha(1f).setDuration(180L * dur).start()
            // الأيقونة تتبدّل «+» ← «×» مع دوران ربع دورة (الشكل مربّع فيبقى متوازياً مع المحاور في النهاية).
            main.setImageResource(R.drawable.ic_close)
            main.rotation = -90f
            main.animate().rotation(0f).setDuration(240L * dur)
                .setInterpolator(android.view.animation.OvershootInterpolator(1.1f)).start()
            rows.forEachIndexed { i, v ->
                v.alpha = 0f
                v.translationY = 28 * d
                v.scaleX = 0.92f; v.scaleY = 0.92f
                v.animate().alpha(1f).translationY(0f).scaleX(1f).scaleY(1f)
                    .setStartDelay(i * 26L * dur).setDuration(230L * dur)
                    .setInterpolator(android.view.animation.OvershootInterpolator(0.9f)).start()
            }
        } else {
            main.setImageResource(android.R.drawable.ic_input_add)
            main.rotation = 90f
            main.animate().rotation(0f).setDuration(200L * dur).start()
            scrim.animate().alpha(0f).setDuration(160L * dur).withEndAction {
                if (!fabOpen) scrim.visibility = View.GONE
            }.start()
            if (rows.isEmpty()) { menu.visibility = View.GONE; return }
            rows.forEachIndexed { i, v ->
                val last = i == rows.lastIndex
                v.animate().alpha(0f).translationY(16 * d).setStartDelay(0L).setDuration(130L * dur)
                    .setInterpolator(android.view.animation.AccelerateInterpolator())
                    .withEndAction { if (last && !fabOpen) menu.visibility = View.GONE }
                    .start()
            }
        }
    }

    // ---- التحديد المتعدد والإيماءات ----

    private fun setupSelectionAndGestures() {
        selectionBar = findViewById(R.id.layoutSelectionBar)
        txtSelectionCount = findViewById(R.id.txtSelectionCount)
        fabContainer = findViewById(R.id.fabContainer)

        // ضغطة مستمرة + سحب رأسي = تحديد نطاق (أو من أيقونة الصف مباشرة أثناء وضع التحديد).
        rvFiles.addOnItemTouchListener(
            DragSelectTouchListener(
                rv = rvFiles,
                model = selection,
                keyAt = { pos -> adapter.keyAt(pos) },
                onChanged = { onSelectionChanged() },
                handleViewIds = intArrayOf(R.id.frameFileIcon, R.id.frameFolderIcon)
            )
        )

        // سحب أفقي على الصف: يمين = إعادة تسمية، يسار = حذف بتأكيد.
        val accent = androidx.core.content.ContextCompat.getColor(this, R.color.rin_files_accent)
        val swipe = RowSwipeCallback(
            ctx = this,
            model = selection,
            rightAction = RowSwipeCallback.Action(
                R.drawable.ic_edit_pencil, accent, getString(R.string.swipe_rename_label)
            ) { pos -> adapter.entryAt(pos)?.let { renameEntry(it) } },
            leftAction = RowSwipeCallback.Action(
                R.drawable.ic_trash_red, 0xFFE5484D.toInt(), getString(R.string.swipe_delete_label)
            ) { pos -> adapter.entryAt(pos)?.let { deleteEntries(listOf(it)) } }
        )
        ItemTouchHelper(swipe).attachToRecyclerView(rvFiles)

        // حركة القرص (pinch) على القائمة: تصغير = عرض مضغوط، تكبير = عرض مريح.
        adapter.setCompact(FileDensity.compact(this))
        val pinch = object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            private var acc = 1f
            override fun onScaleBegin(d: ScaleGestureDetector): Boolean { acc = 1f; return true }
            override fun onScale(d: ScaleGestureDetector): Boolean { acc *= d.scaleFactor; return true }
            override fun onScaleEnd(d: ScaleGestureDetector) {
                val want = when { acc < 0.85f -> true; acc > 1.15f -> false; else -> return }
                if (want != FileDensity.compact(this@FilesActivity)) {
                    FileDensity.save(this@FilesActivity, want)
                    adapter.setCompact(want)
                }
            }
        }
        val pinchDetector = ScaleGestureDetector(this, pinch)
        rvFiles.addOnItemTouchListener(object : RecyclerView.SimpleOnItemTouchListener() {
            override fun onInterceptTouchEvent(rv: RecyclerView, e: android.view.MotionEvent): Boolean {
                pinchDetector.onTouchEvent(e)
                return false
            }
        })

        // عنوان الشاشة: نقرتان = إلى أول القائمة، ضغطة مستمرة = إلى آخرها.
        val titleGestures = android.view.GestureDetector(this, object : android.view.GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: android.view.MotionEvent): Boolean {
                if (adapter.itemCount > 0) rvFiles.smoothScrollToPosition(0)
                return true
            }
            override fun onLongPress(e: android.view.MotionEvent) {
                if (adapter.itemCount > 0) rvFiles.smoothScrollToPosition(adapter.itemCount - 1)
            }
        })
        findViewById<View>(R.id.txtToolbarTitle).setOnTouchListener { _, ev -> titleGestures.onTouchEvent(ev); true }

        findViewById<View>(R.id.btnSelectionClose).setOnClickListener { clearSelection() }
        findViewById<View>(R.id.btnSelectionAll).setOnClickListener { selectAllVisible() }
        findViewById<View>(R.id.btnSelectionMore).setOnClickListener { anchor ->
            SelectionMenu.show(
                ctx = this, anchor = anchor, project = project, entries = selectedEntries(),
                parentRelDir = currentRelDir, protectedPaths = protectedPaths(),
                onInvert = { invertSelection() },
                onChanged = {
                    selection.clear()
                    refresh()
                },
                onSelectPattern = { selectByPattern() }
            )
        }
        // ضغطة مستمرة على "تحديد الكل" = تحديد بنمط (*.rin ...).
        findViewById<View>(R.id.btnSelectionAll).setOnLongClickListener { selectByPattern(); true }
        findViewById<View>(R.id.btnSelectionDelete).setOnClickListener { deleteEntries(selectedEntries()) }
        // تنزيل المحدّد إلى الجهاز: عنصر ملف واحد = ملف خام، وإلا ZIP.
        findViewById<View>(R.id.btnSelectionDownload).setOnClickListener {
            DownloadExporter.saveEntries(this, project, selectedEntries())
        }
    }

    // ---- ميزات احترافية: مسار تفاعلي، الأخيرة، نطاق البحث، ملخص المجلد ----

    private fun setupProChrome() {
        findViewById<View>(R.id.btnRecentFiles).setOnClickListener { showRecentDialog() }
        searchOpts = SearchPrefs.load(this)
        folderScope = SearchPrefs.folderScope(this)
        val input = findViewById<EditText>(R.id.inputSearchFiles)
        input.setOnFocusChangeListener { _, _ -> updateSearchChrome() }
        input.setOnEditorActionListener { v, _, _ ->
            if (searchQuery.isNotBlank()) SearchHistory.add(this, project.name, searchQuery)
            (getSystemService(INPUT_METHOD_SERVICE) as android.view.inputmethod.InputMethodManager)
                .hideSoftInputFromWindow(v.windowToken, 0)
            true
        }
        renderSearchOptions()
        updateSearchChrome()
        adapter.onOpenHit = { hit -> openSearchHit(hit) }
        adapter.onVisibleChanged = {
            pruneSelection()
            updateEmptyState()
        }
        renderStatusChips()
    }

    /** شرائح التصفية السريعة: تُبدَّل على القائمة الحالية دون إعادة قراءة القرص. */
    private fun renderStatusChips() {
        val box = findViewById<android.widget.LinearLayout>(R.id.layoutStatusChips)
        box.removeAllViews()
        val d = resources.displayMetrics.density
        val modes = listOf(
            StatusMode.ALL to R.string.status_all,
            StatusMode.ERRORS to R.string.status_errors,
            StatusMode.UNSAVED to R.string.status_unsaved,
            StatusMode.PINNED to R.string.status_pinned
        )
        for ((mode, label) in modes) {
            val on = mode == statusMode
            box.addView(TextView(this).apply {
                setText(label)
                textSize = 13f
                maxLines = 1
                gravity = android.view.Gravity.CENTER
                setTextColor(androidx.core.content.ContextCompat.getColor(
                    context, if (on) R.color.rin_files_on_accent else R.color.rin_files_chip_text))
                if (on) setTypeface(typeface, android.graphics.Typeface.BOLD)
                setPadding((16 * d).toInt(), (8 * d).toInt(), (16 * d).toInt(), (8 * d).toInt())
                setBackgroundResource(if (on) R.drawable.bg_file_chip_on else R.drawable.bg_file_chip)
                isClickable = true
                isFocusable = true
                setOnClickListener {
                    if (statusMode != mode) {
                        statusMode = mode
                        adapter.setStatusMode(mode)
                        renderStatusChips()
                    }
                }
                layoutParams = android.widget.LinearLayout.LayoutParams(
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT
                ).apply { marginEnd = (8 * d).toInt() }
            })
        }
    }

    /** يحدّد ظهور حالة "فارغ" ورسالتها حسب العناصر الظاهرة فعلاً (بعد التصفية). */
    private fun updateEmptyState() {
        val empty = adapter.itemCount == 0
        if (empty) {
            txtEmptyMessage.setText(
                when {
                    statusMode != StatusMode.ALL -> R.string.filter_no_match
                    searchQuery.isNotBlank() -> R.string.search_no_results
                    else -> R.string.no_files_yet
                }
            )
        }
        txtEmpty.visibility = if (empty) View.VISIBLE else View.GONE
    }

    private fun showRecentDialog() {
        val rels = FileRecents.list(this, project)
        if (rels.isEmpty()) {
            Toast.makeText(this, R.string.recent_empty, Toast.LENGTH_SHORT).show()
            return
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.recent_title)
            .setItems(rels.toTypedArray()) { _, which ->
                val rel = rels[which]
                val f = java.io.File(project.dir, rel)
                handleOpen(RinFile(f.name, f, f.length(), f.lastModified(), rel))
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يبني شريط المسار: [المشروع] › [مجلد] › [مجلد فرعي]. آخر مستوى (الحالي) بلون الهوية. */
    private fun renderBreadcrumb() {
        val box = findViewById<android.widget.LinearLayout>(R.id.layoutBreadcrumb)
        box.removeAllViews()
        val rtl = resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL
        val d = resources.displayMetrics.density
        val parts = currentRelDir.split('/').filter { it.isNotBlank() }
        fun chip(label: String, rel: String, current: Boolean) {
            val tv = TextView(this).apply {
                text = label
                textSize = 13f
                maxLines = 1
                setTextColor(androidx.core.content.ContextCompat.getColor(context, if (current) R.color.rin_files_hero_accent else R.color.rin_files_hero_text))
                if (current) setTypeface(typeface, android.graphics.Typeface.BOLD)
                setPadding((8 * d).toInt(), (6 * d).toInt(), (8 * d).toInt(), (6 * d).toInt())
                setBackgroundResource(R.drawable.bg_files_crumb)
                setOnClickListener {
                    if (!current) {
                        currentRelDir = rel
                        val search = this@FilesActivity.findViewById<EditText>(R.id.inputSearchFiles)
                        if (search.text.isNotEmpty()) search.setText("") else refresh()
                    }
                }
                setOnLongClickListener {
                    val cm = this@FilesActivity.getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
                    cm.setPrimaryClip(ClipData.newPlainText("path", rel))
                    Toast.makeText(context, R.string.path_copied_toast, Toast.LENGTH_SHORT).show()
                    true
                }
            }
            box.addView(tv)
        }
        fun sep() {
            box.addView(TextView(this).apply {
                text = if (rtl) "‹" else "›"
                textSize = 15f
                setTextColor(androidx.core.content.ContextCompat.getColor(context, R.color.rin_files_hero_dim))
            })
        }
        chip(project.name, "", parts.isEmpty())
        var acc = ""
        parts.forEachIndexed { i, seg ->
            acc = if (acc.isEmpty()) seg else "$acc/$seg"
            sep()
            chip(seg, acc, i == parts.lastIndex)
        }
        val sv = findViewById<android.widget.HorizontalScrollView>(R.id.scrollBreadcrumb)
        sv.post { sv.fullScroll(if (rtl) View.FOCUS_LEFT else View.FOCUS_RIGHT) }
    }

    private fun renderSummary(folders: Int, files: Int, bytes: Long) {
        findViewById<TextView>(R.id.txtFilesSummary).apply {
            visibility = View.VISIBLE
            setTextColor(androidx.core.content.ContextCompat.getColor(this@FilesActivity, R.color.rin_files_hero_dim))
            text = getString(R.string.files_summary_format, files, folders, android.text.format.Formatter.formatFileSize(this@FilesActivity, bytes))
        }
    }

    // ---- البحث المتقدم: التشغيل، الملخص، الخيارات، السجل، الاستبدال ----

    private fun currentSearchOptions(): SearchOptions =
        searchOpts.copy(query = searchQuery, scopeRel = if (folderScope) currentRelDir else "")

    /** يلغي أي بحث جارٍ (نتيجته المتأخرة تُتجاهل عبر [searchToken]). */
    private fun cancelSearch() {
        pendingSearch?.let { searchHandler.removeCallbacks(it) }
        pendingSearch = null
        searchToken++
        lastSearch = null
    }

    private fun runSearch(delayMs: Long) {
        pendingSearch?.let { searchHandler.removeCallbacks(it) }
        val token = ++searchToken
        val opts = currentSearchOptions()
        findViewById<TextView>(R.id.txtFilesSummary).apply {
            visibility = View.VISIBLE
            setTextColor(androidx.core.content.ContextCompat.getColor(this@FilesActivity, R.color.rin_files_hero_dim))
            setText(R.string.sr_searching)
        }
        val run = Runnable {
            Thread {
                val res = FileSearch.search(project.dir, opts, { token != searchToken }) { ProjectManager.isBinaryFile(it) }
                searchHandler.post { if (token == searchToken) applySearchResult(res) }
            }.start()
        }
        pendingSearch = run
        if (delayMs <= 0L) run.run() else searchHandler.postDelayed(run, delayMs)
    }

    private fun applySearchResult(res: SearchResult) {
        lastSearch = res
        val rows = res.hits.map { it.file }
        adapter.setPinned(FilePins.get(this, project.name))
        adapter.setSearchHits(res.hits.associateBy { it.file.relPath })
        adapter.submit(emptyList(), rows, searchMode = true)
        adapter.setDirtyPaths(EditorDirtyState.dirtyPaths(project.name))
        pruneSelection()
        updateEmptyState()
        scanForErrors(rows.take(60))
        renderSearchSummary(res)
        renderSearchOptions()
    }

    private fun renderSearchSummary(res: SearchResult) {
        val tv = findViewById<TextView>(R.id.txtFilesSummary)
        tv.visibility = View.VISIBLE
        if (res.error != null) {
            tv.setTextColor(0xFFFF6B76.toInt())
            tv.text = getString(R.string.sr_regex_error, res.error)
            return
        }
        tv.setTextColor(androidx.core.content.ContextCompat.getColor(this, R.color.rin_files_hero_dim))
        val base = if (res.totalMatches > 0)
            getString(R.string.sr_summary_format, res.totalMatches, res.hits.size, res.elapsedMs.toInt())
        else getString(R.string.sr_summary_names, res.hits.size, res.elapsedMs.toInt())
        tv.text = if (res.truncated) base + getString(R.string.sr_truncated) else base
    }

    private fun updateSearchChrome() {
        val input = findViewById<EditText>(R.id.inputSearchFiles)
        findViewById<View>(R.id.scrollSearchOptions).visibility =
            if (input.hasFocus() || searchQuery.isNotEmpty()) View.VISIBLE else View.GONE
    }

    private fun renderSearchOptions() {
        val box = findViewById<android.widget.LinearLayout>(R.id.layoutSearchOptions)
        box.removeAllViews()
        val d = resources.displayMetrics.density
        fun chip(label: String, on: Boolean, action: () -> Unit) {
            box.addView(TextView(this).apply {
                text = label
                textSize = 12.5f
                maxLines = 1
                gravity = android.view.Gravity.CENTER
                setTextColor(androidx.core.content.ContextCompat.getColor(
                    context, if (on) R.color.rin_files_on_accent else R.color.rin_files_chip_text))
                if (on) setTypeface(typeface, android.graphics.Typeface.BOLD)
                setPadding((14 * d).toInt(), (7 * d).toInt(), (14 * d).toInt(), (7 * d).toInt())
                setBackgroundResource(if (on) R.drawable.bg_file_chip_on else R.drawable.bg_file_chip)
                isClickable = true
                setOnClickListener { action() }
                layoutParams = android.widget.LinearLayout.LayoutParams(
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT
                ).apply { marginEnd = (6 * d).toInt() }
            })
        }
        fun change(o: SearchOptions) {
            searchOpts = o
            SearchPrefs.save(this, o)
            renderSearchOptions()
            if (searchQuery.isNotBlank()) runSearch(0L)
        }
        chip(getString(when (searchOpts.mode) {
            SearchMode.NAMES -> R.string.sr_mode_names
            SearchMode.CONTENT -> R.string.sr_mode_content
            SearchMode.BOTH -> R.string.sr_mode_both
        }), true) { change(searchOpts.copy(mode = searchOpts.mode.next())) }
        chip(getString(R.string.sr_chip_case), searchOpts.caseSensitive) { change(searchOpts.copy(caseSensitive = !searchOpts.caseSensitive)) }
        chip(getString(R.string.sr_chip_word), searchOpts.wholeWord) { change(searchOpts.copy(wholeWord = !searchOpts.wholeWord)) }
        chip(getString(R.string.sr_chip_regex), searchOpts.regex) { change(searchOpts.copy(regex = !searchOpts.regex)) }
        chip(getString(if (folderScope) R.string.sr_scope_folder else R.string.sr_scope_project), folderScope) {
            folderScope = !folderScope
            SearchPrefs.setFolderScope(this, folderScope)
            renderSearchOptions()
            if (searchQuery.isNotBlank()) runSearch(0L)
        }
        chip(getString(R.string.sr_filters), false) { showSearchFiltersHelp() }
        chip(getString(R.string.sr_history), false) { showSearchHistory() }
        val canReplace = (lastSearch?.hits?.any { it.totalMatches > 0 } == true) && searchOpts.mode != SearchMode.NAMES
        if (canReplace) chip(getString(R.string.sr_replace), false) { showReplaceDialog() }
    }

    private fun showSearchFiltersHelp() {
        AlertDialog.Builder(this)
            .setTitle(R.string.sr_filters_title)
            .setMessage(R.string.sr_filters_help)
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    private fun showSearchHistory() {
        val items = SearchHistory.list(this, project.name)
        if (items.isEmpty()) {
            Toast.makeText(this, R.string.sr_history_empty, Toast.LENGTH_SHORT).show()
            return
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.sr_history_title)
            .setItems(items.toTypedArray()) { _, i ->
                val input = findViewById<EditText>(R.id.inputSearchFiles)
                input.setText(items[i])
                input.setSelection(input.text.length)
            }
            .setNeutralButton(R.string.sr_history_clear) { _, _ -> SearchHistory.clear(this, project.name) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** نقر على نتيجة محتوى: مطابقة واحدة تُفتح مباشرة عند سطرها، وعدّة مطابقات تُعرض في قائمة. */
    private fun openSearchHit(hit: SearchHit) {
        if (searchQuery.isNotBlank()) SearchHistory.add(this, project.name, searchQuery)
        if (hit.matches.size <= 1) handleOpen(hit.file, hit.firstLine, hit.matches.firstOrNull()) else showMatchesDialog(hit)
    }

    private fun showMatchesDialog(hit: SearchHit) {
        val items = hit.matches.map { m -> SearchHighlight.line(this, "${m.line}:  ", m) }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.sr_matches_title, hit.file.name, hit.totalMatches))
            .setItems(items) { _, i -> handleOpen(hit.file, hit.matches[i].line, hit.matches[i]) }
            .setNeutralButton(R.string.sr_open_file) { _, _ -> handleOpen(hit.file, hit.firstLine, hit.matches.firstOrNull()) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showReplaceDialog() {
        val res = lastSearch ?: return
        val opts = currentSearchOptions()
        val content = res.hits.filter { it.totalMatches > 0 }
        if (content.isEmpty()) {
            Toast.makeText(this, R.string.sr_replace_none, Toast.LENGTH_SHORT).show()
            return
        }
        if (res.truncated) {
            Toast.makeText(this, R.string.sr_replace_blocked_truncated, Toast.LENGTH_LONG).show()
            return
        }
        val err = FileReplace.validate(opts)
        if (err != null) {
            Toast.makeText(
                this,
                if (err == "empty-match") getString(R.string.sr_replace_empty_match) else getString(R.string.sr_regex_error, err),
                Toast.LENGTH_LONG
            ).show()
            return
        }
        val d = resources.displayMetrics.density
        val input = EditText(this).apply {
            hint = getString(R.string.sr_replace_hint)
            setSingleLine(true)
        }
        val wrap = android.widget.LinearLayout(this).apply {
            setPadding((20 * d).toInt(), (8 * d).toInt(), (20 * d).toInt(), 0)
            addView(input, android.widget.LinearLayout.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT
            ))
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.sr_replace_title)
            .setMessage(getString(R.string.sr_replace_confirm, content.sumOf { it.totalMatches }, content.size))
            .setView(wrap)
            .setPositiveButton(R.string.sr_replace_apply) { _, _ -> performReplace(content, opts, input.text.toString()) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun performReplace(hits: List<SearchHit>, opts: SearchOptions, replacement: String) {
        val skip = protectedPaths()
        Thread {
            val out = try { FileReplace.apply(hits, opts, replacement, skip) } catch (t: Throwable) { null }
            runOnUiThread {
                if (out == null) {
                    Toast.makeText(this, R.string.file_save_error, Toast.LENGTH_LONG).show()
                    return@runOnUiThread
                }
                refresh()
                showReplaceResult(out)
            }
        }.start()
    }

    private fun showReplaceResult(out: FileReplace.Outcome) {
        val msg = StringBuilder(getString(R.string.sr_replace_done, out.replacements, out.files))
        if (out.skipped.isNotEmpty()) msg.append('\n').append(getString(R.string.sr_replace_skipped, out.skipped.size))
        val b = AlertDialog.Builder(this).setMessage(msg.toString()).setPositiveButton(android.R.string.ok, null)
        if (out.backups.isNotEmpty()) {
            b.setNegativeButton(R.string.sr_replace_undo) { _, _ ->
                val n = FileReplace.undo(out.backups)
                refresh()
                Toast.makeText(this, getString(R.string.sr_replace_undone, n), Toast.LENGTH_SHORT).show()
            }
        }
        b.show()
    }

    private fun selectedEntries(): List<FsEntry> = adapter.entries().filter { it.relPath in selection }

    /** ملفات مفتوحة بتعديلات غير محفوظة في المحرر: لا تُحذف/تُنقل ضمن عملية دُفعة. */
    private fun protectedPaths(): Set<String> = EditorDirtyState.dirtyPaths(project.name)

    private fun onSelectionChanged() {
        adapter.refreshSelection()
        updateSelectionBar()
    }

    private fun updateSelectionBar() {
        val active = selection.active
        selectionBar.isVisible = active
        if (active) setFabOpen(false, animate = false)
        fabContainer.isVisible = !active
        findViewById<View>(R.id.rowFabPaste).isVisible = FileClipboard.hasContent
        if (active) txtSelectionCount.text = getString(R.string.selection_count_format, selection.count)
    }

    private fun clearSelection() {
        if (!selection.active) return
        selection.clear()
        onSelectionChanged()
    }

    private fun selectAllVisible() {
        adapter.entries().forEach { selection.add(it.relPath) }
        onSelectionChanged()
    }

    private fun selectByPattern() {
        PatternSelect.ask(this) { rx ->
            adapter.entries().filter { rx.matches(it.name) }.forEach { selection.add(it.relPath) }
            onSelectionChanged()
        }
    }

    private fun invertSelection() {
        adapter.entries().forEach { selection.toggle(it.relPath) }
        onSelectionChanged()
    }

    private fun renameEntry(entry: FsEntry) = when (entry) {
        is FsEntry.FileEntry -> showRenameFileDialog(entry.file)
        is FsEntry.FolderEntry -> showRenameFolderDialog(entry.folder)
    }

    private fun deleteEntries(entries: List<FsEntry>) {
        FileBatchOps.confirmDelete(this, entries, protectedPaths(), project.name) {
            selection.clear()
            refresh()
        }
    }

    private fun handleBack() {
        if (fabOpen) {
            setFabOpen(false)
            return
        }
        if (selection.active) {
            clearSelection()
            return
        }
        if (currentRelDir.isBlank()) {
            finish()
        } else {
            currentRelDir = currentRelDir.substringBeforeLast('/', "")
            refresh()
        }
    }

    /** Android 16 (targetSdk 36) لا يستدعي onBackPressed() إطلاقاً — نستخدم OnBackPressedCallback. */
    private val backCallback = object : androidx.activity.OnBackPressedCallback(true) {
        override fun handleOnBackPressed() = handleBack() // في الجذر يستدعي finish()
    }

    private fun enterFolder(folder: RinFolder) {
        currentRelDir = folder.relPath
        refresh()
    }

    private fun handleOpen(file: RinFile, line: Int = 0, m: SearchMatch? = null) {
        FileRecents.record(this, project.name, file.relPath)
        when {
            ProjectManager.isImageFile(file.name) || ProjectManager.isVideoFile(file.name) ->
                openInMediaPreview(file)
            // حزمة RinPak داخل المشروع: معاينة/استيراد داخل التطبيق (لا نص خام ولا تطبيق خارجي).
            file.name.endsWith(RinPak.EXTENSION, ignoreCase = true) || file.name.endsWith(".rinproj", ignoreCase = true) ->
                openRinPak(file)
            ProjectManager.isBinaryFile(file.name) -> openExternally(file)
            else -> openInEditor(file, line, m)
        }
    }

    /** يمرّر ملف الحزمة إلى شاشة المشاريع (نفس مسار ACTION_VIEW الخارجي): معاينة ← تحقق ← استيراد. */
    private fun openRinPak(file: RinFile) {
        try {
            val uri: Uri = FileProvider.getUriForFile(this, "$packageName.fileprovider", file.file)
            startActivity(
                Intent(this, ProjectsActivity::class.java).apply {
                    action = Intent.ACTION_VIEW
                    data = uri
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
            )
        } catch (e: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${e.message}", Toast.LENGTH_LONG).show()
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

    private val anyMime = arrayOf("*/*")

    private fun showUploadHub() {
        fun item(icon: Int, label: Int, action: () -> Unit) = IconMenu.Item(icon, getString(label), action)
        IconMenu.show(this, getString(R.string.hub_upload_title), listOf(
            item(R.drawable.fx_upload, R.string.up_single) { importFileLauncher.launch(anyMime) },
            item(R.drawable.fx_upload_multi, R.string.up_multi) { importMultiLauncher.launch(anyMime) },
            item(R.drawable.fx_upload_folder, R.string.up_folder) { importTreeLauncher.launch(null) },
            item(R.drawable.fx_zip, R.string.zip_upload_extract) {
                importZipLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed", "*/*"))
            },
            item(R.drawable.fx_zip_list, R.string.zip_browse_device) {
                browseZipLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed", "*/*"))
            },
            item(R.drawable.rs_ic_download, R.string.dl_from_url) {
                UrlDownloader.show(this, project, currentRelDir) { refresh() }
            },
            item(R.drawable.fx_repo, R.string.repo_menu_restore) {
                RepoRestore.showIntoProject(this, project, currentRelDir) { refresh() }
            }
        ))
    }

    private fun showMediaHub() {
        val on = Uploader.shrinkEnabled(this)
        IconMenu.show(this, getString(R.string.hub_media_title), listOf(
            IconMenu.Item(R.drawable.fx_media_upload, getString(R.string.media_single)) {
                importMediaLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
            },
            IconMenu.Item(R.drawable.fx_media_multi, getString(R.string.media_multi)) {
                importMultiMediaLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
            },
            IconMenu.Item(R.drawable.fx_audio, getString(R.string.media_audio)) {
                importMultiLauncher.launch(arrayOf("audio/*"))
            },
            IconMenu.Item(R.drawable.fx_resize, getString(if (on) R.string.media_shrink_on else R.string.media_shrink_off)) {
                Uploader.setShrink(this, !on)
                Toast.makeText(this, if (!on) R.string.media_shrink_on else R.string.media_shrink_off, Toast.LENGTH_SHORT).show()
            }
        ))
    }

    private fun showZipToolsDialog() {
        fun item(icon: Int, label: String, action: () -> Unit) = IconMenu.Item(icon, label, action)
        IconMenu.show(this, getString(R.string.project_tools_title), listOf(
            item(R.drawable.fx_zip, getString(R.string.zip_upload_extract)) {
                importZipLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed", "*/*"))
            },
            item(R.drawable.fx_zip_list, getString(R.string.zip_browse_device)) {
                browseZipLauncher.launch(arrayOf("application/zip", "application/x-zip-compressed", "*/*"))
            },
            item(R.drawable.fx_share, getString(R.string.export_project_action)) { downloadProjectAsZip() },
            item(R.drawable.rs_ic_download, getString(R.string.dl_project_zip)) { DownloadExporter.saveProject(this, project) },
            item(R.drawable.rs_ic_download, getString(R.string.dl_folder_zip)) { DownloadExporter.saveFolder(this, project, currentRelDir) },
            item(R.drawable.rs_ic_download, getString(R.string.dl_from_url)) {
                UrlDownloader.show(this, project, currentRelDir) { refresh() }
            },
            item(R.drawable.fx_repo, getString(R.string.repo_menu_restore)) {
                RepoRestore.showIntoProject(this, project, currentRelDir) { refresh() }
            },
            item(R.drawable.fx_repo, getString(R.string.repo_menu_update)) {
                RepoRestore.updateFromSource(this, project, currentRelDir) { refresh() }
            },
            item(R.drawable.fx_storage, getString(R.string.tool_storage)) { StorageAnalyzer.show(this, project) },
            item(R.drawable.fx_dups, getString(R.string.tool_dups)) { DuplicateFinder.show(this, project, protectedPaths()) { refresh() } },
            item(R.drawable.fx_tree, getString(R.string.tool_copy_tree)) { TreeExport.copy(this, project) },
            item(R.drawable.fx_eye, getString(if (FileFilter.showHidden(this)) R.string.tool_hidden_hide else R.string.tool_hidden_show)) {
                FileFilter.setShowHidden(this, !FileFilter.showHidden(this)); refresh()
            },
            item(R.drawable.fx_jump, getString(R.string.tool_jump)) {
                FolderJump.show(this, project) { rel ->
                    currentRelDir = rel
                    val search = findViewById<EditText>(R.id.inputSearchFiles)
                    if (search.text.isNotEmpty()) search.setText("") else refresh()
                }
            },
            item(R.drawable.fx_findreplace, getString(R.string.tool_findreplace)) {
                FindReplace.show(this, project, protectedPaths()) { refresh() }
            },
            item(R.drawable.fx_empty, getString(R.string.tool_empty)) {
                EmptyFiles.show(this, project, protectedPaths()) { refresh() }
            }
        ))
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

    /** تصدير المشروع كحزمة RinPak: حوار بيانات وصفية ثم مشاركة أو حفظ في التنزيلات ([RinPakActions]). */
    private fun downloadProjectAsZip() = RinPakActions.startExport(this, project)

    override fun onResume() {
        super.onResume()
        refresh()
    }

    /** يزداد مع كل refresh() جديد؛ يمنع نتيجة فحص أخطاء خلفي متأخرة (لمجلد/بحث سابق غادره
     *  المستخدم فعلاً) من الكتابة فوق نتائج القائمة الحالية. */
    private var scanToken: Int = 0

    private fun refresh() {
        renderBreadcrumb()
        if (searchQuery.isNotBlank()) {
            findViewById<View>(R.id.txtFilesSummary).visibility = View.VISIBLE
            runSearch(0L)
            return
        }
        cancelSearch()
        renderSearchOptions()
        txtEmptyMessage.setText(R.string.no_files_yet)
        val (rawFolders, rawFiles) = ProjectManager.listEntries(project, currentRelDir)
        val pins = FilePins.get(this, project.name)
        val folders = FileFilter.applyFolders(this, FileSort.folders(this, rawFolders, pins))
        val files = FileFilter.apply(this, FileSort.files(this, rawFiles, pins))
        adapter.setPinned(pins)
        adapter.setSearchHits(emptyMap())
        renderSummary(rawFolders.size, rawFiles.size, rawFiles.sumOf { it.sizeBytes })
        adapter.submit(folders, files)
        pruneSelection()
        adapter.setDirtyPaths(EditorDirtyState.dirtyPaths(project.name))
        updateEmptyState()
        scanForErrors(files)

        findViewById<TextView>(R.id.txtToolbarSubtitle).text =
            if (currentRelDir.isBlank()) project.name else "${project.name} / $currentRelDir"
    }

    /** يزيل من التحديد أي عنصر لم يعد ظاهراً (تغيير مجلد/بحث/حذف) ويحدّث الشريط. */
    private fun pruneSelection() {
        if (!::selectionBar.isInitialized) return
        selection.retainOnly(adapter.entries().map { it.relPath }.toSet())
        updateSelectionBar()
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

    private fun showRowMenu(entry: FsEntry, anchor: View) {
        RowMenu.show(this, anchor, project, entry, currentRelDir, protectedPaths()) { refresh() }
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

    private fun showDeleteFileConfirm(file: RinFile) = deleteEntries(listOf(FsEntry.FileEntry(file)))

    private fun showDeleteFolderConfirm(folder: RinFolder) = deleteEntries(listOf(FsEntry.FolderEntry(folder)))

    private fun openInEditor(file: RinFile, line: Int = 0, m: SearchMatch? = null) {
        val intent = Intent(this, MainActivity::class.java)
        if (line > 0) intent.putExtra(MainActivity.EXTRA_GOTO_LINE, line)
        if (m != null) {
            intent.putExtra(MainActivity.EXTRA_GOTO_COL, m.col)
            intent.putExtra(MainActivity.EXTRA_GOTO_END_COL, m.endCol)
        }
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
    val onMore: (FsEntry, View) -> Unit,
    private val selection: SelectionModel,
    private val onSelectionChanged: () -> Unit
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    private companion object {
        const val TYPE_FOLDER = 0
        const val TYPE_FILE = 1
        /** حمولة تحديث جزئي: إعادة رسم حالة التحديد فقط دون إعادة ربط الصف كاملاً. */
        const val PAYLOAD_SELECTION = "selection"
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

    private var allItems: List<FileRow> = emptyList()
    private var statusMode: StatusMode = StatusMode.ALL
    /** يُستدعى بعد تغيّر العناصر الظاهرة بسبب التصفية/نتيجة فحص الأخطاء (لتحديث حالة "فارغ"). */
    var onVisibleChanged: (() -> Unit)? = null

    private fun keyOf(r: FileRow): String = when (r) {
        is FileRow.FolderRow -> r.folder.relPath
        is FileRow.FileRowItem -> r.file.relPath
    }

    /** يبني القائمة الظاهرة من الكاملة حسب شريحة التصفية الحالية. */
    private fun rebuild() {
        items = when (statusMode) {
            StatusMode.ALL -> allItems
            StatusMode.ERRORS -> allItems.filter { it is FileRow.FileRowItem && it.file.relPath in errorPaths }
            StatusMode.UNSAVED -> allItems.filter { it is FileRow.FileRowItem && it.file.relPath in dirtyPaths }
            StatusMode.PINNED -> allItems.filter { keyOf(it) in pinned }
        }
    }

    fun setStatusMode(mode: StatusMode) {
        statusMode = mode
        rebuild()
        notifyDataSetChanged()
        onVisibleChanged?.invoke()
    }

    fun submit(folders: List<RinFolder>, files: List<RinFile>, searchMode: Boolean = false) {
        this.searchMode = searchMode
        allItems = folders.map { FileRow.FolderRow(it) } + files.map { FileRow.FileRowItem(it) }
        rebuild()
        notifyDataSetChanged()
    }

    /** مفتاح التحديد (المسار النسبي) للصف عند [position]. */
    fun keyAt(position: Int): String? = when (val r = items.getOrNull(position)) {
        is FileRow.FolderRow -> r.folder.relPath
        is FileRow.FileRowItem -> r.file.relPath
        null -> null
    }

    fun entryAt(position: Int): FsEntry? = when (val r = items.getOrNull(position)) {
        is FileRow.FolderRow -> FsEntry.FolderEntry(r.folder)
        is FileRow.FileRowItem -> FsEntry.FileEntry(r.file)
        null -> null
    }

    fun entries(): List<FsEntry> = items.map {
        when (it) {
            is FileRow.FolderRow -> FsEntry.FolderEntry(it.folder)
            is FileRow.FileRowItem -> FsEntry.FileEntry(it.file)
        }
    }

    /** يعيد رسم حالة التحديد (خلفية + شارة ✓ + إخفاء أزرار الصف) لكل الصفوف بلا إعادة ربط كاملة. */
    fun refreshSelection() {
        if (items.isNotEmpty()) notifyItemRangeChanged(0, items.size, PAYLOAD_SELECTION)
    }

    private fun bindSelectionState(holder: RecyclerView.ViewHolder, position: Int) {
        val key = keyAt(position) ?: return
        val selected = key in selection
        val selecting = selection.active
        applyDensity(holder)
        val actionVis = if (selecting) View.GONE else View.VISIBLE
        holder.itemView.setBackgroundResource(
            if (holder is FolderVH) {
                if (selected) R.drawable.bg_folder_card_selected else R.drawable.bg_folder_card
            } else {
                if (selected) R.drawable.bg_file_card_selected else R.drawable.bg_file_card
            }
        )
        when (holder) {
            is FolderVH -> {
                holder.imgCheck.visibility = if (selected) View.VISIBLE else View.GONE
                holder.btnMore.visibility = actionVis
                holder.btnRename.visibility = actionVis
                holder.btnDelete.visibility = actionVis
            }
            is FileVH -> {
                holder.imgCheck.visibility = if (selected) View.VISIBLE else View.GONE
                holder.btnMore.visibility = actionVis
                holder.btnRename.visibility = actionVis
                holder.btnDelete.visibility = actionVis
            }
        }
    }

    private fun toggle(key: String) {
        selection.toggle(key)
        onSelectionChanged()
    }

    private var pinned: Set<String> = emptySet()
    private var compact: Boolean = false
    /** نتائج البحث المتقدم: relPath ← الاسم/المطابقات (تمييز + مقتطف السطر) — انظر FileSearch.kt. */
    private var searchHits: Map<String, SearchHit> = emptyMap()
    /** نقر على نتيجة فيها مطابقات محتوى (يفتح عند السطر أو قائمة المطابقات). */
    var onOpenHit: ((SearchHit) -> Unit)? = null

    fun setSearchHits(h: Map<String, SearchHit>) { searchHits = h }

    fun setCompact(value: Boolean) {
        compact = value
        notifyDataSetChanged()
    }

    /** العرض المضغوط: حشوة رأسية أصغر وإخفاء سطر المعلومات (الحجم/التاريخ). */
    private fun applyDensity(holder: RecyclerView.ViewHolder) {
        val d = holder.itemView.resources.displayMetrics.density
        val ph = (12 * d).toInt()
        val pv = ((if (compact) 5 else 12) * d).toInt()
        holder.itemView.setPadding(ph, pv, ph, pv)
        val vis = if (compact && !searchMode) View.GONE else View.VISIBLE
        when (holder) {
            is FolderVH -> holder.txtMeta.visibility = vis
            is FileVH -> holder.txtMeta.visibility = vis
        }
    }

    /** المثبّتات (FilePins): تُعرض نجمة قبل الاسم. تُستدعى قبل [submit] فلا حاجة لإشعار. */
    fun setPinned(paths: Set<String>) { pinned = paths }

    private fun applyPin(tv: TextView, key: String) {
        if (key in pinned) RsIcons.start(tv, R.drawable.rs_ic_star, 14, 4, androidx.core.content.ContextCompat.getColor(tv.context, R.color.rin_files_accent))
        else RsIcons.clear(tv)
    }

    fun setErrorPaths(paths: Set<String>) {
        errorPaths = paths
        if (statusMode == StatusMode.ERRORS) rebuild()
        notifyDataSetChanged()
        if (statusMode == StatusMode.ERRORS) onVisibleChanged?.invoke()
    }

    fun setDirtyPaths(paths: Set<String>) {
        dirtyPaths = paths
        if (statusMode == StatusMode.UNSAVED) rebuild()
        notifyDataSetChanged()
        if (statusMode == StatusMode.UNSAVED) onVisibleChanged?.invoke()
    }

    class FolderVH(view: View) : RecyclerView.ViewHolder(view) {
        val imgIcon: android.widget.ImageView = view.findViewById(R.id.imgFolderIcon)
        val imgCheck: View = view.findViewById(R.id.imgFolderSelectCheck)
        val txtName: TextView = view.findViewById(R.id.txtFolderNameItem)
        val txtMeta: TextView = view.findViewById(R.id.txtFolderMeta)
        val txtCount: TextView = view.findViewById(R.id.txtFolderCount)
        val btnMore: View = view.findViewById(R.id.btnMoreFolder)
        val btnRename: View = view.findViewById(R.id.btnRenameFolder)
        val btnDelete: View = view.findViewById(R.id.btnDeleteFolder)
    }

    class FileVH(view: View) : RecyclerView.ViewHolder(view) {
        val imgIcon: android.widget.ImageView = view.findViewById(R.id.imgFileIcon)
        val imgCheck: View = view.findViewById(R.id.imgFileSelectCheck)
        val txtErrorBadge: View = view.findViewById(R.id.txtErrorBadge)
        val txtTypeBadge: TextView = view.findViewById(R.id.txtFileTypeBadge)
        val imgPlay: View = view.findViewById(R.id.imgFilePlay)
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

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int, payloads: MutableList<Any>) {
        if (payloads.contains(PAYLOAD_SELECTION)) {
            bindSelectionState(holder, position)
        } else {
            super.onBindViewHolder(holder, position, payloads)
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        when (val row = items[position]) {
            is FileRow.FolderRow -> {
                val folder = row.folder
                holder as FolderVH
                holder.txtName.text = folder.name
                applyPin(holder.txtName, folder.relPath)
                holder.imgIcon.setImageResource(RsFolderIcons.forName(folder.name))
                val childCount = folder.dir.list()?.size ?: 0
                holder.txtCount.text = if (childCount > 99) "99+" else childCount.toString()
                holder.txtCount.visibility = if (childCount > 0) View.VISIBLE else View.GONE
                holder.txtMeta.text = holder.itemView.context.getString(
                    R.string.folder_meta_count_format,
                    childCount,
                    dateFormat.format(Date(folder.lastModified))
                )
                holder.itemView.setOnClickListener {
                    if (selection.active) toggle(folder.relPath) else onOpenFolder(folder)
                }
                holder.btnMore.setOnClickListener { onMore(FsEntry.FolderEntry(folder), holder.btnMore) }
                holder.btnRename.setOnClickListener { onRenameFolder(folder) }
                holder.btnDelete.setOnClickListener { onDeleteFolder(folder) }
                bindSelectionState(holder, position)
            }
            is FileRow.FileRowItem -> {
                val file = row.file
                holder as FileVH
                val hit = searchHits[file.relPath]
                val sctx = holder.itemView.context
                holder.txtName.text =
                    if (searchMode && hit != null) SearchHighlight.name(sctx, file.name, hit.nameRanges) else file.name
                applyPin(holder.txtName, file.relPath)
                holder.txtMeta.text = if (searchMode && hit != null && hit.matches.isNotEmpty()) {
                    val dir = file.relPath.substringBeforeLast('/', "")
                    val head = (if (dir.isEmpty()) "" else "$dir/  ") + sctx.getString(R.string.sr_line_prefix, hit.firstLine, "")
                    val extra = hit.totalMatches - 1
                    SearchHighlight.line(sctx, head, hit.matches[0],
                        if (extra > 0) "   " + sctx.getString(R.string.sr_more_matches, extra) else "")
                } else if (searchMode) {
                    holder.itemView.context.getString(R.string.search_result_meta_format, file.relPath)
                } else {
                    holder.itemView.context.getString(
                        R.string.file_meta_format,
                        formatSize(file.sizeBytes),
                        dateFormat.format(Date(file.lastModified))
                    )
                }
                // مصغّرة الصور/الفيديو + لصيقة النوع؛ الأبعاد أو المدة تُضاف أمام الحجم والتاريخ فور جاهزيتها.
                val baseMeta = holder.txtMeta.text
                val metaOwner = file.file.absolutePath
                holder.txtMeta.tag = metaOwner
                val infoCallback: (String) -> Unit = { info ->
                    if (holder.txtMeta.tag == metaOwner) holder.txtMeta.text = "$info • $baseMeta"
                }
                FileRowStyle.bind(holder.imgIcon, holder.txtTypeBadge, holder.imgPlay, file.file,
                    if (searchMode) null else infoCallback)
                holder.txtErrorBadge.visibility = if (file.relPath in errorPaths) View.VISIBLE else View.GONE
                holder.txtDirtyDot.visibility = if (file.relPath in dirtyPaths) View.VISIBLE else View.GONE
                holder.itemView.setOnClickListener {
                    if (selection.active) toggle(file.relPath)
                    else if (searchMode && hit != null && hit.totalMatches > 0) onOpenHit?.invoke(hit)
                    else onOpenFile(file)
                }
                holder.btnMore.setOnClickListener { onMore(FsEntry.FileEntry(file), holder.btnMore) }
                holder.btnRename.setOnClickListener { onRenameFile(file) }
                holder.btnDelete.setOnClickListener { onDeleteFile(file) }
                bindSelectionState(holder, position)
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

/** تمييز مواضع المطابقة (خلفية + خط عريض) في اسم الملف ومقتطف السطر. */
internal object SearchHighlight {
    private fun bg(ctx: android.content.Context): Int {
        val c = androidx.core.content.ContextCompat.getColor(ctx, R.color.rin_files_accent_fill)
        return (c and 0x00FFFFFF) or (0x66 shl 24)
    }

    private fun mark(ctx: android.content.Context, sp: android.text.Spannable, s: Int, e: Int) {
        if (s < 0 || e > sp.length || s >= e) return
        sp.setSpan(android.text.style.BackgroundColorSpan(bg(ctx)), s, e, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        sp.setSpan(android.text.style.StyleSpan(android.graphics.Typeface.BOLD), s, e, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    fun name(ctx: android.content.Context, text: String, ranges: List<IntRange>): CharSequence {
        if (ranges.isEmpty()) return text
        val sp = android.text.SpannableString(text)
        for (r in ranges) mark(ctx, sp, r.first, r.last + 1)
        return sp
    }

    fun line(ctx: android.content.Context, prefix: String, m: SearchMatch, suffix: String = ""): CharSequence {
        val sb = android.text.SpannableStringBuilder(prefix).append(m.preview)
        mark(ctx, sb, prefix.length + m.hlStart, prefix.length + m.hlEnd)
        if (suffix.isNotEmpty()) sb.append(suffix)
        return sb
    }
}
