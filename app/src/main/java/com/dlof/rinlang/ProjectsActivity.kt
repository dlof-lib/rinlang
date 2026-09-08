package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ArrayAdapter
import android.widget.PopupMenu
import android.widget.RadioButton
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView

/**
 * شاشة "أنشئ مشروع": تعرض كل مشاريع Rin الموجودة (مجلدات داخل filesDir/projects)
 * وتسمح بإنشاء مشروع جديد، إعادة تسميته، حذفه، أو فتحه (ينتقل لشاشة الملفات
 * الخاصة به عبر [FilesActivity]).
 */
class ProjectsActivity : AppCompatActivity() {

    private lateinit var rvProjects: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var txtEmptyTitle: TextView
    private lateinit var txtEmptyHint: TextView
    private lateinit var txtProjectCount: TextView
    private lateinit var adapter: ProjectsAdapter

    /** كل المشاريع بعد الفرز، قبل تطبيق فلتر البحث — المصدر الذي يُعاد فلترته عند كل كتابة. */
    private var sortedProjects: List<Project> = emptyList()
    private var searchQuery: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_projects)
        RinLoading.startup(this, 700L)
        BottomNavHelper.setup(this, BottomNavTab.PROJECTS)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.projects_screen_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvProjects = findViewById(R.id.rvProjects)
        txtEmpty = findViewById(R.id.txtEmptyProjects)
        txtEmptyTitle = findViewById(R.id.txtEmptyProjectsTitle)
        txtEmptyHint = findViewById(R.id.txtEmptyProjectsHint)
        txtProjectCount = findViewById(R.id.txtProjectCount)
        val fabNewProject: View = findViewById(R.id.fabNewProject)

        adapter = ProjectsAdapter(
            onOpen = { project -> openProject(project) },
            onRename = { project -> showRenameDialog(project) },
            onDelete = { project -> showDeleteConfirm(project) },
            onMove = { project -> showMoveDialog(project) }
        )
        // شبكة عمودين بمظهر "غلاف ألبوم" (انظر item_project.xml) بدل قائمة مسطّحة أحادية
        // العمود — يوحّد مظهر شاشة "مشاريعي" مع شاشة "الألبومات" بدل أن تبدو شاشتين من
        // تطبيقين مختلفين.
        rvProjects.layoutManager = GridLayoutManager(this, 2)
        rvProjects.adapter = adapter

        fabNewProject.setOnClickListener { showCreateDialog() }
        findViewById<View>(R.id.btnProjectAlbums).setOnClickListener { startActivity(Intent(this, AlbumsActivity::class.java)) }
        findViewById<View>(R.id.btnProjectSort).setOnClickListener { anchor -> showSortMenu(anchor) }

        // شريط البحث كان موجوداً بصرياً فقط بلا أي منطق خلفه — الكتابة فيه لم تكن تفعل شيئاً.
        // الآن يفلتر شبكة المشاريع فورياً بحسب الاسم مع كل حرف يُكتب.
        findViewById<EditText>(R.id.inputProjectSearch).addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                searchQuery = s?.toString().orEmpty()
                applyFilter()
            }
            override fun afterTextChanged(s: android.text.Editable?) {}
        })
    }

    /** قائمة "ترتيب المشاريع" المنبثقة من رأس الشاشة: تكرار سريع لخيارات AppSettings.setProjectSort
     * المتاحة أصلاً في الإعدادات، لكن مباشرة فوق شاشة المشاريع نفسها. */
    private fun showSortMenu(anchor: View) {
        val popup = PopupMenu(this, anchor)
        popup.menuInflater.inflate(R.menu.menu_project_sort, popup.menu)
        popup.setOnMenuItemClickListener { item ->
            val value = when (item.itemId) {
                R.id.sortProjectsName -> "name"
                R.id.sortProjectsType -> "type"
                else -> "recent"
            }
            AppSettings.setProjectSort(this, value)
            refresh()
            true
        }
        popup.show()
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        sortedProjects = ProjectManager.listProjects(this).let { list ->
            when (AppSettings.getProjectSort(this)) {
                "name" -> list.sortedBy { it.name.lowercase() }
                "type" -> list.sortedWith(compareBy<Project> { it.type.id }.thenBy { it.name.lowercase() })
                else -> list
            }
        }
        // عدّاد المشاريع كان نصاً ثابتاً على "لا توجد مشاريع" دوماً بغضّ النظر عن العدد
        // الفعلي — لم يكن مربوطاً بأي منطق. الآن يعرض العدد الحقيقي.
        txtProjectCount.text = if (sortedProjects.isEmpty()) {
            getString(R.string.projects_count_zero)
        } else {
            getString(R.string.projects_count_format, sortedProjects.size)
        }
        applyFilter()
    }

    /** يطبّق فلتر البحث الحالي فوق [sortedProjects] ويُحدّث الشبكة وحالة "لا توجد نتائج/مشاريع". */
    private fun applyFilter() {
        val query = searchQuery.trim()
        val filtered = if (query.isEmpty()) {
            sortedProjects
        } else {
            sortedProjects.filter { it.name.contains(query, ignoreCase = true) }
        }
        adapter.submit(filtered)

        if (filtered.isEmpty()) {
            txtEmpty.visibility = View.VISIBLE
            if (sortedProjects.isEmpty()) {
                txtEmptyTitle.text = getString(R.string.no_projects_yet)
                txtEmptyHint.text = getString(R.string.no_projects_hint)
                txtEmptyHint.visibility = View.VISIBLE
            } else {
                txtEmptyTitle.text = getString(R.string.no_search_results)
                txtEmptyHint.visibility = View.GONE
            }
        } else {
            txtEmpty.visibility = View.GONE
        }
    }

    private fun openProject(project: Project) {
        val intent = Intent(this, FilesActivity::class.java)
        intent.putExtra(FilesActivity.EXTRA_PROJECT_NAME, project.name)
        startActivity(intent)
    }

    /**
     * حوار "مشروع جديد": اسم المشروع + شبكة 2×2 من شرائح اختيار النوع (Container/Table/UI/
     * Free Project، انظر [ProjectType]). عند الضغط على "إنشاء" تظهر مراحل الإنشاء
     * (جاري التحميل.. / يتم التجهيز.. / تم..) عبر [ProjectCreationProgressDialog]، ثم يُفتح
     * المشروع تلقائياً في [FilesActivity].
     */
    private fun showCreateDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_create_project, null)
        val input: EditText = view.findViewById(R.id.inputProjectName)
        // بطاقات نوع المشروع أصبحت LinearLayout (أيقونة + عنوان + وصف) بدل TextView مفردة،
        // لكن منطق التحديد (isSelected) نفسه لأن bg_project_type_chip.xml selector يعمل على أي View.
        val chipContainer: View = view.findViewById(R.id.chipTypeContainer)
        val chipTable: View = view.findViewById(R.id.chipTypeTable)
        val chipUi: View = view.findViewById(R.id.chipTypeUi)
        val chipFree: View = view.findViewById(R.id.chipTypeFree)
        val chipIllust: View = view.findViewById(R.id.chipTypeIllust)
        val chips = mapOf(
            chipContainer to ProjectType.CONTAINER,
            chipTable to ProjectType.TABLE,
            chipUi to ProjectType.UI,
            chipFree to ProjectType.FREE,
            chipIllust to ProjectType.ILLUST
        )

        // قسم "رسم الواجهة" (يظهر فقط عند اختيار نوع UI): توب بار/بلا توب بار، قائمة جانبية/بلا
        // قائمة جانبية، ولون أساسي — تُقرأ كلها عند الضغط على "إنشاء" وتُمرَّر كـ
        // ProjectManager.UiDesignOptions لتوليد main.rin المطابق (انظر ProjectManager.kt).
        val sectionUiDesign: View = view.findViewById(R.id.sectionUiDesign)
        val switchUiTopBar: Switch = view.findViewById(R.id.switchUiTopBar)
        val switchUiSidebar: Switch = view.findViewById(R.id.switchUiSidebar)
        val rowUiColors: LinearLayout = view.findViewById(R.id.rowUiColors)
        val rowUiTextColor: LinearLayout = view.findViewById(R.id.rowUiTextColor)
        val rowUiBackground: LinearLayout = view.findViewById(R.id.rowUiBackground)
        val switchUiBottomNav: Switch = view.findViewById(R.id.switchUiBottomNav)
        val radioUiFilled: RadioButton = view.findViewById(R.id.radioUiFilled)
        val radioUiSoft: RadioButton = view.findViewById(R.id.radioUiSoft)
        val radioUiOutline: RadioButton = view.findViewById(R.id.radioUiOutline)
        val spinnerUiFont: android.widget.Spinner = view.findViewById(R.id.spinnerUiFont)
        val spinnerUiTypography: android.widget.Spinner = view.findViewById(R.id.spinnerUiTypography)
        val spinnerUiRadius: android.widget.Spinner = view.findViewById(R.id.spinnerUiRadius)
        val uiPreview: UiDesignPreviewView = view.findViewById(R.id.uiDesignPreview)

        // لوحة الألوان الأساسية المتاحة لاختيار المستخدم؛ أول لون (البنفسجي) هو الافتراضي
        // نفسه المستخدم سابقاً في قالب UI الثابت، حتى لا يتغيّر الشكل الافتراضي لمن لا يلمس هذا الخيار.
        val colorPalette = listOf(
            R.color.ui_design_color_purple,
            R.color.ui_design_color_blue,
            R.color.ui_design_color_green,
            R.color.ui_design_color_amber,
            R.color.ui_design_color_pink,
            R.color.ui_design_color_cyan
        ).map { ContextCompat.getColor(this, it) }
        // فهرس اصطناعي (خارج مدى colorPalette) يمثّل اختيار "لون مخصص" عبر عجلة الألوان الكاملة
        // بدل أحد الألوان الجاهزة الستة.
        val customColorIndex = colorPalette.size
        var selectedColorIndex = 0
        var customColor: Int? = null
        var backgroundColor = ContextCompat.getColor(this, R.color.rin_background)
        var textColor = ContextCompat.getColor(this, R.color.rin_on_toolbar)

        val swatchSizePx = (30 * resources.displayMetrics.density).toInt()
        val swatchStrokePx = (2.5f * resources.displayMetrics.density).toInt()
        val plusIconPx = (7 * resources.displayMetrics.density).toInt()

        fun renderColorSwatches() {
            rowUiColors.removeAllViews()
            colorPalette.forEachIndexed { index, color ->
                val swatch = View(this)
                val params = LinearLayout.LayoutParams(0, swatchSizePx, 1f).apply {
                    val marginPx = (4 * resources.displayMetrics.density).toInt()
                    setMargins(marginPx, 0, marginPx, 0)
                }
                swatch.layoutParams = params
                swatch.background = GradientDrawable().apply {
                    shape = GradientDrawable.OVAL
                    setColor(color)
                    if (index == selectedColorIndex) {
                        setStroke(swatchStrokePx, ContextCompat.getColor(this@ProjectsActivity, R.color.rin_on_toolbar))
                    }
                }
                swatch.setOnClickListener {
                    selectedColorIndex = index
                    renderColorSwatches()
                    uiPreview.configure(colorPalette[selectedColorIndex], backgroundColor, textColor, switchUiTopBar.isChecked, switchUiSidebar.isChecked, switchUiBottomNav.isChecked, "filled", 14, "sans", "medium")
                }
                rowUiColors.addView(swatch)
            }

            // شارة "لون مخصص": تعرض علامة + فوق دائرة فارغة إن لم يُختر لون مخصص بعد، أو
            // اللون المخصص نفسه إن كان موجوداً. الضغط عليها يفتح عجلة الألوان الكاملة دوماً
            // (لتعديل الاختيار حتى لو كان محدَّداً سلفاً).
            val customSwatch = android.widget.ImageView(this)
            val customParams = LinearLayout.LayoutParams(0, swatchSizePx, 1f).apply {
                val marginPx = (4 * resources.displayMetrics.density).toInt()
                setMargins(marginPx, 0, marginPx, 0)
            }
            customSwatch.layoutParams = customParams
            val isCustomSelected = selectedColorIndex == customColorIndex
            customSwatch.background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                if (customColor != null) {
                    setColor(customColor!!)
                } else {
                    setColor(ContextCompat.getColor(this@ProjectsActivity, android.R.color.transparent))
                }
                setStroke(
                    if (isCustomSelected) swatchStrokePx else (1.5f * resources.displayMetrics.density).toInt(),
                    ContextCompat.getColor(
                        this@ProjectsActivity,
                        if (isCustomSelected) R.color.rin_on_toolbar else R.color.rin_editor_hint
                    )
                )
            }
            if (customColor == null) {
                val plusIcon = ContextCompat.getDrawable(this, android.R.drawable.ic_input_add)?.mutate()
                plusIcon?.setTint(ContextCompat.getColor(this, R.color.rin_editor_hint))
                customSwatch.setImageDrawable(plusIcon)
                customSwatch.setPadding(plusIconPx, plusIconPx, plusIconPx, plusIconPx)
            } else {
                customSwatch.setImageDrawable(null)
                customSwatch.setPadding(0, 0, 0, 0)
            }
            customSwatch.contentDescription = getString(R.string.color_picker_custom_desc)
            customSwatch.setOnClickListener {
                showColorPickerDialog(customColor ?: colorPalette[selectedColorIndex.coerceIn(0, colorPalette.lastIndex)]) { pickedColor ->
                    customColor = pickedColor
                    selectedColorIndex = customColorIndex
                    renderColorSwatches()
                    uiPreview.configure(pickedColor, backgroundColor, textColor, switchUiTopBar.isChecked, switchUiSidebar.isChecked, switchUiBottomNav.isChecked, "filled", 14, "sans", "medium")
                }
            }
            rowUiColors.addView(customSwatch)
        }
        renderColorSwatches()

        fun addColorControl(row: LinearLayout, color: Int, onPick: (Int) -> Unit) {
            val swatch = View(this)
            val size = (34 * resources.displayMetrics.density).toInt()
            swatch.layoutParams = LinearLayout.LayoutParams(size, size).apply { setMargins(0, 2, 8, 2) }
            swatch.background = GradientDrawable().apply { shape = GradientDrawable.RECTANGLE; cornerRadius = 10f; setColor(color); setStroke(2, ContextCompat.getColor(this@ProjectsActivity, R.color.rin_divider)) }
            swatch.setOnClickListener { showColorPickerDialog(color, onPick) }
            row.removeAllViews(); row.addView(swatch)
        }

        fun currentUiColor(): Int =
            if (selectedColorIndex == customColorIndex) (customColor ?: colorPalette[0]) else colorPalette[selectedColorIndex]

        val fontAdapter = ArrayAdapter.createFromResource(this, R.array.ui_font_names, android.R.layout.simple_spinner_item).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        spinnerUiFont.adapter = fontAdapter
        val typoAdapter = ArrayAdapter.createFromResource(this, R.array.ui_typography_names, android.R.layout.simple_spinner_item).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        spinnerUiTypography.adapter = typoAdapter
        val radiusAdapter = ArrayAdapter.createFromResource(this, R.array.ui_radius_names, android.R.layout.simple_spinner_item).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        spinnerUiRadius.adapter = radiusAdapter
        spinnerUiRadius.setSelection(2)

        fun currentButtonStyle(): String = when { radioUiOutline.isChecked -> "outline"; radioUiSoft.isChecked -> "soft"; else -> "filled" }
        fun currentFont(): String = resources.getStringArray(R.array.ui_font_values)[spinnerUiFont.selectedItemPosition.coerceIn(0, 2)]
        fun currentTypography(): String = resources.getStringArray(R.array.ui_typography_values)[spinnerUiTypography.selectedItemPosition.coerceIn(0, 2)]
        fun currentRadius(): Int = resources.getIntArray(R.array.ui_radius_values)[spinnerUiRadius.selectedItemPosition.coerceIn(0, 4)]
        fun updateUiPreview() { uiPreview.configure(currentUiColor(), backgroundColor, textColor, switchUiTopBar.isChecked, switchUiSidebar.isChecked, switchUiBottomNav.isChecked, currentButtonStyle(), currentRadius(), currentFont(), currentTypography()) }

        addColorControl(rowUiTextColor, textColor) { textColor = it; updateUiPreview() }
        addColorControl(rowUiBackground, backgroundColor) { backgroundColor = it; updateUiPreview() }

        // Spinner (وأي AdapterView) لا يدعم setOnClickListener إطلاقاً — يرمي RuntimeException
        // فوراً عند أول محاولة استخدامه ("Don't call setOnClickListener for an AdapterView").
        // اختيار عنصر من Spinner أصلاً يُعاد بثه عبر setOnItemSelectedListener بالأسفل، فلا
        // حاجة لإضافته هنا أساساً.
        val previewListeners = listOf<View>(switchUiTopBar, switchUiSidebar, switchUiBottomNav, radioUiFilled, radioUiSoft, radioUiOutline)
        previewListeners.forEach { it.setOnClickListener { updateUiPreview() } }
        spinnerUiFont.setOnItemSelectedListener(object : android.widget.AdapterView.OnItemSelectedListener { override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {} override fun onItemSelected(parent: android.widget.AdapterView<*>?, v: View?, position: Int, id: Long) { updateUiPreview() } })
        spinnerUiTypography.setOnItemSelectedListener(spinnerUiFont.onItemSelectedListener)
        spinnerUiRadius.setOnItemSelectedListener(spinnerUiFont.onItemSelectedListener)
        updateUiPreview()

        var selectedType = ProjectType.FREE
        fun selectChip(chip: View) {
            selectedType = chips.getValue(chip)
            chips.keys.forEach { it.isSelected = it === chip }
            sectionUiDesign.visibility = if (selectedType == ProjectType.UI) View.VISIBLE else View.GONE
            if (selectedType == ProjectType.UI) updateUiPreview()
        }
        chips.keys.forEach { chip -> chip.setOnClickListener { selectChip(chip) } }
        selectChip(chipFree)

        AlertDialog.Builder(this)
            .setTitle(R.string.new_project_title)
            .setView(view)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString()
                val uiOptions = ProjectManager.UiDesignOptions(
                    topBar = switchUiTopBar.isChecked,
                    sidebar = switchUiSidebar.isChecked,
                    bottomNav = switchUiBottomNav.isChecked,
                    buttonStyle = currentButtonStyle(),
                    primaryColor = String.format("#%06X", 0xFFFFFF and currentUiColor()),
                    background = String.format("#%06X", 0xFFFFFF and backgroundColor),
                    text = String.format("#%06X", 0xFFFFFF and textColor),
                    fontFamily = currentFont(),
                    typography = currentTypography(),
                    cornerRadius = currentRadius()
                )
                ProjectCreationProgressDialog(this).run(
                    work = {
                        val project = ProjectManager.createProject(this, name, selectedType, uiOptions)
                        if (selectedType == ProjectType.ILLUST) {
                            com.dlof.rinlang.store.languages.CustomLanguageProjectScaffolder
                                .installBundledIllust(this, project.dir)
                        }
                        project
                    },
                    onDone = { project, errorMessage ->
                        if (project != null) {
                            refresh()
                            openProject(project)
                        } else {
                            Toast.makeText(this, errorMessage ?: getString(R.string.project_name_hint), Toast.LENGTH_SHORT).show()
                        }
                    }
                )
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /**
     * حوار "اختيار لون" الكامل: عجلة ألوان (Hue+Saturation) تفاعلية + معاينة دائرية + حقل هكس
     * قابل للتعديل يدوياً. يُفتح من شارة "لون مخصص" في حوار "مشروع جديد" ([showCreateDialog]).
     * [onPicked] يُستدعى فقط عند الضغط على "رجوع" (وليس أثناء السحب)، باللون النهائي المختار.
     */
    private fun showColorPickerDialog(initialColor: Int, onPicked: (Int) -> Unit) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_color_picker, null)
        val wheel: HueSaturationPickerView = view.findViewById(R.id.hueSaturationPicker)
        val preview: View = view.findViewById(R.id.imgColorPreview)
        val hexInput: EditText = view.findViewById(R.id.inputColorHex)

        var currentColor = initialColor

        fun updatePreview(color: Int, updateHexField: Boolean) {
            currentColor = color
            preview.background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(color)
                setStroke(
                    (1.5f * resources.displayMetrics.density).toInt(),
                    ContextCompat.getColor(this@ProjectsActivity, R.color.rin_divider)
                )
            }
            if (updateHexField) {
                hexInput.setText(String.format("%06X", 0xFFFFFF and color))
            }
        }

        wheel.post { wheel.setColor(initialColor) }
        updatePreview(initialColor, updateHexField = true)
        wheel.onColorChanged = { color -> updatePreview(color, updateHexField = true) }

        // تعديل الهكس يدوياً: عند مغادرة الحقل (فقدان التركيز)، إن كانت القيمة صالحة (6 خانات
        // hex) نطبّقها على المعاينة وموضع المؤشر في العجلة، وإلا نعيد القيمة السابقة الصالحة.
        hexInput.setOnFocusChangeListener { _, hasFocus ->
            if (!hasFocus) {
                val text = hexInput.text.toString().removePrefix("#")
                val parsed = text.toIntOrNull(16)
                if (text.length == 6 && parsed != null) {
                    val color = (0xFF000000).toInt() or parsed
                    wheel.setColor(color)
                    updatePreview(color, updateHexField = false)
                } else {
                    updatePreview(currentColor, updateHexField = true)
                }
            }
        }

        AlertDialog.Builder(this)
            .setTitle(R.string.color_picker_title)
            .setView(view)
            .setPositiveButton(R.string.color_picker_back) { _, _ -> onPicked(currentColor) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    // FIX (مشكلة البومات): كانت هذه الشاشة تحمل نسخة قديمة موازية من تدفّق الألبومات بالكامل
    // (showAlbumsDialog/showCreateAlbumDialog/showAlbumProjects — حوارات AlertDialog بسيطة)،
    // من قبل أن تُبنى شاشتا AlbumsActivity/AlbumDetailActivity المخصّصتين بتصميم الغلاف
    // المرئي. لم تعد الدوال الثلاث مستدعاة من أي مكان (btnProjectAlbums يفتح AlbumsActivity
    // مباشرة الآن) وبقيت معلّقة بلا استخدام — كود ميت يعرض منطق ألبومات مختلف/متضارب لو
    // استُدعي بالخطأ لاحقاً. أُزيلت هنا نهائياً.
    //
    // كما كان showMoveDialog نفسه ينتهي بطريق مسدود: إن لم يملك المستخدم أي ألبوم بعد، يعرض
    // Toast فقط ("أنشئ ألبوماً أولاً") ويُغلق — بلا أي طريق فعلي لإنشاء الألبوم من هنا، فيضطر
    // المستخدم للخروج، فتح شاشة الألبومات، إنشاء واحد، ثم العودة والمحاولة من جديد. أصبح
    // الآن "+ ألبوم جديد" خياراً دائم الظهور أول القائمة، فينشئ الألبوم وينقل المشروع إليه
    // في خطوة واحدة.
    private fun showMoveDialog(project: Project) {
        val albums = ProjectAlbumManager.listAlbums(this)
        val newAlbumLabel = getString(R.string.album_new)
        val names = (listOf("+ $newAlbumLabel") + albums).toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(R.string.album_move_title)
            .setItems(names) { _, which ->
                if (which == 0) {
                    showCreateAlbumThenMove(project)
                } else {
                    try {
                        ProjectAlbumManager.moveProjectToAlbum(this, project, albums[which - 1])
                        refresh()
                    } catch (e: IllegalArgumentException) {
                        Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showCreateAlbumThenMove(project: Project) {
        val input = EditText(this).apply { hint = getString(R.string.album_name_hint) }
        AlertDialog.Builder(this)
            .setTitle(R.string.album_new)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString()
                try {
                    ProjectAlbumManager.createAlbum(this, name)
                    ProjectAlbumManager.moveProjectToAlbum(this, project, name.trim())
                    refresh()
                    Toast.makeText(this, R.string.album_created, Toast.LENGTH_SHORT).show()
                } catch (e: IllegalArgumentException) {
                    Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showRenameDialog(project: Project) {
        val input = EditText(this)
        input.setText(project.name)
        AlertDialog.Builder(this)
            .setTitle(R.string.rename_project_title)
            .setView(input)
            .setPositiveButton(R.string.rename) { _, _ ->
                try {
                    ProjectManager.renameProject(project, input.text.toString())
                    refresh()
                } catch (e: IllegalArgumentException) {
                    Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showDeleteConfirm(project: Project) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_project_title)
            .setMessage(getString(R.string.delete_project_confirm, project.name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectManager.deleteProject(project)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

private class ProjectsAdapter(
    val onOpen: (Project) -> Unit,
    val onRename: (Project) -> Unit,
    val onDelete: (Project) -> Unit,
    val onMove: (Project) -> Unit
) : RecyclerView.Adapter<ProjectsAdapter.VH>() {

    private var items: List<Project> = emptyList()

    fun submit(newItems: List<Project>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtProjectName)
        val txtType: TextView = view.findViewById(R.id.txtProjectType)
        val txtMeta: TextView = view.findViewById(R.id.txtProjectMeta)
        val frameTypeIcon: android.widget.FrameLayout = view.findViewById(R.id.frameProjectTypeIcon)
        val imgTypeIcon: android.widget.ImageView = view.findViewById(R.id.imgProjectTypeIcon)
        val btnMore: View = view.findViewById(R.id.btnProjectMore)
        val btnOpen: View = view.findViewById(R.id.btnOpenProject)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_project, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val project = items[position]
        val context = holder.itemView.context
        val density = context.resources.displayMetrics.density

        // كانت البطاقة كلها تُلوَّن بتدرّج زاهٍ حسب نوع المشروع (يبدو مرحاً/طفولياً أقرب
        // لتطبيق ألبومات صور). الآن البطاقة مسطّحة محايدة (bg_project_card)، وهوية نوع
        // المشروع تنحصر في شارة أيقونة صغيرة ملوّنة بخفّة (لون النوع بشفافية منخفضة كخلفية
        // + نفس اللون كامل التشبّع للأيقونة والتسمية) — تصميم أهدأ وأقرب لأدوات برمجية جادة.
        val (iconRes, colorRes) = typeIconAndColor(project.type)
        val color = ContextCompat.getColor(context, colorRes)
        holder.frameTypeIcon.background = GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = 9f * density
            setColor(withAlpha(color, 0.16f))
        }
        val icon = ContextCompat.getDrawable(context, iconRes)?.mutate()
        icon?.setTint(color)
        holder.imgTypeIcon.setImageDrawable(icon)

        holder.txtName.text = project.name
        holder.txtType.text = typeLabel(context, project.type).uppercase()
        holder.txtType.setTextColor(color)
        // البطاقة كانت تعرض عدد الملفات فقط — لا شيء يوحي متى آخر مرة عُدِّل فيها المشروع
        // رغم أن Project.lastModified متوفّر أصلاً. إضافة وقت نسبي ("قبل يومين"، محلَّى تلقائياً
        // حسب لغة الجهاز عبر DateUtils) تجعل البطاقة تعكس مشروعاً حياً قيد العمل، لا مجرّد مجلد.
        val fileCount = ProjectManager.listFiles(project).size
        val relativeTime = android.text.format.DateUtils.getRelativeTimeSpanString(
            project.lastModified, System.currentTimeMillis(), android.text.format.DateUtils.MINUTE_IN_MILLIS
        )
        holder.txtMeta.text = context.getString(R.string.project_meta_with_time_format, fileCount, relativeTime)

        holder.itemView.setOnClickListener { onOpen(project) }
        holder.btnOpen.setOnClickListener { onOpen(project) }
        holder.btnMore.setOnClickListener { anchor -> showActionsMenu(anchor, project) }
    }

    override fun getItemCount(): Int = items.size

    /** قائمة "المزيد" المنبثقة على غلاف البطاقة: إعادة تسمية / نقل لألبوم / حذف. */
    private fun showActionsMenu(anchor: View, project: Project) {
        val popup = PopupMenu(anchor.context, anchor)
        popup.menuInflater.inflate(R.menu.menu_project_actions, popup.menu)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.actionRenameProject -> onRename(project)
                R.id.actionMoveProject -> onMove(project)
                R.id.actionDeleteProject -> onDelete(project)
            }
            true
        }
        popup.show()
    }

    /** يُرجع نفس اللون بدرجة شفافية مخفَّضة (alpha 0..1) — تُستخدَم لخلفية شارة أيقونة النوع
     * كي تبقى شارة صغيرة خفيفة بدل تلوين كامل البطاقة. */
    private fun withAlpha(c: Int, alpha: Float): Int =
        Color.argb((alpha * 255).toInt(), Color.red(c), Color.green(c), Color.blue(c))

    /** نص شارة نوع المشروع المعروضة بجانب اسمه في القائمة. */
    private fun typeLabel(context: android.content.Context, type: ProjectType): String = when (type) {
        ProjectType.CONTAINER -> context.getString(R.string.project_type_container)
        ProjectType.TABLE -> context.getString(R.string.project_type_table)
        ProjectType.UI -> context.getString(R.string.project_type_ui)
        ProjectType.FREE -> context.getString(R.string.project_type_free)
        ProjectType.ILLUST -> context.getString(R.string.project_type_illust)
    }

    /** أيقونة + لون هوية شارة نوع المشروع، بنفس الأيقونات المستخدمة في حوار "مشروع جديد". */
    private fun typeIconAndColor(type: ProjectType): Pair<Int, Int> = when (type) {
        ProjectType.CONTAINER -> R.drawable.ic_type_container to R.color.project_type_container_color
        ProjectType.TABLE -> R.drawable.ic_type_table to R.color.project_type_table_color
        ProjectType.UI -> R.drawable.ic_type_ui to R.color.project_type_ui_color
        ProjectType.FREE -> R.drawable.ic_type_free to R.color.project_type_free_color
        ProjectType.ILLUST -> R.drawable.ic_illust_file to R.color.project_type_illust_color
    }
}
