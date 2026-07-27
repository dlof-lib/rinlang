package com.dlof.rinlang.store.extensions

import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.R
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.widgets.ShimmerLayout
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

/**
 * الصفحة الرئيسية لـ "Rin Extensions Marketplace": متجر رسمي منفصل عن متجر المكتبات
 * ([com.dlof.rinlang.store.RinStoreActivity])، مخصَّص لإضافات وأدوات محرّر ولغة Rin
 * (إضافات، مكتبات، سمات، عناصر واجهة، أدوات تصحيح، أدوات AI، وقوالب).
 */
class RinExtensionsMarketplaceActivity : BaseConnectivityActivity() {

    companion object {
        private const val CATEGORY_ALL = "__all__"
    }

    private lateinit var rvExtensions: RecyclerView
    private lateinit var shimmerSkeleton: ShimmerLayout
    private lateinit var txtEmpty: View
    private lateinit var adapter: ExtensionAdapter
    private lateinit var edtSearch: EditText
    private lateinit var chipGroupType: ChipGroup
    private lateinit var layoutCli: View
    private lateinit var txtCliOutput: TextView
    private lateinit var edtCliCommand: EditText

    private var allExtensions: List<RinExtension> = emptyList()
    private var selectedType: String = CATEGORY_ALL

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_rin_extensions_marketplace)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.ext_marketplace_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvExtensions = findViewById(R.id.rvExtensions)
        shimmerSkeleton = findViewById(R.id.shimmerExtSkeleton)
        txtEmpty = findViewById(R.id.txtExtEmpty)
        edtSearch = findViewById(R.id.edtExtSearch)
        chipGroupType = findViewById(R.id.chipGroupExtType)
        layoutCli = findViewById(R.id.layoutExtCli)
        txtCliOutput = findViewById(R.id.txtExtCliOutput)
        edtCliCommand = findViewById(R.id.edtExtCliCommand)

        adapter = ExtensionAdapter(
            isInstalled = { ext -> ExtensionManager.isInstalled(this, ext.id) },
            onOpenDetail = { ext -> openDetail(ext) },
            onQuickAction = { ext -> openDetail(ext) }
        )
        rvExtensions.layoutManager = LinearLayoutManager(this)
        rvExtensions.adapter = adapter

        edtSearch.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = applyFilters()
        })

        findViewById<View>(R.id.btnExtCli).setOnClickListener {
            layoutCli.visibility = if (layoutCli.visibility == View.VISIBLE) View.GONE else View.VISIBLE
        }
        findViewById<View>(R.id.btnExtCliRun).setOnClickListener { runCliCommand() }
        edtCliCommand.setOnEditorActionListener { _, _, _ -> runCliCommand(); true }

        runIfOnline { loadExtensions() }
    }

    override fun onResume() {
        super.onResume()
        adapter.notifyDataSetChanged() // يعكس أي تثبيت/إزالة تمّت في شاشة التفاصيل
    }

    override fun onConnectionRestored() {
        loadExtensions()
    }

    private fun runCliCommand() {
        val raw = edtCliCommand.text?.toString()?.trim().orEmpty()
        if (raw.isEmpty()) return
        RinExtCliCommands.execute(this, raw) { lines ->
            val previous = txtCliOutput.text.toString()
            txtCliOutput.text = (previous + "\n$ $raw\n" + lines.joinToString("\n")).takeLast(4000)
            edtCliCommand.setText("")
            adapter.notifyDataSetChanged()
        }
    }

    private fun loadExtensions() {
        showSkeleton()
        ExtensionRepository.fetchAll { extensions ->
            hideSkeleton()
            allExtensions = extensions
            rebuildTypeChips()
            applyFilters()
        }
    }

    private fun showSkeleton() {
        shimmerSkeleton.visibility = View.VISIBLE
        shimmerSkeleton.startShimmer()
        rvExtensions.visibility = View.GONE
        txtEmpty.visibility = View.GONE
    }

    private fun hideSkeleton() {
        shimmerSkeleton.stopShimmer()
        shimmerSkeleton.visibility = View.GONE
        rvExtensions.visibility = View.VISIBLE
    }

    private fun typeLabel(type: ExtensionType): String = when (type) {
        ExtensionType.EXTENSION -> getString(R.string.ext_type_extension)
        ExtensionType.LIBRARY -> getString(R.string.ext_type_library)
        ExtensionType.THEME -> getString(R.string.ext_type_theme)
        ExtensionType.UI_COMPONENT -> getString(R.string.ext_type_ui_component)
        ExtensionType.DEBUG_TOOL -> getString(R.string.ext_type_debug_tool)
        ExtensionType.AI_TOOL -> getString(R.string.ext_type_ai_tool)
        ExtensionType.TEMPLATE -> getString(R.string.ext_type_template)
    }

    private fun rebuildTypeChips() {
        chipGroupType.removeAllViews()
        val presentTypes = allExtensions.map { it.extensionType }.distinct()
        val allChip = Chip(this).apply {
            text = getString(R.string.store_category_all)
            isCheckable = true
            isChecked = selectedType == CATEGORY_ALL
            setOnClickListener { selectedType = CATEGORY_ALL; refreshChipChecks(); applyFilters() }
        }
        chipGroupType.addView(allChip)
        presentTypes.forEach { type ->
            val chip = Chip(this).apply {
                text = typeLabel(type)
                isCheckable = true
                isChecked = selectedType == type.id
                setOnClickListener { selectedType = type.id; refreshChipChecks(); applyFilters() }
            }
            chipGroupType.addView(chip)
        }
    }

    private fun refreshChipChecks() {
        for (i in 0 until chipGroupType.childCount) {
            (chipGroupType.getChildAt(i) as? Chip)?.isChecked = false
        }
    }

    private fun applyFilters() {
        val query = edtSearch.text?.toString()?.trim().orEmpty()
        val result = allExtensions.filter { ext ->
            (selectedType == CATEGORY_ALL || ext.type == selectedType) &&
                (query.isEmpty() ||
                    ext.name.contains(query, ignoreCase = true) ||
                    ext.description.contains(query, ignoreCase = true) ||
                    ext.developer.contains(query, ignoreCase = true))
        }
        adapter.submit(result)
        txtEmpty.visibility = if (result.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun openDetail(ext: RinExtension) {
        startActivity(Intent(this, ExtensionDetailActivity::class.java).putExtra(ExtensionDetailActivity.EXTRA_EXTENSION, ext))
    }

    /** محوّل قائمة الإضافات، بنفس هوية PackageAdapter في متجر الحزم. */
    private inner class ExtensionAdapter(
        private val isInstalled: (RinExtension) -> Boolean,
        private val onOpenDetail: (RinExtension) -> Unit,
        private val onQuickAction: (RinExtension) -> Unit
    ) : RecyclerView.Adapter<ExtensionAdapter.VH>() {

        private var items: List<RinExtension> = emptyList()

        fun submit(newItems: List<RinExtension>) {
            items = newItems
            notifyDataSetChanged()
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_rin_extension, parent, false)
            return VH(view)
        }

        override fun getItemCount(): Int = items.size

        override fun onBindViewHolder(holder: VH, position: Int) {
            val ext = items[position]
            holder.txtInitial.text = ext.name.take(1).uppercase()
            holder.txtName.text = ext.name
            holder.txtType.text = typeLabel(ext.extensionType)
            holder.txtMeta.text = getString(R.string.ext_item_meta_format, ext.version, ext.developer, ext.downloadCount)
            holder.txtDescription.text = ext.description
            holder.txtRating.text = if (ext.ratingCount > 0)
                getString(R.string.store_rating_format, ext.averageRating, ext.ratingCount)
            else getString(R.string.store_rating_none)

            holder.btnAction.text = if (isInstalled(ext)) getString(R.string.ext_action_open) else getString(R.string.action_install)
            holder.btnAction.setOnClickListener { onQuickAction(ext) }
            holder.root.setOnClickListener { onOpenDetail(ext) }
        }

        inner class VH(view: View) : RecyclerView.ViewHolder(view) {
            val root: View = view.findViewById(R.id.rootExtensionCard)
            val txtInitial: TextView = view.findViewById(R.id.txtExtInitial)
            val txtName: TextView = view.findViewById(R.id.txtExtName)
            val txtType: TextView = view.findViewById(R.id.chipExtType)
            val txtMeta: TextView = view.findViewById(R.id.txtExtMeta)
            val txtDescription: TextView = view.findViewById(R.id.txtExtDescription)
            val txtRating: TextView = view.findViewById(R.id.txtExtRating)
            val btnAction: android.widget.Button = view.findViewById(R.id.btnExtAction)
        }
    }
}
