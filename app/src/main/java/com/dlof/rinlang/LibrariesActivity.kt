package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.store.AccountActivity
import com.dlof.rinlang.store.PublishPackageActivity
import com.dlof.rinlang.store.RinStoreActivity
import com.dlof.rinlang.widgets.IndsinIconView
import com.dlof.rinlang.widgets.IndsinIcons
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "المكتبات" الخاصة بمشروع واحد (يفتحها زر "المكتبات" داخل المحرر [MainActivity]).
 *
 * تتيح لأي مستخدم:
 *  - "رفع مكتبة" (upload): استيراد ملف مكتبة .og.rin موجود بالفعل على الجهاز عبر SAF.
 *  - إنشاء مكتبة جديدة فارغة (بقالب بسيط جاهز) وفتحها للتعديل مباشرة في المحرر.
 *  - تعديل/حذف أي مكتبة من مكتباته الخاصة.
 *  - تصفّح المكتبات القياسية الخمس الجاهزة (مدمجة داخل المفسّر، تعمل فوراً بلا رفع).
 *  - "إدراج" سطر @import المناسب مباشرة داخل كود المحرر بضغطة واحدة (تُغلق هذه الشاشة
 *    وتُعيد السطر إلى [MainActivity] عبر setResult، فيُدرَج عند مكان المؤشر في الكود).
 */
class LibrariesActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** النتيجة المُعادة إلى المحرر: نص سطر @import الجاهز للإدراج عند المؤشر. */
        const val EXTRA_IMPORT_STATEMENT = "extra_import_statement"
    }

    private lateinit var project: Project
    private lateinit var rvUserLibraries: RecyclerView
    private lateinit var rvBuiltinLibraries: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var userAdapter: UserLibraryAdapter
    private lateinit var tvHeroSubtitle: TextView
    private lateinit var tvLibrariesCountPill: TextView
    private lateinit var builtinAdapter: BuiltinLibraryAdapter
    private lateinit var cardFeatured: View
    private lateinit var tvBuiltinCount: TextView
    private lateinit var tvNoResults: View
    private lateinit var llChips: LinearLayout
    private lateinit var btnClearSearch: View

    /** نص البحث الحالي وفئة الفلترة (null = الكل) — يطبَّقان على "مكتباتي" و"الجاهزة" معاً. */
    private var query = ""
    private var selectedCategory: LibCategory? = null
    private val chips = mutableListOf<Pair<TextView, LibCategory?>>()
    private var userLibraries: List<RinLibrary> = emptyList()

    /** متصفّح الأيقونات يعيد سطر @view.Icon؛ نمرّره فوراً إلى المحرر كأي إدراج آخر. */
    private val iconBrowserLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val statement = result.data?.getStringExtra(EXTRA_IMPORT_STATEMENT)
            if (result.resultCode == RESULT_OK && statement != null) finishWithImport(statement)
        }

    private val importLibraryLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) uploadLibrary(uri)
        }

    /** يستقبل سطر @import من متجر Rin فور التثبيت، ويمرّره فوراً للمحرر (بضغطة تثبيت واحدة
     *  بدل تثبيت ثم عودة لهذه الشاشة لاختيار "إدراج" يدوياً). */
    private val rinStoreLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val importStatement = result.data?.getStringExtra(RinStoreActivity.EXTRA_IMPORT_STATEMENT)
            if (result.resultCode == RESULT_OK && importStatement != null) {
                finishWithImport(importStatement)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_libraries)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME)
            ?: run { finish(); return }
        val existing = ProjectManager.listProjects(this).find { it.name == projectName }
            ?: run {
                Toast.makeText(this, R.string.project_not_found, Toast.LENGTH_SHORT).show()
                finish(); return
            }
        project = existing
        title = getString(R.string.libraries_title_format, project.name)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.libraries_screen_title)
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            text = project.name
            visibility = View.VISIBLE
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        // رأس شاشة "المكتبات" الجديد: نفس اسم المشروع الذي كان يظهر في txtToolbarSubtitle،
        // معروضاً الآن أيضاً داخل البطاقة المتدرّجة العلوية.
        tvHeroSubtitle = findViewById(R.id.tvLibrariesHeroSubtitle)
        tvHeroSubtitle.text = project.name
        tvLibrariesCountPill = findViewById(R.id.tvLibrariesCountPill)

        rvUserLibraries = findViewById(R.id.rvUserLibraries)
        rvBuiltinLibraries = findViewById(R.id.rvBuiltinLibraries)
        txtEmpty = findViewById(R.id.txtEmptyLibraries)
        val fabUploadLibrary: View = findViewById(R.id.fabUploadLibrary)
        val fabNewLibrary: View = findViewById(R.id.fabNewLibrary)

        userAdapter = UserLibraryAdapter(
            onEdit = { lib -> openLibraryInEditor(lib) },
            onRename = { lib -> showRenameLibraryDialog(lib) },
            onDelete = { lib -> showDeleteConfirm(lib) },
            onInsert = { lib -> finishWithImport("@import \"${ProjectManagerLibPrefix}${lib.name}\";") },
            onPublish = { lib ->
                startActivity(
                    Intent(this, PublishPackageActivity::class.java)
                        .putExtra(PublishPackageActivity.EXTRA_PROJECT_NAME, project.name)
                        .putExtra(PublishPackageActivity.EXTRA_LIBRARY_NAME, lib.name)
                )
            }
        )
        rvUserLibraries.layoutManager = LinearLayoutManager(this)
        rvUserLibraries.adapter = userAdapter

        builtinAdapter = BuiltinLibraryAdapter { info ->
            finishWithImport("@import \"${info.importPath}\";")
        }
        rvBuiltinLibraries.layoutManager = LinearLayoutManager(this)
        rvBuiltinLibraries.adapter = builtinAdapter

        cardFeatured = findViewById(R.id.cardIconsFeatured)
        tvBuiltinCount = findViewById(R.id.tvBuiltinCount)
        tvNoResults = findViewById(R.id.tvLibrariesNoResults)
        llChips = findViewById(R.id.llLibraryChips)
        btnClearSearch = findViewById(R.id.btnClearLibrarySearch)
        setupSearch()
        buildChips()
        setupFeaturedCard()
        applyFilter()

        fabUploadLibrary.setOnClickListener {
            // نقبل .og.rin أو أي نص عادي، لأن أندرويد لا يربط MIME type رسمياً بهذا الامتداد المخصص.
            importLibraryLauncher.launch(arrayOf("text/plain", "application/octet-stream", "*/*"))
        }
        fabNewLibrary.setOnClickListener { showCreateLibraryDialog() }

        findViewById<View>(R.id.btnBrowseStore).setOnClickListener {
            rinStoreLauncher.launch(
                Intent(this, RinStoreActivity::class.java)
                    .putExtra(RinStoreActivity.EXTRA_PROJECT_NAME, project.name)
            )
        }
        findViewById<View>(R.id.btnMyAccount).setOnClickListener {
            startActivity(Intent(this, AccountActivity::class.java))
        }
        findViewById<View>(R.id.btnRinExtensions).setOnClickListener {
            startActivity(Intent(this, com.dlof.rinlang.store.extensions.RinExtensionsMarketplaceActivity::class.java))
        }
        findViewById<View>(R.id.btnSettings).setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val libraries = ProjectManager.listLibraries(project)
        userLibraries = libraries
        // حبّة العدّاد أعلى الشاشة: تعكس فوراً عدد مكتبات المستخدم لهذا المشروع دون فتح القائمة.
        tvLibrariesCountPill.text = libraries.size.toString()
        applyFilter()
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    // ---- البحث والفلترة ------------------------------------------------------------------------

    private fun setupSearch() {
        val et = findViewById<EditText>(R.id.etLibrarySearch)
        et.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                query = s?.toString().orEmpty()
                btnClearSearch.visibility = if (query.isEmpty()) View.GONE else View.VISIBLE
                applyFilter()
            }
        })
        btnClearSearch.setOnClickListener { et.setText("") }
    }

    private fun buildChips() {
        llChips.removeAllViews()
        chips.clear()
        val entries = ArrayList<Pair<String, LibCategory?>>()
        entries.add(Pair(getString(R.string.libs_filter_all), null))
        for (c in LibCategory.values()) entries.add(Pair(getString(c.labelRes), c))
        for ((label, cat) in entries) {
            val chip = TextView(this).apply {
                text = label
                textSize = 13f
                setPadding(dp(14), dp(7), dp(14), dp(7))
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
                ).apply { marginEnd = dp(6) }
                setOnClickListener {
                    selectedCategory = cat
                    updateChips()
                    applyFilter()
                }
            }
            chips.add(Pair(chip, cat))
            llChips.addView(chip)
        }
        updateChips()
    }

    private fun updateChips() {
        for ((chip, cat) in chips) {
            val on = cat == selectedCategory
            chip.isSelected = on
            chip.setBackgroundResource(if (on) R.drawable.bg_lib_chip_selected else R.drawable.bg_filter_chip_unselected)
            chip.setTextColor(
                if (on) ContextCompat.getColor(this, android.R.color.white)
                else ContextCompat.getColor(this, R.color.rin_on_toolbar)
            )
        }
    }

    /** يطبّق البحث + الفئة على "مكتباتي" و"الجاهزة"، ويُظهر البطاقة المميّزة عند عدم وجود بحث/فئة تستبعدها. */
    private fun applyFilter() {
        val q = query.trim().lowercase()
        val cat = selectedCategory

        // مكتباتي: بحث بالاسم فقط (الفئات خاصة بالمدمجة)
        val mine = if (q.isEmpty()) userLibraries else userLibraries.filter { it.name.lowercase().contains(q) }
        userAdapter.submit(mine)
        txtEmpty.visibility = if (userLibraries.isEmpty() && q.isEmpty()) View.VISIBLE else View.GONE

        val showFeatured = q.isEmpty() && (cat == null || cat == LibCategory.UI)
        cardFeatured.visibility = if (showFeatured) View.VISIBLE else View.GONE

        var list = BuiltinLibraries.all.filter { info ->
            (cat == null || info.category == cat) &&
                (q.isEmpty() || info.displayName.lowercase().contains(q) ||
                    info.description.lowercase().contains(q) || info.sampleFunctions.lowercase().contains(q))
        }
        // البطاقة المميّزة تغني عن تكرار مكتبة الأيقونات في القائمة
        if (showFeatured) list = list.filter { it.displayName != BuiltinLibraries.FEATURED }
        builtinAdapter.submit(list)
        tvBuiltinCount.text = getString(R.string.libs_builtin_count_format, list.size)
        tvNoResults.visibility = if (list.isEmpty() && !showFeatured && mine.isEmpty()) View.VISIBLE else View.GONE
    }

    // ---- البطاقة المميّزة: مكتبة الأيقونات ------------------------------------------------------

    private fun setupFeaturedCard() {
        val openBrowser = View.OnClickListener {
            iconBrowserLauncher.launch(
                Intent(this, IconLibraryActivity::class.java)
                    .putExtra(EXTRA_PROJECT_NAME, project.name)
            )
        }
        cardFeatured.setOnClickListener(openBrowser)
        findViewById<View>(R.id.btnOpenIconBrowser).setOnClickListener(openBrowser)
        findViewById<View>(R.id.btnInsertIconsImport).setOnClickListener {
            finishWithImport("@import \"lib/indsinicons.og.rin\";")
        }

        // معاينة حقيقية: ستّ أيقونات تُرسم من مسارات SVG نفسها التي تستعملها المكتبة
        val data = runCatching { IndsinIcons.load(this) }.getOrNull() ?: return
        findViewById<TextView>(R.id.tvFeaturedSubtitle).text =
            getString(R.string.libs_featured_subtitle_format, data.icons.size)
        val row = findViewById<LinearLayout>(R.id.rowFeaturedIcons)
        row.removeAllViews()
        for (name in listOf("home", "search", "heart", "settings", "bell", "rocket")) {
            val icon = data.find(name) ?: continue
            val v = IndsinIconView(this).apply {
                setIcon(icon)
                setIconColor(ContextCompat.getColor(this@LibrariesActivity, android.R.color.white))
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
                layoutParams = LinearLayout.LayoutParams(0, dp(28), 1f)
            }
            row.addView(v)
        }
    }

    private fun uploadLibrary(uri: Uri) {
        try {
            val lib = ProjectManager.importLibraryFromUri(this, project, uri)
            refresh()
            Toast.makeText(this, getString(R.string.library_uploaded_toast, lib.name), Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, "${getString(R.string.file_open_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun showCreateLibraryDialog() {
        val input = EditText(this)
        input.hint = getString(R.string.library_name_hint)
        input.inputType = InputType.TYPE_CLASS_TEXT
        AlertDialog.Builder(this)
            .setTitle(R.string.new_library_title)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                try {
                    val lib = ProjectManager.createLibrary(project, name)
                    refresh()
                    openLibraryInEditor(lib)
                } catch (t: Throwable) {
                    Toast.makeText(this, t.message ?: getString(R.string.file_save_error), Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showRenameLibraryDialog(lib: RinLibrary) {
        // نفس أسلوب showRenameFileDialog/showRenameFolderDialog تماماً في FilesActivity.kt، مع فارق
        // واحد: نُحدِّد الاسم الأساسي فقط (بلا امتداد ".og.rin" المزدوج) مسبقاً في الحقل، لأن
        // ProjectManager.renameLibrary() يُسقِط هذا الامتداد ويعيد إضافته دائماً بنفسه.
        val baseName = lib.name.removeSuffix(".og.rin")
        val input = EditText(this)
        input.setText(baseName)
        input.setSelection(0, baseName.length)
        AlertDialog.Builder(this)
            .setTitle(R.string.rename_library_title)
            .setView(input)
            .setPositiveButton(R.string.rename) { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isEmpty() || newName == baseName) return@setPositiveButton
                try {
                    val renamed = ProjectManager.renameLibrary(project, lib, newName)
                    refresh()
                    Toast.makeText(this, getString(R.string.library_renamed_toast, renamed.name), Toast.LENGTH_SHORT).show()
                } catch (t: Throwable) {
                    Toast.makeText(this, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showDeleteConfirm(lib: RinLibrary) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_library_title)
            .setMessage(getString(R.string.delete_library_confirm, lib.name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectManager.deleteLibrary(lib)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يفتح مكتبة المستخدم في نفس محرّر الشيفرة (MainActivity)، للاستفادة من التلوين النحوي والتراجع/الإعادة. */
    private fun openLibraryInEditor(lib: RinLibrary) {
        val intent = Intent(this, MainActivity::class.java)
        intent.putExtra(MainActivity.EXTRA_PROJECT_NAME, project.name)
        intent.putExtra(MainActivity.EXTRA_LIBRARY_NAME, lib.name)
        startActivity(intent)
    }

    /** يُعيد سطر @import الجاهز إلى المحرر الذي فتح هذه الشاشة، ثم يغلقها. */
    private fun finishWithImport(importStatement: String) {
        val result = Intent()
        result.putExtra(EXTRA_IMPORT_STATEMENT, importStatement)
        setResult(RESULT_OK, result)
        finish()
    }
}

/** بادئة المسار الذي تُحفَظ تحته كل مكتبات المستخدم فعلياً، لبناء سطر @import الصحيح. */
private const val ProjectManagerLibPrefix = "lib/"

private class UserLibraryAdapter(
    val onEdit: (RinLibrary) -> Unit,
    val onRename: (RinLibrary) -> Unit,
    val onDelete: (RinLibrary) -> Unit,
    val onInsert: (RinLibrary) -> Unit,
    val onPublish: (RinLibrary) -> Unit
) : RecyclerView.Adapter<UserLibraryAdapter.VH>() {

    private var items: List<RinLibrary> = emptyList()
    private val dateFormat = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)

    fun submit(newItems: List<RinLibrary>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtLibraryNameItem)
        val txtMeta: TextView = view.findViewById(R.id.txtLibraryMeta)
        val btnDelete: View = view.findViewById(R.id.btnDeleteLibrary)
        val btnRename: View = view.findViewById(R.id.btnRenameLibrary)
        val btnEdit: View = view.findViewById(R.id.btnEditLibrary)
        val btnInsert: View = view.findViewById(R.id.btnInsertLibrary)
        val btnPublish: View = view.findViewById(R.id.btnPublishLibrary)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_library, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val lib = items[position]
        holder.txtName.text = lib.name
        holder.txtMeta.text = holder.itemView.context.getString(
            R.string.file_meta_format,
            formatSize(lib.sizeBytes),
            dateFormat.format(Date(lib.lastModified))
        )
        holder.btnEdit.setOnClickListener { onEdit(lib) }
        holder.btnRename.setOnClickListener { onRename(lib) }
        holder.btnDelete.setOnClickListener { onDelete(lib) }
        holder.btnInsert.setOnClickListener { onInsert(lib) }
        holder.btnPublish.setOnClickListener { onPublish(lib) }
    }

    override fun getItemCount(): Int = items.size

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> "${bytes / (1024 * 1024)} MB"
    }
}

private class BuiltinLibraryAdapter(
    val onInsert: (BuiltinLibraryInfo) -> Unit
) : RecyclerView.Adapter<BuiltinLibraryAdapter.VH>() {

    private var items: List<BuiltinLibraryInfo> = emptyList()

    fun submit(newItems: List<BuiltinLibraryInfo>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val imgIcon: ImageView = view.findViewById(R.id.imgBuiltinIcon)
        val txtName: TextView = view.findViewById(R.id.txtBuiltinNameItem)
        val txtDescription: TextView = view.findViewById(R.id.txtBuiltinDescription)
        val txtFunctions: TextView = view.findViewById(R.id.txtBuiltinFunctions)
        val txtCategory: TextView = view.findViewById(R.id.txtBuiltinCategory)
        val btnInsert: View = view.findViewById(R.id.btnInsertBuiltin)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_builtin_library, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val info = items[position]
        holder.imgIcon.setImageResource(info.iconRes)
        holder.txtName.text = info.importPath
        holder.txtDescription.text = info.description
        holder.txtFunctions.text = info.sampleFunctions
        holder.txtCategory.setText(info.category.labelRes)
        holder.btnInsert.setOnClickListener { onInsert(info) }
    }

    override fun getItemCount(): Int = items.size
}
