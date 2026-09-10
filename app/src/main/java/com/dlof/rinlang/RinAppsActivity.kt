package com.dlof.rinlang

import android.app.AlertDialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "تطبيقات Rin": سجل كل عمليات تصدير APK الناجحة ([RinAppsRegistry]) — تثبيت/تفاصيل/
 * مشاركة/حفظ/حذف/إعادة تصدير لكل تطبيق، وزر عائم لبدء تصدير مشروع جديد (يفتح
 * [ApkExportActivity] بعد اختيار المشروع من قائمة سريعة).
 */
class RinAppsActivity : AppCompatActivity() {

    private lateinit var rv: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var txtCount: TextView
    private lateinit var adapter: RinAppsAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_rin_apps)

        findViewById<TextView>(R.id.txtToolbarTitle)?.text = getString(R.string.rin_apps_screen_title)
        findViewById<ImageButton>(R.id.btnToolbarBack)?.setOnClickListener { finish() }

        rv = findViewById(R.id.rvRinApps)
        txtEmpty = findViewById(R.id.txtEmptyRinApps)
        txtCount = findViewById(R.id.txtRinAppsCount)

        adapter = RinAppsAdapter(
            onInstall = { r -> ApkInstallActions.install(this, r.apkFile(this)) },
            onDetails = { r -> startActivity(RinAppSettingsActivity.intentFor(this, r.id)) },
            onMore = { r, anchor -> showMoreMenu(r, anchor) }
        )
        rv.layoutManager = LinearLayoutManager(this)
        rv.adapter = adapter

        findViewById<View>(R.id.fabRinAppsNewExport).setOnClickListener { showProjectPicker() }
    }

    override fun onResume() {
        super.onResume()
        reload()
    }

    private fun reload() {
        val records = RinAppsRegistry.listAll(this)
        adapter.submit(records)
        txtEmpty.visibility = if (records.isEmpty()) View.VISIBLE else View.GONE
        rv.visibility = if (records.isEmpty()) View.GONE else View.VISIBLE
        txtCount.text = resources.getQuantityStringSafe(records.size)
    }

    private fun showMoreMenu(record: RinAppRecord, anchor: View) {
        val popup = PopupMenu(this, anchor)
        popup.menuInflater.inflate(R.menu.menu_rin_app_actions, popup.menu)
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.actionRinAppShare -> { ApkInstallActions.share(this, record.apkFile(this)); true }
                R.id.actionRinAppSave -> { ApkInstallActions.saveToDownloads(this, record.apkFile(this)); true }
                R.id.actionRinAppReExport -> { startExportFor(record.projectName); true }
                R.id.actionRinAppDelete -> { confirmDelete(record); true }
                else -> false
            }
        }
        popup.show()
    }

    private fun confirmDelete(record: RinAppRecord) {
        AlertDialog.Builder(this)
            .setTitle(record.displayName.ifBlank { record.projectName })
            .setMessage(R.string.rin_app_delete_confirm)
            .setPositiveButton(R.string.rin_app_action_delete) { _, _ ->
                RinAppsRegistry.remove(this, record.id)
                reload()
                Toast.makeText(this, R.string.rin_app_deleted_toast, Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showProjectPicker() {
        val projects = ProjectManager.listProjects(this)
        if (projects.isEmpty()) {
            Toast.makeText(this, R.string.more_row_needs_project_toast, Toast.LENGTH_SHORT).show()
            return
        }
        val names = projects.map { it.name }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle(R.string.apk_export_screen_title)
            .setItems(names) { _, which -> startExportFor(names[which]) }
            .show()
    }

    private fun startExportFor(projectName: String) {
        val exists = ProjectManager.listProjects(this).any { it.name == projectName }
        if (!exists) {
            Toast.makeText(this, R.string.more_row_needs_project_toast, Toast.LENGTH_SHORT).show()
            return
        }
        val intent = android.content.Intent(this, ApkExportActivity::class.java)
        intent.putExtra(ApkExportActivity.EXTRA_PROJECT_NAME, projectName)
        startActivity(intent)
    }

    /** يبني نصّاً بسيطاً "N تطبيقات مُصدَّرة" بلا الحاجة لموارد plurals مخصّصة لكل لغة. */
    private fun android.content.res.Resources.getQuantityStringSafe(count: Int): String =
        getString(R.string.rin_apps_count_format, count)

    private class RinAppsAdapter(
        private val onInstall: (RinAppRecord) -> Unit,
        private val onDetails: (RinAppRecord) -> Unit,
        private val onMore: (RinAppRecord, View) -> Unit
    ) : RecyclerView.Adapter<RinAppsAdapter.VH>() {

        private val items = ArrayList<RinAppRecord>()

        fun submit(records: List<RinAppRecord>) {
            items.clear()
            items.addAll(records)
            notifyDataSetChanged()
        }

        class VH(view: View) : RecyclerView.ViewHolder(view) {
            val name: TextView = view.findViewById(R.id.txtRinAppName)
            val meta: TextView = view.findViewById(R.id.txtRinAppMeta)
            val btnInstall: View = view.findViewById(R.id.btnRinAppInstall)
            val btnDetails: View = view.findViewById(R.id.btnRinAppDetails)
            val btnMore: View = view.findViewById(R.id.btnRinAppMore)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_rin_app, parent, false)
            return VH(view)
        }

        override fun onBindViewHolder(holder: VH, position: Int) {
            val record = items[position]
            val ctx = holder.itemView.context
            holder.name.text = record.displayName.ifBlank { record.projectName }
            val sizeKb = record.sizeBytes / 1024
            val dateStr = DateFormat.getDateInstance(DateFormat.SHORT).format(Date(record.exportedAt))
            val sigTag = if (record.signedWithV2) "v1+v2" else "v1"
            holder.meta.text = ctx.getString(R.string.rin_app_meta_format, record.applicationId, sizeKb, dateStr, sigTag)
            holder.btnInstall.setOnClickListener { onInstall(record) }
            holder.btnDetails.setOnClickListener { onDetails(record) }
            holder.btnMore.setOnClickListener { onMore(record, it) }
        }

        override fun getItemCount(): Int = items.size
    }
}
