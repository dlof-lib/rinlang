package com.dlof.rinlang.store

import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageButton
import android.widget.PopupMenu
import android.widget.RatingBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

/**
 * شاشة "متجر Rin": تصفح كل الحزم المنشورة من كل المستخدمين (قراءة عامة، لا تحتاج تسجيل
 * دخول)، بحث + تصنيفات + فرز، وتثبيت أي حزمة داخل lib/ الخاص بالمشروع الحالي بضغطة واحدة
 * (مع التحقق من تبعياتها المعلَنة أولاً).
 */
class RinStoreActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** النتيجة المُعادة عند التثبيت: سطر (أو أسطر) @import الجاهزة، لتُدرَج مباشرة في المحرر. */
        const val EXTRA_IMPORT_STATEMENT = "extra_import_statement"
        private const val CATEGORY_ALL = "__all__"
    }

    private enum class SortMode(val labelRes: Int) {
        NEWEST(R.string.store_sort_newest),
        RATING(R.string.store_sort_rating),
        DOWNLOADS(R.string.store_sort_downloads),
        NAME(R.string.store_sort_name)
    }

    private lateinit var project: Project
    private lateinit var rvPackages: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var adapter: PackageAdapter
    private lateinit var edtSearch: EditText
    private lateinit var chipGroupCategory: ChipGroup
    private lateinit var btnSort: ImageButton

    private var allPackages: List<RinPackage> = emptyList()
    private var selectedCategory: String = CATEGORY_ALL
    private var sortMode: SortMode = SortMode.NEWEST
    /** تجنّب تثبيت نفس الحزمة أكثر من مرة أثناء حلّ التبعيات المتشابكة. */
    private val installedThisSession = mutableSetOf<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_rin_store)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME) ?: run { finish(); return }
        project = ProjectManager.listProjects(this).find { it.name == projectName }
            ?: run { finish(); return }

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.rin_store_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvPackages = findViewById(R.id.rvPackages)
        txtEmpty = findViewById(R.id.txtEmptyStore)
        edtSearch = findViewById(R.id.edtStoreSearch)
        chipGroupCategory = findViewById(R.id.chipGroupStoreCategory)
        btnSort = findViewById(R.id.btnStoreSort)

        adapter = PackageAdapter(
            onInstall = { pkg -> confirmAndInstall(pkg) },
            onRate = { pkg, ratingBar -> showRateDialog(pkg, ratingBar) },
            onReport = { pkg -> showReportDialog(pkg) }
        )
        rvPackages.layoutManager = LinearLayoutManager(this)
        rvPackages.adapter = adapter

        edtSearch.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = applyFilters()
        })

        btnSort.setOnClickListener { showSortMenu() }

        loadPackages()
    }

    private fun loadPackages() {
        PackageRepository.fetchAllPackages { packages ->
            allPackages = packages
            rebuildCategoryChips()
            applyFilters()
        }
    }

    private fun rebuildCategoryChips() {
        chipGroupCategory.removeAllViews()
        val categories = listOf(CATEGORY_ALL) + allPackages.map { it.category }.distinct().sorted()
        categories.forEach { category ->
            val chip = Chip(this).apply {
                text = if (category == CATEGORY_ALL) getString(R.string.store_category_all) else category
                isCheckable = true
                isChecked = category == selectedCategory
                setOnClickListener {
                    selectedCategory = category
                    for (i in 0 until chipGroupCategory.childCount) {
                        (chipGroupCategory.getChildAt(i) as? Chip)?.isChecked = false
                    }
                    isChecked = true
                    applyFilters()
                }
            }
            chipGroupCategory.addView(chip)
        }
    }

    private fun showSortMenu() {
        val popup = PopupMenu(this, btnSort)
        SortMode.values().forEach { mode -> popup.menu.add(0, mode.ordinal, 0, getString(mode.labelRes)) }
        popup.setOnMenuItemClickListener { item ->
            sortMode = SortMode.values()[item.itemId]
            applyFilters()
            true
        }
        popup.show()
    }

    private fun applyFilters() {
        val query = edtSearch.text?.toString()?.trim().orEmpty()
        var result = allPackages.filter { pkg ->
            (selectedCategory == CATEGORY_ALL || pkg.category == selectedCategory) &&
                (query.isEmpty() ||
                    pkg.name.contains(query, ignoreCase = true) ||
                    pkg.description.contains(query, ignoreCase = true) ||
                    pkg.publisherName.contains(query, ignoreCase = true))
        }
        result = when (sortMode) {
            SortMode.NEWEST -> result.sortedByDescending { it.createdAt }
            SortMode.RATING -> result.sortedByDescending { it.averageRating }
            SortMode.DOWNLOADS -> result.sortedByDescending { it.downloadCount }
            SortMode.NAME -> result.sortedBy { it.name.lowercase() }
        }
        adapter.submit(result)
        txtEmpty.visibility = if (result.isEmpty()) View.VISIBLE else View.GONE
    }

    /** يتحقّق من تبعيات الحزمة أولاً؛ فقط عند الرضا التام أو موافقة المستخدم يُثبِّت فعلياً. */
    private fun confirmAndInstall(pkg: RinPackage) {
        val check = DependencyResolver.check(project, pkg)
        if (check.isSatisfied) {
            installPackage(pkg)
            return
        }

        val message = StringBuilder(getString(R.string.dependency_check_message_prefix))
        check.missing.forEach { (name, req) ->
            // إن وُجدت في كتالوج المتجر حزمة بنفس الاسم تحقّق الشرط، اقترح تثبيتها تلقائياً أولاً.
            val candidate = DependencyResolver.findSatisfying(allPackages, name, req)
            if (candidate == null) {
                message.append(getString(R.string.dependency_missing_line, name, req)).append("\n")
            } else {
                message.append(getString(R.string.dependency_missing_line, name, req))
                    .append(" — ").append(candidate.version).append("\n")
            }
        }
        check.versionMismatch.forEach { (name, req, installed) ->
            message.append(getString(R.string.dependency_mismatch_line, name, req, installed)).append("\n")
        }

        AlertDialog.Builder(this)
            .setTitle(R.string.dependency_check_title)
            .setMessage(message.toString().trim())
            .setPositiveButton(R.string.action_install_with_dependencies) { _, _ ->
                installedThisSession.clear()
                installWithDependencies(pkg)
            }
            .setNeutralButton(R.string.action_install_anyway) { _, _ -> installPackage(pkg) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يثبّت تبعيات [pkg] المتوفرة في كتالوج المتجر أولاً (بعمق واحد لتفادي تكرار لا نهائي)، ثم [pkg] نفسها. */
    private fun installWithDependencies(pkg: RinPackage) {
        val importLines = mutableListOf<String>()
        val check = DependencyResolver.check(project, pkg)
        (check.missing.map { it.first to it.second } + check.versionMismatch.map { it.first to it.second })
            .distinctBy { it.first }
            .forEach { (name, requirement) ->
                if (name in installedThisSession) return@forEach
                val candidate = DependencyResolver.findSatisfying(allPackages, name, requirement)
                if (candidate != null) {
                    installedThisSession.add(name)
                    importLines.add(installQuietly(candidate))
                }
            }
        importLines.add(installQuietly(pkg))
        finishWithImports(importLines)
    }

    private fun installPackage(pkg: RinPackage) {
        try {
            finishWithImports(listOf(installQuietly(pkg)))
        } catch (t: Throwable) {
            Toast.makeText(this, t.message ?: "فشل التثبيت", Toast.LENGTH_LONG).show()
        }
    }

    /** يثبّت حزمة واحدة فعلياً ويرجع سطر @import الجاهز لها، بلا إغلاق للشاشة (يُستخدم للتبعيات المتعددة). */
    private fun installQuietly(pkg: RinPackage): String {
        val library = PackagingUtils.installPackage(this, project, pkg)
        PackageRepository.incrementDownloadCount(pkg.id)
        return "@import \"lib/${library.name}\";"
    }

    private fun finishWithImports(importStatements: List<String>) {
        if (importStatements.isEmpty()) return
        Toast.makeText(
            this,
            getString(R.string.package_installed_import_toast, importStatements.size.toString()),
            Toast.LENGTH_SHORT
        ).show()
        val result = Intent()
        result.putExtra(EXTRA_IMPORT_STATEMENT, importStatements.joinToString("\n"))
        setResult(RESULT_OK, result)
        finish()
    }

    private fun showRateDialog(pkg: RinPackage, anchorRatingBar: RatingBar) {
        val uid = AuthRepository.currentUid()
        if (uid == null) {
            Toast.makeText(this, R.string.rate_package_login_required, Toast.LENGTH_SHORT).show()
            return
        }
        val dialogRatingBar = RatingBar(this).apply {
            numStars = 5
            stepSize = 1f
            rating = 4f
        }
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.rate_package_title, pkg.name))
            .setView(dialogRatingBar)
            .setPositiveButton(R.string.action_rate_package) { _, _ ->
                val value = dialogRatingBar.rating.toInt().coerceIn(1, 5)
                PackageRepository.submitRating(pkg.id, uid, value) { success ->
                    if (success) {
                        Toast.makeText(this, R.string.rate_package_submitted, Toast.LENGTH_SHORT).show()
                        loadPackages()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يعرض قائمة أسباب جاهزة، ويرسل البلاغ المختار عبر [PackageRepository.submitReport]. */
    private fun showReportDialog(pkg: RinPackage) {
        val uid = AuthRepository.currentUid()
        if (uid == null) {
            Toast.makeText(this, R.string.report_package_login_required, Toast.LENGTH_SHORT).show()
            return
        }
        val reasons = resources.getStringArray(R.array.report_reasons)
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.report_package_title, pkg.name))
            .setMessage(R.string.report_package_message)
            .setItems(reasons) { _, index ->
                PackageRepository.submitReport(pkg.id, uid, reasons[index]) { success ->
                    if (success) {
                        Toast.makeText(this, R.string.report_package_submitted, Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

private class PackageAdapter(
    val onInstall: (RinPackage) -> Unit,
    val onRate: (RinPackage, RatingBar) -> Unit,
    val onReport: (RinPackage) -> Unit
) : RecyclerView.Adapter<PackageAdapter.VH>() {

    private var items: List<RinPackage> = emptyList()

    fun submit(newItems: List<RinPackage>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtInitial: TextView = view.findViewById(R.id.txtPackageInitial)
        val txtName: TextView = view.findViewById(R.id.txtPackageName)
        val txtMeta: TextView = view.findViewById(R.id.txtPackageMeta)
        val txtCategory: TextView = view.findViewById(R.id.txtPackageCategory)
        val txtDescription: TextView = view.findViewById(R.id.txtPackageDescription)
        val txtDependencies: TextView = view.findViewById(R.id.txtPackageDependencies)
        val ratingBar: RatingBar = view.findViewById(R.id.ratingBarPackage)
        val txtRating: TextView = view.findViewById(R.id.txtPackageRating)
        val btnRate: View = view.findViewById(R.id.btnRatePackage)
        val btnReport: View = view.findViewById(R.id.btnReportPackage)
        val btnInstall: View = view.findViewById(R.id.btnInstallPackage)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_store_package, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val pkg = items[position]
        val context = holder.itemView.context
        holder.txtInitial.text = pkg.name.take(1).uppercase()
        holder.txtName.text = "${pkg.name} — ${pkg.publisherName}"
        holder.txtMeta.text = context.getString(
            R.string.store_item_meta_format, pkg.version, pkg.license, pkg.downloadCount
        )
        holder.txtCategory.text = pkg.category
        holder.txtDescription.text = pkg.description.ifBlank { "" }
        holder.txtDescription.visibility = if (pkg.description.isBlank()) View.GONE else View.VISIBLE

        if (pkg.dependencies.isEmpty()) {
            holder.txtDependencies.visibility = View.GONE
        } else {
            holder.txtDependencies.visibility = View.VISIBLE
            val depsText = pkg.dependencies.entries.joinToString(", ") { "${it.key} ${it.value}" }
            holder.txtDependencies.text = context.getString(R.string.store_dependencies_format, depsText)
        }

        holder.ratingBar.rating = pkg.averageRating.toFloat()
        holder.txtRating.text = if (pkg.ratingCount > 0L) {
            context.getString(R.string.store_rating_format, pkg.averageRating, pkg.ratingCount)
        } else {
            context.getString(R.string.store_rating_none)
        }

        holder.btnInstall.setOnClickListener { onInstall(pkg) }
        holder.btnRate.setOnClickListener { onRate(pkg, holder.ratingBar) }
        holder.btnReport.setOnClickListener { onReport(pkg) }
    }

    override fun getItemCount(): Int = items.size
}
