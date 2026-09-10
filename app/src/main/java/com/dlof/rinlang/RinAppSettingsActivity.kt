package com.dlof.rinlang

import android.app.AlertDialog
import android.os.Bundle
import android.widget.Button
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.apk.RinSigningIdentity
import java.text.DateFormat
import java.util.Date

/**
 * شاشة "إعدادات" تطبيق Rin واحد مُصدَّر مسبقاً (سجل [RinAppsRegistry]) — تفاصيله الكاملة
 * وإجراءاته. تُفتَح من بطاقة التطبيق في [RinAppsActivity].
 */
class RinAppSettingsActivity : AppCompatActivity() {

    private lateinit var record: RinAppRecord

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_rin_app_settings)

        val id = intent.getStringExtra(EXTRA_APP_ID)
        val found = id?.let { RinAppsRegistry.find(this, it) }
        if (found == null) {
            Toast.makeText(this, R.string.rin_app_not_found_toast, Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        record = found

        findViewById<TextView>(R.id.txtToolbarTitle)?.text = getString(R.string.rin_app_settings_screen_title)
        findViewById<ImageButton>(R.id.btnToolbarBack)?.setOnClickListener { finish() }

        findViewById<TextView>(R.id.txtRinAppSettingsName).text = record.displayName.ifBlank { record.projectName }

        detailRow(R.id.rowRinAppProject, R.string.rin_app_detail_project, record.projectName)
        detailRow(R.id.rowRinAppPackageId, R.string.rin_app_detail_package_id, record.applicationId)
        detailRow(R.id.rowRinAppEntryFile, R.string.rin_app_detail_entry_file, record.entryFile)
        detailRow(R.id.rowRinAppSize, R.string.rin_app_detail_size, "${record.sizeBytes / 1024} KB")
        detailRow(
            R.id.rowRinAppDate,
            R.string.rin_app_detail_date,
            DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(record.exportedAt))
        )
        detailRow(
            R.id.rowRinAppSigning,
            R.string.rin_app_detail_signing,
            if (record.signedWithV2) getString(R.string.rin_app_signing_v1_v2) else getString(R.string.rin_app_signing_v1_only)
        )
        val fingerprint = RinSigningIdentity.currentFingerprintOrNull(this) ?: getString(R.string.rin_app_detail_unavailable)
        detailRow(R.id.rowRinAppFingerprint, R.string.rin_app_detail_fingerprint, fingerprint)

        findViewById<Button>(R.id.btnRinAppSettingsInstall).setOnClickListener {
            ApkInstallActions.install(this, record.apkFile(this))
        }
        findViewById<Button>(R.id.btnRinAppSettingsShare).setOnClickListener {
            ApkInstallActions.share(this, record.apkFile(this))
        }
        findViewById<Button>(R.id.btnRinAppSettingsSave).setOnClickListener {
            ApkInstallActions.saveToDownloads(this, record.apkFile(this))
        }
        findViewById<Button>(R.id.btnRinAppSettingsReExport).setOnClickListener {
            val exists = ProjectManager.listProjects(this).any { it.name == record.projectName }
            if (!exists) {
                Toast.makeText(this, R.string.more_row_needs_project_toast, Toast.LENGTH_SHORT).show()
            } else {
                val intent = android.content.Intent(this, ApkExportActivity::class.java)
                intent.putExtra(ApkExportActivity.EXTRA_PROJECT_NAME, record.projectName)
                startActivity(intent)
            }
        }
        findViewById<Button>(R.id.btnRinAppSettingsDelete).setOnClickListener { confirmDelete() }
    }

    private fun detailRow(includeId: Int, labelRes: Int, value: String) {
        val row = findViewById<android.view.View>(includeId)
        row.findViewById<TextView>(R.id.detailLabel).text = getString(labelRes)
        row.findViewById<TextView>(R.id.detailValue).text = value.ifBlank { getString(R.string.rin_app_detail_unavailable) }
    }

    private fun confirmDelete() {
        AlertDialog.Builder(this)
            .setTitle(record.displayName.ifBlank { record.projectName })
            .setMessage(R.string.rin_app_delete_confirm)
            .setPositiveButton(R.string.rin_app_action_delete) { _, _ ->
                RinAppsRegistry.remove(this, record.id)
                Toast.makeText(this, R.string.rin_app_deleted_toast, Toast.LENGTH_SHORT).show()
                finish()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    companion object {
        private const val EXTRA_APP_ID = "extra_app_id"
        fun intentFor(context: android.content.Context, appId: String): android.content.Intent =
            android.content.Intent(context, RinAppSettingsActivity::class.java).putExtra(EXTRA_APP_ID, appId)
    }
}
