package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import android.text.Editable
import android.text.TextWatcher
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView

/**
 * شاشة "أنشئ مشروع": تعرض كل مشاريع Rin الموجودة (مجلدات داخل filesDir/projects)
 * وتسمح بإنشاء مشروع جديد، إعادة تسميته، حذفه، أو فتحه (ينتقل لشاشة الملفات
 * الخاصة به عبر [FilesActivity]).
 */
class ProjectsActivity : AppCompatActivity() {

    private lateinit var rvProjects: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var adapter: ProjectsAdapter
    private lateinit var txtProjectCount: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_projects)
        RinLoading.startup(this, 700L)
        BottomNavHelper.setup(this, BottomNavTab.PROJECTS)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.projects_screen_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvProjects = findViewById(R.id.rvProjects)
        txtEmpty = findViewById(R.id.txtEmptyProjects)
        txtProjectCount = findViewById(R.id.txtProjectCount)
        val inputProjectSearch: EditText = findViewById(R.id.inputProjectSearch)
        val fabNewProject: View = findViewById(R.id.fabNewProject)

        adapter = ProjectsAdapter(
            onOpen = { project -> openProject(project) },
            onRename = { project -> showRenameDialog(project) },
            onDelete = { project -> showDeleteConfirm(project) },
            onMove = { project -> showMoveDialog(project) }
        )
        rvProjects.layoutManager = LinearLayoutManager(this)
        rvProjects.adapter = adapter

        inputProjectSearch.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                adapter.setQuery(s?.toString().orEmpty())
                updateEmptyState()
            }
            override fun afterTextChanged(s: Editable?) = Unit
        })

        fabNewProject.setOnClickListener { showCreateDialog() }
        findViewById<View>(R.id.btnProjectAlbums).setOnClickListener { showAlbumsDialog() }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val projects = ProjectManager.listProjects(this).let { list ->
            when (AppSettings.getProjectSort(this)) {
                "name" -> list.sortedBy { it.name.lowercase() }
                "type" -> list.sortedWith(compareBy<Project> { it.type.id }.thenBy { it.name.lowercase() })
                else -> list
            }
        }
        adapter.submit(projects)
        updateEmptyState()
        txtProjectCount.text = resources.getQuantityString(
            R.plurals.projects_count,
            projects.size,
            projects.size
        )
    }

    private fun updateEmptyState() {
        txtEmpty.visibility = if (adapter.itemCount == 0) View.VISIBLE else View.GONE
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
                }
            }
            rowUiColors.addView(customSwatch)
        }
        renderColorSwatches()

        fun currentUiColor(): Int =
            if (selectedColorIndex == customColorIndex) (customColor ?: colorPalette[0]) else colorPalette[selectedColorIndex]

        var selectedType = ProjectType.FREE
        fun selectChip(chip: View) {
            selectedType = chips.getValue(chip)
            chips.keys.forEach { it.isSelected = it === chip }
            sectionUiDesign.visibility = if (selectedType == ProjectType.UI) View.VISIBLE else View.GONE
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
                    primaryColor = String.format("#%06X", 0xFFFFFF and currentUiColor())
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

    private fun showMoveDialog(project: Project) {
        val albums = ProjectAlbumManager.listAlbums(this)
        if (albums.isEmpty()) {
            Toast.makeText(this, R.string.album_create_first, Toast.LENGTH_SHORT).show()
            return
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.album_move_title)
            .setItems(albums.toTypedArray()) { _, which ->
                try { ProjectAlbumManager.moveProjectToAlbum(this, project, albums[which]); refresh() }
                catch (e: IllegalArgumentException) { Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show() }
            }.setNegativeButton(R.string.cancel, null).show()
    }

    private fun showAlbumsDialog() {
        val albums=ProjectAlbumManager.listAlbums(this)
        val names=(listOf(getString(R.string.album_uncategorized))+albums).toTypedArray()
        AlertDialog.Builder(this).setTitle(R.string.albums_title).setItems(names){_,which->if(which>0)showAlbumProjects(albums[which-1])}.setPositiveButton(R.string.album_new){_,_->showCreateAlbumDialog()}.setNegativeButton(R.string.cancel,null).show()
    }
    private fun showCreateAlbumDialog() {
        val input=EditText(this); input.hint=getString(R.string.album_name_hint)
        AlertDialog.Builder(this).setTitle(R.string.album_new).setView(input).setPositiveButton(R.string.create){_,_->try{ProjectAlbumManager.createAlbum(this,input.text.toString());Toast.makeText(this,R.string.album_created,Toast.LENGTH_SHORT).show()}catch(e:IllegalArgumentException){Toast.makeText(this,e.message,Toast.LENGTH_SHORT).show()}}.setNegativeButton(R.string.cancel,null).show()
    }
    private fun showAlbumProjects(album:String) {
        val projects=ProjectAlbumManager.projectsInAlbum(this,album); val names=if(projects.isEmpty())arrayOf(getString(R.string.album_empty))else projects.map{it.name}.toTypedArray()
        AlertDialog.Builder(this).setTitle(album).setItems(names){_,which->if(projects.isNotEmpty())openProject(projects[which])}.setNegativeButton(R.string.cancel,null).show()
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

    private var allItems: List<Project> = emptyList()
    private var items: List<Project> = emptyList()
    private var query: String = ""

    fun submit(newItems: List<Project>) {
        allItems = newItems
        applyFilter()
    }

    fun setQuery(value: String) {
        query = value.trim()
        applyFilter()
    }

    private fun applyFilter() {
        items = if (query.isBlank()) {
            allItems
        } else {
            allItems.filter { it.name.contains(query, ignoreCase = true) }
        }
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtProjectName)
        val txtType: TextView = view.findViewById(R.id.txtProjectType)
        val txtMeta: TextView = view.findViewById(R.id.txtProjectMeta)
        val btnRename: View = view.findViewById(R.id.btnRenameProject)
        val btnDelete: View = view.findViewById(R.id.btnDeleteProject)
        val btnMove: View = view.findViewById(R.id.btnMoveProject)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_project, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val project = items[position]
        val context = holder.itemView.context
        holder.txtName.text = project.name
        holder.txtType.text = typeLabel(context, project.type)
        val (iconRes, colorRes) = typeIconAndColor(project.type)
        val color = androidx.core.content.ContextCompat.getColor(context, colorRes)
        holder.txtType.setTextColor(color)
        // نضبط حجم الأيقونة يدوياً بدل setCompoundDrawablesWithIntrinsicBounds لأن حجمها
        // الأصلي (24dp) أكبر من ارتفاع شارة صغيرة كهذه.
        val iconSizePx = (11 * context.resources.displayMetrics.density).toInt()
        val icon = androidx.core.content.ContextCompat.getDrawable(context, iconRes)?.mutate()
        icon?.setTint(color)
        icon?.setBounds(0, 0, iconSizePx, iconSizePx)
        holder.txtType.setCompoundDrawables(icon, null, null, null)
        val fileCount = ProjectManager.listFiles(project).size
        holder.txtMeta.text = context.getString(R.string.project_meta_format, fileCount)
        holder.itemView.setOnClickListener { onOpen(project) }
        holder.btnRename.setOnClickListener { onRename(project) }
        holder.btnDelete.setOnClickListener { onDelete(project) }
        holder.btnMove.setOnClickListener { onMove(project) }
    }

    override fun getItemCount(): Int = items.size

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
