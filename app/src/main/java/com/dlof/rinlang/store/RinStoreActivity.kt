package com.dlof.rinlang.store

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R

/**
 * شاشة "متجر Rin": تصفح كل الحزم المنشورة من كل المستخدمين (قراءة عامة، لا تحتاج تسجيل
 * دخول)، وتثبيت أي حزمة داخل lib/ الخاص بالمشروع الحالي بضغطة واحدة.
 */
class RinStoreActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** النتيجة المُعادة عند التثبيت: سطر @import الجاهز، لتُدرَج مباشرة في المحرر بضغطة واحدة. */
        const val EXTRA_IMPORT_STATEMENT = "extra_import_statement"
    }

    private lateinit var project: Project
    private lateinit var rvPackages: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var adapter: PackageAdapter

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

        adapter = PackageAdapter { pkg -> installPackage(pkg) }
        rvPackages.layoutManager = LinearLayoutManager(this)
        rvPackages.adapter = adapter

        loadPackages()
    }

    private fun loadPackages() {
        PackageRepository.fetchAllPackages { packages ->
            adapter.submit(packages)
            txtEmpty.visibility = if (packages.isEmpty()) View.VISIBLE else View.GONE
        }
    }

    private fun installPackage(pkg: RinPackage) {
        try {
            val library = PackagingUtils.installPackage(this, project, pkg)
            PackageRepository.incrementDownloadCount(pkg.id)
            val importStatement = "@import \"lib/${library.name}\";"
            Toast.makeText(this, getString(R.string.package_installed_import_toast, library.name), Toast.LENGTH_SHORT).show()
            val result = Intent()
            result.putExtra(EXTRA_IMPORT_STATEMENT, importStatement)
            setResult(RESULT_OK, result)
            finish()
        } catch (t: Throwable) {
            Toast.makeText(this, t.message ?: "فشل التثبيت", Toast.LENGTH_LONG).show()
        }
    }
}

private class PackageAdapter(
    val onInstall: (RinPackage) -> Unit
) : RecyclerView.Adapter<PackageAdapter.VH>() {

    private var items: List<RinPackage> = emptyList()

    fun submit(newItems: List<RinPackage>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtPackageName)
        val txtMeta: TextView = view.findViewById(R.id.txtPackageMeta)
        val txtDescription: TextView = view.findViewById(R.id.txtPackageDescription)
        val btnInstall: View = view.findViewById(R.id.btnInstallPackage)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_store_package, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val pkg = items[position]
        val context = holder.itemView.context
        holder.txtName.text = "${pkg.name} — ${pkg.publisherName}"
        holder.txtMeta.text = context.getString(
            R.string.store_item_meta_format, pkg.version, pkg.license, pkg.downloadCount
        )
        holder.txtDescription.text = pkg.description.ifBlank { "" }
        holder.txtDescription.visibility = if (pkg.description.isBlank()) View.GONE else View.VISIBLE
        holder.btnInstall.setOnClickListener { onInstall(pkg) }
    }

    override fun getItemCount(): Int = items.size
}
