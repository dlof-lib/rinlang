package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.widgets.IndsinIconView
import com.dlof.rinlang.widgets.IndsinIcons

/**
 * متصفّح أيقونات indsin (مكتبة lib/indsinicons.og.rin) داخل RinStudio.
 * بحث بالاسم أو الاسم البديل + فلترة بالفئة + شبكة أيقونات حقيقية؛ الضغط يفتح التفاصيل ومنها
 * «إدراج في الكود» الذي يعيد سطر @view.Icon إلى المحرر عبر [LibrariesActivity.EXTRA_IMPORT_STATEMENT].
 */
class IconLibraryActivity : RinBaseActivity() {

    private lateinit var data: IndsinIcons.Data
    private lateinit var adapter: IconAdapter
    private lateinit var rv: RecyclerView
    private lateinit var tvCount: TextView
    private lateinit var tvEmpty: View
    private lateinit var llChips: LinearLayout
    private var query = ""
    private var category: String? = null
    private val chips = mutableListOf<Pair<TextView, String?>>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_icon_library)

        data = try {
            IndsinIcons.load(this)
        } catch (t: Throwable) {
            Toast.makeText(this, t.message ?: "icons.json", Toast.LENGTH_LONG).show()
            finish(); return
        }

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.icons_screen_title)
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            text = getString(R.string.icons_count_format, data.icons.size)
            visibility = View.VISIBLE
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        tvCount = findViewById(R.id.tvIconsCount)
        tvEmpty = findViewById(R.id.tvIconsEmpty)
        llChips = findViewById(R.id.llIconChips)
        rv = findViewById(R.id.rvIcons)
        adapter = IconAdapter { showDetail(it) }
        rv.layoutManager = GridLayoutManager(this, 4)
        rv.adapter = adapter

        findViewById<EditText>(R.id.etIconSearch).addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                query = s?.toString().orEmpty()
                applyFilter()
            }
        })

        buildChips()
        applyFilter()
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun buildChips() {
        llChips.removeAllViews()
        chips.clear()
        val isArabic = resources.configuration.locales[0].language == "ar"
        val entries = ArrayList<Pair<String, String?>>()
        entries.add(Pair(getString(R.string.libs_filter_all), null))
        for (c in data.categories) entries.add(Pair(if (isArabic) c.ar else c.en, c.id))
        for ((label, id) in entries) {
            val chip = TextView(this).apply {
                text = label
                textSize = 13f
                setPadding(dp(14), dp(7), dp(14), dp(7))
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT
                ).apply { marginEnd = dp(6) }
                setOnClickListener {
                    category = id
                    updateChips()
                    applyFilter()
                }
            }
            chips += chip to id
            llChips.addView(chip)
        }
        updateChips()
    }

    private fun updateChips() {
        for ((chip, id) in chips) {
            val on = id == category
            chip.isSelected = on
            chip.setBackgroundResource(if (on) R.drawable.bg_lib_chip_selected else R.drawable.bg_filter_chip_unselected)
            chip.setTextColor(
                if (on) ContextCompat.getColor(this, android.R.color.white)
                else ContextCompat.getColor(this, R.color.rin_on_toolbar)
            )
        }
    }

    private fun applyFilter() {
        val list = data.search(query, category)
        adapter.submit(list)
        tvCount.text = getString(R.string.icons_count_format, list.size)
        tvEmpty.visibility = if (list.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun showDetail(icon: IndsinIcons.Icon) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_icon_detail, null)
        view.findViewById<IndsinIconView>(R.id.iconBig).apply {
            setIcon(icon)
            setIconColor(ContextCompat.getColor(this@IconLibraryActivity, R.color.rin_accent))
        }
        view.findViewById<TextView>(R.id.tvDetailName).text = icon.name
        view.findViewById<TextView>(R.id.tvDetailAliases).apply {
            if (icon.aliases.isEmpty()) visibility = View.GONE
            else text = getString(R.string.icons_aliases_format, icon.aliases.joinToString("، "))
        }
        view.findViewById<TextView>(R.id.tvDetailSnippet).text = IndsinIcons.snippet(icon.name)

        AlertDialog.Builder(this)
            .setView(view)
            .setPositiveButton(R.string.icons_action_insert) { _, _ ->
                setResult(
                    RESULT_OK,
                    Intent().putExtra(LibrariesActivity.EXTRA_IMPORT_STATEMENT, IndsinIcons.snippet(icon.name))
                )
                finish()
            }
            .setNeutralButton(R.string.icons_action_copy_name) { _, _ ->
                val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("icon", icon.name))
                Toast.makeText(this, getString(R.string.icons_name_copied, icon.name), Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(R.string.icons_close, null)
            .show()
    }
}

private class IconAdapter(
    val onClick: (IndsinIcons.Icon) -> Unit
) : RecyclerView.Adapter<IconAdapter.VH>() {

    private var items: List<IndsinIcons.Icon> = emptyList()

    fun submit(list: List<IndsinIcons.Icon>) {
        items = list
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val icon: IndsinIconView = view.findViewById(R.id.iconView)
        val name: TextView = view.findViewById(R.id.tvIconName)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(LayoutInflater.from(parent.context).inflate(R.layout.item_icon_tile, parent, false))

    override fun onBindViewHolder(holder: VH, position: Int) {
        val ic = items[position]
        holder.icon.setIcon(ic)
        holder.icon.setIconColor(ContextCompat.getColor(holder.itemView.context, R.color.rin_on_toolbar))
        holder.name.text = ic.name
        holder.itemView.contentDescription = ic.name
        holder.itemView.setOnClickListener { onClick(ic) }
    }

    override fun getItemCount(): Int = items.size
}
