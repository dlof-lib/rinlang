package com.dlof.rinlang

import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.CheckBox
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat

/**
 * معالج «مشروع جديد» الاحترافي — 4 خطوات: القالب ← التفاصيل (+ خيارات القالب) ← المكتبات ← المراجعة.
 *
 *  - التحقق من الاسم لحظياً (فراغ/رموز/طول/تكرار) قبل الانتقال.
 *  - مكتبات Rin المدمجة ([BuiltinLibraries]) بالبحث، مع توصيات حسب القالب؛ المختارة تُستورد أعلى main.rin.
 *  - بنية مشروع مرتبة (README/.gitignore/src/tests/docs/assets/rin.project.json) بمعاينة شجرية حيّة
 *    مصدرها [ProjectBlueprint] (منطق خالص مُختبَر).
 *  - مشاريع الويب (HTML / HTML+JS) تتخطّى خطوة المكتبات لأنها لا تستخدم @import.
 *
 * النتيجة: setResult(RESULT_OK, EXTRA_CREATED=اسم المشروع) فتفتحه ProjectsActivity مباشرة.
 * الواجهة مبنية برمجياً (لا XML جديد) لتبقى الوحدة مكتفية بذاتها.
 */
class NewProjectActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_CREATED = "created_project_name"
        private const val PREFS = "new_project_wizard"
        private const val KEY_AUTHOR = "author"
    }

    private enum class Step { TEMPLATE, DETAILS, LIBS, REVIEW }

    private class TypeMeta(val type: ProjectType, val title: Int, val desc: Int, val icon: Int, val color: Int)

    private val rinTypes = listOf(
        TypeMeta(ProjectType.FREE, R.string.project_type_free, R.string.project_type_free_desc, R.drawable.ic_type_free, R.color.project_type_free_color),
        TypeMeta(ProjectType.CONTAINER, R.string.project_type_container, R.string.project_type_container_desc, R.drawable.ic_type_container, R.color.project_type_container_color),
        TypeMeta(ProjectType.TABLE, R.string.project_type_table, R.string.project_type_table_desc, R.drawable.ic_type_table, R.color.project_type_table_color),
        TypeMeta(ProjectType.UI, R.string.project_type_ui, R.string.project_type_ui_desc, R.drawable.ic_type_ui, R.color.project_type_ui_color),
        TypeMeta(ProjectType.ILLUST, R.string.project_type_illust, R.string.project_type_illust_desc, R.drawable.ic_illust_file, R.color.project_type_illust_color)
    )
    private val webTypes = listOf(
        TypeMeta(ProjectType.HTML, R.string.project_type_html, R.string.project_type_html_desc, R.drawable.ic_type_html, R.color.project_type_html_color),
        TypeMeta(ProjectType.HTML_JS, R.string.project_type_html_js, R.string.project_type_html_js_desc, R.drawable.ic_type_html, R.color.project_type_html_js_color)
    )

    // ---- الحالة ----
    private var stepIndex = 0
    private var type = ProjectType.FREE
    private var name = ""
    private var description = ""
    private var author = ""
    private var version = "0.1.0"
    private var nameError: String? = null

    private var uiTopBar = true
    private var uiSidebar = true
    private var uiBottomNav = false
    private var uiPrimary = 0xFF7C5CFF.toInt()
    private var containerVar = "counter"
    private var containerValue = "0"
    private var tableCols = 2
    private var tableRows = 1
    private var freeTemplate = "greeting"
    private var indsinTemplate: String? = null

    // ---- الحفظ على الهاتف ----
    private var savePhone = false
    private var saveTree: Uri? = null
    private var saveLabel = ""
    private var saveAuto = true
    private var saveZip = false
    @Volatile private var saveNote: String? = null

    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            ExternalProjectSaver.persist(this, uri)
            if (ExternalProjectSaver.check(this, uri) == ExternalProjectSaver.Check.OK) {
                saveTree = uri; savePhone = true
                saveLabel = ExternalProjectSaver.folderLabel(this, uri)
                ExternalProjectSaver.setDefaultTree(this, uri)
            } else {
                Toast.makeText(this, R.string.wiz_save_not_writable, Toast.LENGTH_LONG).show()
                if (saveTree == null) savePhone = false
            }
        } else if (saveTree == null) savePhone = false
        render()
    }

    private val selectedLibs = LinkedHashSet<String>()
    private var libsTouched = false
    private var libQuery = ""
    private var layout = ProjectBlueprint.Layout()

    // ---- العناصر ----
    private lateinit var stepLabel: TextView
    private lateinit var progress: ProgressBar
    private lateinit var page: LinearLayout
    private lateinit var scroll: ScrollView
    private lateinit var btnBack: TextView
    private lateinit var btnNext: TextView

    private val steps: List<Step>
        get() = if (type == ProjectType.HTML || type == ProjectType.HTML_JS)
            listOf(Step.TEMPLATE, Step.DETAILS, Step.REVIEW)
        else listOf(Step.TEMPLATE, Step.DETAILS, Step.LIBS, Step.REVIEW)

    private val d by lazy { resources.displayMetrics.density }
    private fun dp(v: Int) = (v * d).toInt()
    private fun col(id: Int) = ContextCompat.getColor(this, id)
    private val cBg get() = col(R.color.rin_background)
    private val cText get() = col(R.color.rin_on_toolbar)
    private val cDim get() = col(R.color.rin_editor_hint)
    private val cLine get() = col(R.color.rin_divider)
    private val cAccent get() = col(R.color.project_type_free_color)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
        author = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_AUTHOR, "") ?: ""
        applyRecommendedLibs()
        saveTree = ExternalProjectSaver.defaultTree(this)?.takeIf { ExternalProjectSaver.hasPermission(this, it) }
        saveLabel = saveTree?.let { ExternalProjectSaver.folderLabel(this, it) }.orEmpty()
        savePhone = saveTree != null && ExternalProjectSaver.defaultEnabled(this)
        saveAuto = ExternalProjectSaver.autoDefault(this); saveZip = ExternalProjectSaver.zipDefault(this)
        setContentView(buildFrame())
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() { if (stepIndex > 0) go(stepIndex - 1) else finish() }
        })
        render()
    }

    // ------------------------------------------------------------------ الإطار

    private fun buildFrame(): View {
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(cBg) }

        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(6))
        }
        val close = TextView(this).apply {
            text = "✕"; textSize = 20f; setTextColor(cText); setPadding(dp(4), dp(4), dp(14), dp(4))
            setOnClickListener { finish() }
        }
        bar.addView(close)
        bar.addView(TextView(this).apply {
            text = getString(R.string.wiz_title); textSize = 20f; setTextColor(cText); typeface = Typeface.DEFAULT_BOLD
        })
        root.addView(bar)

        stepLabel = TextView(this).apply { textSize = 13f; setTextColor(cDim); setPadding(dp(20), 0, dp(20), dp(6)) }
        root.addView(stepLabel)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 100
            progressTintList = android.content.res.ColorStateList.valueOf(cAccent)
        }
        root.addView(progress, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(4)).apply {
            marginStart = dp(20); marginEnd = dp(20)
        })

        scroll = ScrollView(this).apply { isFillViewport = true }
        page = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(16), dp(20), dp(24)) }
        scroll.addView(page, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))

        val bottom = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; setPadding(dp(16), dp(10), dp(16), dp(14))
        }
        btnBack = button(getString(R.string.wiz_back), filled = false) { if (stepIndex > 0) go(stepIndex - 1) else finish() }
        btnNext = button(getString(R.string.wiz_next), filled = true) { onNext() }
        bottom.addView(btnBack, LinearLayout.LayoutParams(0, dp(48), 1f).apply { marginEnd = dp(8) })
        bottom.addView(btnNext, LinearLayout.LayoutParams(0, dp(48), 1.4f))
        root.addView(bottom)
        return root
    }

    private fun button(text: String, filled: Boolean, onClick: () -> Unit) = TextView(this).apply {
        this.text = text; gravity = Gravity.CENTER; textSize = 15f; typeface = Typeface.DEFAULT_BOLD
        setTextColor(if (filled) Color.WHITE else cText)
        background = GradientDrawable().apply {
            cornerRadius = dp(12).toFloat()
            if (filled) setColor(cAccent) else { setColor(Color.TRANSPARENT); setStroke(dp(1), cLine) }
        }
        setOnClickListener { onClick() }
    }

    // ------------------------------------------------------------------ التنقل

    private fun go(i: Int) {
        stepIndex = i.coerceIn(0, steps.size - 1)
        render()
        scroll.scrollTo(0, 0)
    }

    private fun onNext() {
        when (steps[stepIndex]) {
            Step.DETAILS -> { if (!validateName()) { render(); return } }
            Step.REVIEW -> { create(); return }
            else -> {}
        }
        go(stepIndex + 1)
    }

    private fun validateName(): Boolean {
        val n = name.trim()
        nameError = when {
            ProjectBlueprint.nameProblem(n) == "empty" -> getString(R.string.wiz_name_empty)
            ProjectBlueprint.nameProblem(n) == "long" -> getString(R.string.wiz_name_long)
            ProjectBlueprint.nameProblem(n) != null || !ProjectManager.isValidProjectName(n) -> getString(R.string.wiz_name_chars)
            ProjectManager.projectExists(this, n) -> getString(R.string.wiz_name_exists)
            else -> null
        }
        return nameError == null
    }

    private fun render() {
        val list = steps
        if (stepIndex >= list.size) stepIndex = list.size - 1
        val s = list[stepIndex]
        val title = getString(when (s) {
            Step.TEMPLATE -> R.string.wiz_step_template
            Step.DETAILS -> R.string.wiz_step_details
            Step.LIBS -> R.string.wiz_step_libs
            Step.REVIEW -> R.string.wiz_step_review
        })
        stepLabel.text = getString(R.string.wiz_step_fmt, stepIndex + 1, list.size, title)
        progress.progress = ((stepIndex + 1) * 100) / list.size
        btnNext.text = getString(if (s == Step.REVIEW) R.string.wiz_create else R.string.wiz_next)
        page.removeAllViews()
        when (s) {
            Step.TEMPLATE -> renderTemplate()
            Step.DETAILS -> renderDetails()
            Step.LIBS -> renderLibs()
            Step.REVIEW -> renderReview()
        }
    }

    // ------------------------------------------------------------------ عناصر مساعدة

    private fun heading(text: String, topDp: Int = 14) = TextView(this).apply {
        this.text = text; textSize = 13f; setTextColor(cDim); typeface = Typeface.DEFAULT_BOLD
        setPadding(0, dp(topDp), 0, dp(6))
    }

    private fun addHeading(text: String, topDp: Int = 14) = page.addView(heading(text, topDp))

    private fun roundBg(fill: Int, stroke: Int, radiusDp: Int = 14, strokeDp: Int = 1) = GradientDrawable().apply {
        cornerRadius = dp(radiusDp).toFloat(); setColor(fill); setStroke(dp(strokeDp), stroke)
    }

    private fun field(hint: String, value: String, inputType: Int = InputType.TYPE_CLASS_TEXT, onChange: (String) -> Unit): EditText =
        EditText(this).apply {
            this.hint = hint; setText(value); this.inputType = inputType
            setTextColor(cText); setHintTextColor(cDim); textSize = 15f
            setSingleLine(true)
            background = roundBg(Color.TRANSPARENT, cLine, 10)
            setPadding(dp(14), dp(12), dp(14), dp(12))
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, a: Int) {}
                override fun onTextChanged(s: CharSequence?, st: Int, b: Int, c: Int) {}
                override fun afterTextChanged(s: Editable?) { onChange(s?.toString() ?: "") }
            })
        }

    private fun addField(f: View) = page.addView(f, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = dp(10) })

    private fun chipRow(options: List<Pair<String, String?>>, selected: String?, onSelect: (String?) -> Unit): View {
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val hs = android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(row) }
        for ((label, value) in options) {
            val on = value == selected
            row.addView(TextView(this).apply {
                text = label; textSize = 13.5f; setTextColor(if (on) Color.WHITE else cText)
                setPadding(dp(14), dp(8), dp(14), dp(8))
                background = roundBg(if (on) cAccent else Color.TRANSPARENT, if (on) cAccent else cLine, 18)
                setOnClickListener { onSelect(value) }
            }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(8) })
        }
        return hs
    }

    // ------------------------------------------------------------------ 1) القالب

    private fun renderTemplate() {
        addHeading(getString(R.string.create_section_rin), 0)
        for (m in rinTypes) addTypeCard(m)
        addHeading(getString(R.string.create_section_web))
        for (m in webTypes) addTypeCard(m)
    }

    private fun addTypeCard(m: TypeMeta) {
        val on = type == m.type
        val accent = col(m.color)
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(14), dp(12), dp(14), dp(12))
            background = roundBg(if (on) (accent and 0x00FFFFFF) or 0x22000000 else Color.TRANSPARENT, if (on) accent else cLine, 14, if (on) 2 else 1)
            setOnClickListener {
                if (type != m.type) {
                    type = m.type
                    if (!libsTouched) applyRecommendedLibs()
                    stepIndex = 0
                    render()
                }
            }
        }
        val icon = ImageView(this).apply { setImageResource(m.icon); setColorFilter(accent) }
        card.addView(icon, LinearLayout.LayoutParams(dp(34), dp(34)).apply { marginEnd = dp(14) })
        val texts = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        texts.addView(TextView(this).apply { text = getString(m.title); textSize = 16f; setTextColor(cText); typeface = Typeface.DEFAULT_BOLD })
        texts.addView(TextView(this).apply { text = getString(m.desc); textSize = 13f; setTextColor(cDim) })
        card.addView(texts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        if (on) card.addView(TextView(this).apply { text = "✓"; textSize = 20f; setTextColor(accent); typeface = Typeface.DEFAULT_BOLD })
        page.addView(card, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = dp(10) })
    }

    // ------------------------------------------------------------------ 2) التفاصيل

    private fun renderDetails() {
        addHeading(getString(R.string.wiz_name_label), 0)
        val nameField = field(getString(R.string.project_name_hint), name) { name = it; if (nameError != null) { nameError = null; errorView?.visibility = View.GONE } }
        addField(nameField)
        errorView = TextView(this).apply {
            setTextColor(0xFFE5484D.toInt()); textSize = 12.5f
            text = nameError ?: ""; visibility = if (nameError == null) View.GONE else View.VISIBLE
        }
        page.addView(errorView)
        addField(field(getString(R.string.wiz_description_hint), description) { description = it })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(field(getString(R.string.wiz_author_hint), author) { author = it }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.6f).apply { marginEnd = dp(8) })
        row.addView(field(getString(R.string.wiz_version_hint), version) { version = it }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        page.addView(row)

        when (type) {
            ProjectType.UI -> renderUiOptions()
            ProjectType.CONTAINER -> {
                addHeading(getString(R.string.wiz_options_section))
                addField(field(getString(R.string.wiz_container_var), containerVar) { containerVar = it })
                addField(field(getString(R.string.wiz_container_value), containerValue) { containerValue = it })
            }
            ProjectType.TABLE -> {
                addHeading(getString(R.string.wiz_options_section))
                addField(field(getString(R.string.wiz_table_columns), tableCols.toString(), InputType.TYPE_CLASS_NUMBER) { tableCols = (it.toIntOrNull() ?: 2).coerceIn(2, 6) })
                addField(field(getString(R.string.wiz_table_rows), tableRows.toString(), InputType.TYPE_CLASS_NUMBER) { tableRows = (it.toIntOrNull() ?: 1).coerceIn(1, 10) })
            }
            ProjectType.FREE -> {
                addHeading(getString(R.string.wiz_free_template))
                page.addView(chipRow(listOf(
                    getString(R.string.wiz_free_empty) to "empty",
                    getString(R.string.wiz_free_greeting) to "greeting",
                    getString(R.string.wiz_free_example) to "example"
                ), freeTemplate) { freeTemplate = it ?: "greeting"; render() })
            }
            else -> {}
        }
        if (type != ProjectType.HTML && type != ProjectType.HTML_JS) {
            addHeading(getString(R.string.indsin_template_label))
            page.addView(chipRow(listOf(
                getString(R.string.indsin_template_calculator) to "calculator",
                getString(R.string.indsin_template_blog) to "blog",
                getString(R.string.indsin_template_buttons) to "buttons",
                getString(R.string.indsin_template_gallery) to "gallery",
                getString(R.string.indsin_template_webview) to "webview"
            ), indsinTemplate) { picked -> indsinTemplate = if (picked == indsinTemplate) null else picked; render() })
        }
    }

    private var errorView: TextView? = null

    private fun renderUiOptions() {
        addHeading(getString(R.string.ui_design_section_title))
        fun toggle(label: String, on: Boolean, set: (Boolean) -> Unit) {
            page.addView(CheckBox(this).apply {
                text = label; isChecked = on; setTextColor(cText); textSize = 15f
                setOnCheckedChangeListener { _, v -> set(v) }
            })
        }
        toggle(getString(R.string.ui_design_topbar_label), uiTopBar) { uiTopBar = it }
        toggle(getString(R.string.ui_design_sidebar_label), uiSidebar) { uiSidebar = it }
        toggle(getString(R.string.ui_design_bottom_nav), uiBottomNav) { uiBottomNav = it }
        addHeading(getString(R.string.wiz_primary_color))
        val palette = listOf(R.color.ui_design_color_purple, R.color.ui_design_color_blue, R.color.ui_design_color_green,
            R.color.ui_design_color_amber, R.color.ui_design_color_pink, R.color.ui_design_color_cyan).map { col(it) }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val swatches = ArrayList<Int?>(palette)
        swatches.add(null)
        for (c in swatches) {
            val selected = c != null && (c and 0xFFFFFF) == (uiPrimary and 0xFFFFFF)
            val sw = View(this).apply {
                background = GradientDrawable().apply {
                    cornerRadius = dp(8).toFloat()
                    if (c != null) setColor(c) else { setColor(uiPrimary.takeIf { palette.none { p -> (p and 0xFFFFFF) == (uiPrimary and 0xFFFFFF) } } ?: Color.TRANSPARENT) }
                    setStroke(dp(if (selected) 3 else 1), if (selected) cText else cLine)
                }
                setOnClickListener {
                    if (c != null) { uiPrimary = c or (0xFF shl 24); render() }
                    else ColorPickerDialog.show(this@NewProjectActivity, uiPrimary) { picked -> uiPrimary = picked or (0xFF shl 24); render() }
                }
            }
            row.addView(sw, LinearLayout.LayoutParams(dp(38), dp(38)).apply { marginEnd = dp(10) })
        }
        page.addView(row)
    }

    // ------------------------------------------------------------------ 3) المكتبات

    private fun recommendedFor(t: ProjectType): List<String> = when (t) {
        ProjectType.UI -> listOf("twinkling-stars", "indsinicons")
        ProjectType.CONTAINER -> listOf("compose")
        else -> emptyList()
    }

    private fun applyRecommendedLibs() {
        selectedLibs.clear()
        val known = BuiltinLibraries.all.map { it.displayName }.toSet()
        for (n in recommendedFor(type)) if (n in known) selectedLibs.add(n)
    }

    private fun renderLibs() {
        val count = TextView(this).apply { textSize = 13f; setTextColor(cDim); setPadding(0, 0, 0, dp(8)) }
        fun updateCount() { count.text = getString(R.string.wiz_libs_selected_fmt, selectedLibs.size) }
        updateCount()
        page.addView(count)
        val listHolder = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        fun rebuild() {
            listHolder.removeAllViews()
            val q = libQuery.trim().lowercase()
            val rec = recommendedFor(type).toSet()
            val items = BuiltinLibraries.all
                .filter { q.isEmpty() || it.displayName.lowercase().contains(q) || it.description.lowercase().contains(q) }
                .sortedWith(compareBy<BuiltinLibraryInfo>({ it.displayName !in rec }, { it.displayName.lowercase() }))
            for (lib in items) {
                val row = LinearLayout(this).apply {
                    orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
                    setPadding(dp(12), dp(10), dp(12), dp(10))
                    background = roundBg(Color.TRANSPARENT, cLine, 12)
                }
                val cb = CheckBox(this).apply { isChecked = lib.displayName in selectedLibs; isClickable = false; isFocusable = false }
                row.addView(cb)
                row.addView(ImageView(this).apply { setImageResource(lib.iconRes) }, LinearLayout.LayoutParams(dp(28), dp(28)).apply { marginStart = dp(4); marginEnd = dp(10) })
                val tx = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
                val titleRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
                titleRow.addView(TextView(this).apply { text = lib.displayName; textSize = 15f; setTextColor(cText); typeface = Typeface.DEFAULT_BOLD })
                if (lib.displayName in rec) titleRow.addView(TextView(this).apply {
                    text = getString(R.string.wiz_libs_recommended); textSize = 10.5f; setTextColor(Color.WHITE)
                    setPadding(dp(8), dp(2), dp(8), dp(2)); background = roundBg(cAccent, cAccent, 8, 0)
                }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { marginStart = dp(8) })
                tx.addView(titleRow)
                tx.addView(TextView(this).apply { text = lib.description; textSize = 12f; setTextColor(cDim); maxLines = 2; ellipsize = android.text.TextUtils.TruncateAt.END })
                row.addView(tx, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
                row.setOnClickListener {
                    libsTouched = true
                    if (!selectedLibs.add(lib.displayName)) selectedLibs.remove(lib.displayName)
                    cb.isChecked = lib.displayName in selectedLibs
                    updateCount()
                }
                listHolder.addView(row, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = dp(8) })
            }
        }
        page.addView(field(getString(R.string.wiz_libs_search), libQuery) { libQuery = it; rebuild() }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = dp(10) })
        page.addView(listHolder)
        rebuild()
    }

    // ------------------------------------------------------------------ 4) المراجعة

    private fun libraryImportPaths(): List<String> {
        if (type == ProjectType.HTML || type == ProjectType.HTML_JS) return emptyList()
        return BuiltinLibraries.all.filter { it.displayName in selectedLibs }.map { it.importPath }
    }

    private fun blueprint() = ProjectBlueprint(
        name = name.trim(), typeId = type.id, description = description, author = author,
        version = version.trim().ifEmpty { "0.1.0" }, libraries = libraryImportPaths(), layout = layout,
        lang = if (java.util.Locale.getDefault().language == "ar") "ar" else "en"
    )

    private fun renderReview() {
        val meta = (rinTypes + webTypes).first { it.type == type }
        val summary = TextView(this).apply {
            textSize = 15f; setTextColor(cText)
            text = "${name.trim()}  ·  v${version.trim().ifEmpty { "0.1.0" }}\n${getString(meta.title)}" +
                (if (type != ProjectType.HTML && type != ProjectType.HTML_JS) "\n" + getString(R.string.wiz_libs_selected_fmt, selectedLibs.size) else "")
            setPadding(dp(14), dp(12), dp(14), dp(12)); background = roundBg(Color.TRANSPARENT, cLine, 12)
        }
        page.addView(summary)

        addHeading(getString(R.string.wiz_layout_section))
        val tree = TextView(this).apply {
            typeface = Typeface.MONOSPACE; textSize = 12.5f; setTextColor(cText)
            setPadding(dp(14), dp(12), dp(14), dp(12)); background = roundBg(Color.TRANSPARENT, cLine, 12)
            textDirection = View.TEXT_DIRECTION_LTR
        }
        fun refreshTree() { tree.text = ProjectBlueprint.renderTree(blueprint().previewPaths()).joinToString("\n") }
        fun toggle(label: String, on: Boolean, set: (Boolean) -> Unit) {
            page.addView(CheckBox(this).apply {
                text = label; isChecked = on; setTextColor(cText); textSize = 14.5f
                setOnCheckedChangeListener { _, v -> set(v); refreshTree() }
            })
        }
        val web = type == ProjectType.HTML || type == ProjectType.HTML_JS
        toggle(getString(R.string.wiz_layout_readme), layout.readme) { layout = layout.copy(readme = it) }
        toggle(getString(R.string.wiz_layout_gitignore), layout.gitignore) { layout = layout.copy(gitignore = it) }
        if (!web) {
            toggle(getString(R.string.wiz_layout_src), layout.srcModule) { layout = layout.copy(srcModule = it) }
            toggle(getString(R.string.wiz_layout_tests), layout.tests) { layout = layout.copy(tests = it) }
        }
        toggle(getString(R.string.wiz_layout_docs), layout.docs) { layout = layout.copy(docs = it) }
        toggle(getString(R.string.wiz_layout_assets), layout.assets) { layout = layout.copy(assets = it) }
        toggle(getString(R.string.wiz_layout_manifest), layout.manifest) { layout = layout.copy(manifest = it) }

        renderSaveSection()

        addHeading(getString(R.string.wiz_preview_tree))
        page.addView(tree)
        refreshTree()
    }

    // ------------------------------------------------------------------ الحفظ على الهاتف

    private fun smallButton(text: String, onClick: () -> Unit) = TextView(this).apply {
        this.text = text; textSize = 13.5f; setTextColor(cText)
        setPadding(dp(14), dp(8), dp(14), dp(8)); background = roundBg(Color.TRANSPARENT, cAccent, 10)
        setOnClickListener { onClick() }
    }

    private fun renderSaveSection() {
        addHeading(getString(R.string.wiz_save_section))
        page.addView(CheckBox(this).apply {
            text = getString(R.string.wiz_save_phone); isChecked = savePhone; setTextColor(cText); textSize = 14.5f
            setOnCheckedChangeListener { _, v ->
                savePhone = v
                if (v && saveTree == null) pickFolder.launch(null) else render()
            }
        })
        if (!savePhone) return
        val wrap = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(6) }
        page.addView(TextView(this).apply {
            textSize = 13.5f; setTextColor(cDim); textDirection = View.TEXT_DIRECTION_LTR
            text = "\uD83D\uDCC1 " + getString(R.string.wiz_save_path_fmt, "$saveLabel/${name.trim().ifEmpty { "…" }}")
        })
        page.addView(smallButton(getString(if (saveTree == null) R.string.wiz_save_choose else R.string.wiz_save_change)) { pickFolder.launch(saveTree) }, wrap)
        page.addView(CheckBox(this).apply {
            text = getString(R.string.wiz_save_auto); isChecked = saveAuto; setTextColor(cText); textSize = 14.5f
            setOnCheckedChangeListener { _, v -> saveAuto = v }
        })
        page.addView(CheckBox(this).apply {
            text = getString(R.string.wiz_save_zip); isChecked = saveZip; setTextColor(cText); textSize = 14.5f
            setOnCheckedChangeListener { _, v -> saveZip = v }
        })
    }

    // ------------------------------------------------------------------ الإنشاء

    private fun create() {
        if (!validateName()) { go(steps.indexOf(Step.DETAILS)); return }
        val bp = blueprint()
        val projectName = name.trim()
        var phoneTree: Uri? = null
        if (savePhone) {
            val t = saveTree
            if (t == null) { Toast.makeText(this, R.string.wiz_save_need_folder, Toast.LENGTH_LONG).show(); return }
            if (ExternalProjectSaver.check(this, t) != ExternalProjectSaver.Check.OK) {
                Toast.makeText(this, R.string.wiz_save_not_writable, Toast.LENGTH_LONG).show()
                saveTree = null; savePhone = false; render(); return
            }
            phoneTree = t
        }
        ExternalProjectSaver.rememberChoices(this, savePhone, saveAuto, saveZip)
        val autoFlag = saveAuto; val zipFlag = saveZip
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_AUTHOR, author.trim()).apply()
        val ui = ProjectManager.UiDesignOptions(
            topBar = uiTopBar, sidebar = uiSidebar, bottomNav = uiBottomNav,
            primaryColor = String.format("#%06X", 0xFFFFFF and uiPrimary)
        )
        val container = ProjectManager.ContainerOptions(containerVar.trim().ifEmpty { "counter" }, containerValue.trim().ifEmpty { "0" })
        val table = ProjectManager.TableOptions(tableCols, tableRows)
        val free = ProjectManager.FreeOptions(freeTemplate)
        val chosenType = type
        val chosenIndsin = indsinTemplate

        val doCreate = {
            ProjectCreationProgressDialog(this).run(
                work = {
                    val project = ProjectManager.createProject(this, projectName, chosenType, ui, container, table, free, chosenIndsin)
                    if (chosenType == ProjectType.ILLUST) {
                        com.dlof.rinlang.store.languages.CustomLanguageProjectScaffolder.installBundledIllust(this, project.dir)
                    }
                    ProjectManager.applyBlueprint(project, bp)
                    ExternalProjectSaver.unlink(this, project.name)   // لا تبقَ روابط قديمة لاسم مُعاد استخدامه
                    val target = phoneTree
                    if (target != null) {
                        val r = ExternalProjectSaver.saveNew(this, project, target, zipFlag, autoFlag)
                        saveNote = if (r.ok) getString(R.string.wiz_save_done, r.folderName, r.files)
                        else getString(R.string.wiz_save_failed, r.error ?: "?")
                    }
                    project
                },
                onDone = { project, errorMessage ->
                    if (project != null) {
                        saveNote?.let { Toast.makeText(this, it, Toast.LENGTH_LONG).show() }
                        setResult(RESULT_OK, Intent().putExtra(EXTRA_CREATED, project.name))
                        finish()
                    } else {
                        Toast.makeText(this, errorMessage ?: getString(R.string.wiz_error_generic), Toast.LENGTH_LONG).show()
                    }
                }
            )
        }
        // لغة Illust تُنزَّل كحزمة أصول عند أول مشروع (كما في الحوار القديم)
        if (chosenType == ProjectType.ILLUST) {
            com.dlof.rinlang.packs.PackDownloadDialog.ensure(
                this,
                com.dlof.rinlang.store.languages.CustomLanguageProjectScaffolder.ILLUST_PACK_ID,
                getString(R.string.project_type_illust)
            ) { doCreate() }
        } else doCreate()
    }
}
