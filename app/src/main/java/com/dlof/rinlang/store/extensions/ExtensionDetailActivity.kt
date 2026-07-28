package com.dlof.rinlang.store.extensions

import android.app.AlertDialog
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.Base64
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.network.NetworkMonitor
import com.dlof.rinlang.store.VersionUtils
import java.text.DateFormat
import java.util.Date

/**
 * صفحة تفاصيل إضافة واحدة داخل "Rin Extensions Marketplace": كل الحقول المطلوبة (الاسم،
 * الإصدار، المطوّر، الوصف، الأذونات، اللغات، لقطات الشاشة، سجل التحديثات، تاريخ الإصدار،
 * الحجم، التقييمات)، بالإضافة إلى شاشة أمان قبل التثبيت (الملفات/الأذونات/المطوّر/التوقيع
 * الرقمي) وأزرار تثبيت/تحديث/إزالة/تعطيل/تمكين.
 */
class ExtensionDetailActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_EXTENSION = "extra_extension"
    }

    private lateinit var ext: RinExtension

    private lateinit var txtName: TextView
    private lateinit var txtDeveloper: TextView
    private lateinit var txtMeta: TextView
    private lateinit var txtDescription: TextView
    private lateinit var txtLanguages: TextView
    private lateinit var layoutProgress: View
    private lateinit var txtProgressLabel: TextView
    private lateinit var progressBar: android.widget.ProgressBar
    private lateinit var btnInstall: android.widget.Button
    private lateinit var btnUpdate: android.widget.Button
    private lateinit var btnEnableDisable: android.widget.Button
    private lateinit var btnUninstall: android.widget.Button
    private lateinit var btnExportRinex: android.widget.Button
    private lateinit var btnDeleteExtension: android.widget.Button
    private lateinit var containerChangelog: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_extension_detail)

        @Suppress("DEPRECATION")
        ext = intent.getSerializableExtra(EXTRA_EXTENSION) as? RinExtension ?: run { finish(); return }

        findViewById<TextView>(R.id.txtToolbarTitle).text = ext.name
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        findViewById<TextView>(R.id.txtDetailInitial).text = ext.name.take(1).uppercase()
        txtName = findViewById(R.id.txtDetailName)
        txtDeveloper = findViewById(R.id.txtDetailDeveloper)
        txtMeta = findViewById(R.id.txtDetailMeta)
        txtDescription = findViewById(R.id.txtDetailDescription)
        txtLanguages = findViewById(R.id.txtDetailLanguages)
        layoutProgress = findViewById(R.id.layoutInstallProgress)
        txtProgressLabel = findViewById(R.id.txtInstallProgressLabel)
        progressBar = findViewById(R.id.progressInstall)
        btnInstall = findViewById(R.id.btnDetailInstall)
        btnUpdate = findViewById(R.id.btnDetailUpdate)
        btnEnableDisable = findViewById(R.id.btnDetailEnableDisable)
        btnUninstall = findViewById(R.id.btnDetailUninstall)
        btnExportRinex = findViewById(R.id.btnDetailExportRinex)
        btnDeleteExtension = findViewById(R.id.btnDetailDeleteExtension)
        containerChangelog = findViewById(R.id.containerChangelog)

        txtName.text = ext.name
        txtDeveloper.text = getString(R.string.ext_detail_developer_format, ext.developer)
        txtDescription.text = ext.description
        txtLanguages.text = if (ext.languages.isNotEmpty())
            getString(R.string.ext_detail_languages_format, ext.languages.joinToString("، "))
        else ""

        bindScreenshots()
        bindChangelog()
        refreshMetaAndButtons()

        btnInstall.setOnClickListener { showSecurityDialogThenInstall(isUpdate = false) }
        btnUpdate.setOnClickListener { showSecurityDialogThenInstall(isUpdate = true) }
        btnEnableDisable.setOnClickListener { toggleEnabled() }
        btnUninstall.setOnClickListener { confirmUninstall() }
        btnExportRinex.setOnClickListener { exportAsRinex() }
        btnDeleteExtension.setOnClickListener { confirmDeleteExtension() }

        // زر الحذف النهائي من المتجر يظهر فقط لصاحب الإضافة (developerUid) — وليس لكل من ثبَّتها.
        btnDeleteExtension.visibility =
            if (ext.developerUid.isNotBlank() && ext.developerUid == AuthRepository.currentUid()) View.VISIBLE else View.GONE
    }

    override fun onResume() {
        super.onResume()
        refreshMetaAndButtons()
    }

    private fun formatBytes(bytes: Long): String {
        if (bytes < 1024) return "$bytes B"
        val kb = bytes / 1024.0
        if (kb < 1024) return "%.1f KB".format(kb)
        return "%.1f MB".format(kb / 1024.0)
    }

    private fun refreshMetaAndButtons() {
        val installed = ExtensionManager.installedRecord(this, ext.id)
        val dateStr = if (ext.releaseDate > 0)
            DateFormat.getDateInstance().format(Date(ext.releaseDate)) else "—"

        txtMeta.text = getString(
            R.string.ext_detail_meta_format,
            ext.version, formatBytes(ext.sizeBytes), dateStr, ext.downloadCount,
            if (ext.ratingCount > 0) getString(R.string.store_rating_format, ext.averageRating, ext.ratingCount)
            else getString(R.string.store_rating_none)
        ) + if (ext.downloadCount >= 20L || (ext.ratingCount >= 3L && ext.averageRating >= 4.5))
            "  •  " + getString(R.string.ext_badge_featured) else ""

        if (installed == null) {
            btnInstall.visibility = View.VISIBLE
            btnUpdate.visibility = View.GONE
            btnEnableDisable.visibility = View.GONE
            btnUninstall.visibility = View.GONE
            return
        }

        btnInstall.visibility = View.GONE
        btnUninstall.visibility = View.VISIBLE
        btnEnableDisable.visibility = View.VISIBLE
        btnEnableDisable.text = if (installed.enabled) getString(R.string.ext_action_disable) else getString(R.string.ext_action_enable)

        val hasUpdate = VersionUtils.compare(ext.version, installed.version) > 0
        btnUpdate.visibility = if (hasUpdate) View.VISIBLE else View.GONE
    }

    private fun bindScreenshots() {
        if (ext.screenshots.isEmpty()) return
        findViewById<View>(R.id.txtScreenshotsHeader).visibility = View.VISIBLE
        val rv = findViewById<RecyclerView>(R.id.rvScreenshots)
        rv.visibility = View.VISIBLE
        rv.layoutManager = LinearLayoutManager(this, LinearLayoutManager.HORIZONTAL, false)
        rv.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
                val v = LayoutInflater.from(parent.context).inflate(R.layout.item_extension_screenshot, parent, false)
                return object : RecyclerView.ViewHolder(v) {}
            }
            override fun getItemCount(): Int = ext.screenshots.size
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val shot = ext.screenshots[position]
                val img = holder.itemView.findViewById<ImageView>(R.id.imgScreenshot)
                val caption = holder.itemView.findViewById<TextView>(R.id.txtScreenshotCaption)
                caption.text = shot.caption
                try {
                    val bytes = Base64.decode(shot.base64Data, Base64.DEFAULT)
                    img.setImageBitmap(BitmapFactory.decodeByteArray(bytes, 0, bytes.size))
                } catch (_: Throwable) {
                    img.setImageDrawable(null)
                }
            }
        }
    }

    private fun bindChangelog() {
        containerChangelog.removeAllViews()
        if (ext.changelog.isEmpty()) {
            findViewById<View>(R.id.txtNoChangelog).visibility = View.VISIBLE
            return
        }
        ext.changelog.sortedByDescending { it.date }.forEach { entry ->
            val row = TextView(this).apply {
                val dateStr = if (entry.date > 0) DateFormat.getDateInstance().format(Date(entry.date)) else ""
                text = getString(R.string.ext_changelog_entry_format, entry.version, dateStr, entry.notes)
                setTextColor(getColorCompat(R.color.rin_editor_text))
                textSize = 12.5f
                setPadding(0, 0, 0, dpToPx(10))
            }
            containerChangelog.addView(row)
        }
    }

    private fun dpToPx(dp: Int): Int = (dp * resources.displayMetrics.density).toInt()
    private fun getColorCompat(colorRes: Int): Int = androidx.core.content.ContextCompat.getColor(this, colorRes)

    /** يبني شاشة الأمان (الأذونات + التوقيع + المطوّر + الملفات) ويعرضها قبل التثبيت/التحديث فعلياً. */
    private fun showSecurityDialogThenInstall(isUpdate: Boolean) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_extension_security, null)

        view.findViewById<TextView>(R.id.txtSecurityDeveloper).text = ext.developer.ifBlank { getString(R.string.ext_detail_developer_unknown) }

        val signatureValid = ext.signature.isBlank() || ExtensionPermissions.verifySignature(ext.base64Data, ext.signature)
        view.findViewById<TextView>(R.id.txtSecuritySignature).text =
            if (ext.signature.isNotBlank()) ExtensionPermissions.shortSignature(ext.signature) else getString(R.string.ext_security_signature_missing)
        val stateView = view.findViewById<TextView>(R.id.txtSecuritySignatureState)
        if (ext.signature.isBlank()) {
            stateView.text = getString(R.string.ext_security_signature_missing_state)
            stateView.setTextColor(getColorCompat(R.color.status_timeout))
        } else if (signatureValid) {
            stateView.text = getString(R.string.ext_security_signature_valid)
            stateView.setTextColor(getColorCompat(R.color.status_success))
        } else {
            stateView.text = getString(R.string.ext_security_signature_invalid)
            stateView.setTextColor(getColorCompat(R.color.status_error))
        }

        val permContainer = view.findViewById<LinearLayout>(R.id.containerSecurityPermissions)
        val permInfos = ExtensionPermissions.describeAll(ext.permissions)
        if (permInfos.isEmpty()) {
            permContainer.addView(TextView(this).apply {
                text = getString(R.string.ext_security_no_permissions)
                setTextColor(getColorCompat(R.color.rin_editor_hint))
                textSize = 12.5f
            })
        } else {
            permInfos.forEach { info ->
                permContainer.addView(TextView(this).apply {
                    text = "• ${info.label}\n   ${info.description}"
                    setTextColor(getColorCompat(R.color.rin_editor_text))
                    textSize = 12.5f
                    setPadding(0, 0, 0, dpToPx(8))
                })
            }
        }

        view.findViewById<TextView>(R.id.txtSecurityFileAccess).text =
            ExtensionPermissions.accessScopeDescription(ext.extensionType)

        val actionLabel = if (isUpdate) getString(R.string.ext_action_update) else getString(R.string.action_install)
        AlertDialog.Builder(this)
            .setView(view)
            .setNegativeButton(R.string.ext_action_cancel, null)
            .setPositiveButton(getString(R.string.ext_security_proceed_format, actionLabel)) { _, _ ->
                if (!signatureValid) {
                    Toast.makeText(this, getString(R.string.ext_security_signature_invalid_toast), Toast.LENGTH_LONG).show()
                } else {
                    performInstall(isUpdate)
                }
            }
            .show()
    }

    private fun performInstall(isUpdate: Boolean) {
        layoutProgress.visibility = View.VISIBLE
        progressBar.progress = 0
        txtProgressLabel.text = getString(
            if (isUpdate) R.string.ext_install_progress_updating else R.string.ext_install_progress_installing,
            ext.name
        )
        setButtonsEnabled(false)

        val onProgress: (Int) -> Unit = { percent -> runOnUiThread { progressBar.progress = percent } }
        val onResult: (ExtensionInstallResult) -> Unit = { result ->
            runOnUiThread {
                layoutProgress.visibility = View.GONE
                setButtonsEnabled(true)
                when (result) {
                    is ExtensionInstallResult.Success -> {
                        val msg = if (isUpdate) R.string.ext_updated_toast else R.string.ext_installed_toast
                        Toast.makeText(this, getString(msg, ext.name), Toast.LENGTH_SHORT).show()
                        refreshMetaAndButtons()
                    }
                    is ExtensionInstallResult.Failure ->
                        Toast.makeText(this, getString(R.string.ext_install_failed_toast, result.message), Toast.LENGTH_LONG).show()
                }
            }
        }

        if (isUpdate) ExtensionManager.update(this, ext, onProgress, onResult)
        else ExtensionManager.install(this, ext, onProgress, onResult)
    }

    private fun setButtonsEnabled(enabled: Boolean) {
        btnInstall.isEnabled = enabled
        btnUpdate.isEnabled = enabled
        btnEnableDisable.isEnabled = enabled
        btnUninstall.isEnabled = enabled
    }

    private fun toggleEnabled() {
        val installed = ExtensionManager.installedRecord(this, ext.id) ?: return
        val newState = !installed.enabled
        ExtensionManager.setEnabled(this, ext.id, newState)
        Toast.makeText(
            this,
            getString(if (newState) R.string.ext_enabled_toast else R.string.ext_disabled_toast, ext.name),
            Toast.LENGTH_SHORT
        ).show()
        refreshMetaAndButtons()
    }

    private fun confirmUninstall() {
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.ext_uninstall_confirm_title))
            .setMessage(getString(R.string.ext_uninstall_confirm_message, ext.name))
            .setNegativeButton(R.string.ext_action_cancel, null)
            .setPositiveButton(R.string.ext_action_uninstall) { _, _ ->
                ExtensionManager.uninstall(this, ext.id)
                Toast.makeText(this, getString(R.string.ext_uninstalled_toast, ext.name), Toast.LENGTH_SHORT).show()
                refreshMetaAndButtons()
            }
            .show()
    }

    /**
     * يصدِّر الإضافة الحالية كملف مستقل بصيغة Rin Extensions الخاصة (.rinex) إلى مجلد Downloads
     * العام على الجهاز، عبر [RinexPackager]. الملف الناتج يحتوي محتوى الإضافة كاملاً + وصفها
     * (extension.rinext) في أرشيف واحد، ويمكن مشاركته أو استيراده لاحقاً بلا اتصال بالإنترنت.
     */
    private fun exportAsRinex() {
        btnExportRinex.isEnabled = false
        RinexPackager.exportToDownloads(this, ext) { uri, error ->
            btnExportRinex.isEnabled = true
            if (uri != null) {
                Toast.makeText(this, getString(R.string.ext_export_rinex_success, ext.name), Toast.LENGTH_LONG).show()
            } else {
                Toast.makeText(
                    this,
                    getString(R.string.ext_export_rinex_failed, error?.message ?: ""),
                    Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    /**
     * يعرض تأكيداً قبل حذف الإضافة نهائياً من "Rin Extensions Marketplace" (وليس فقط من هذا
     * الجهاز). متاح فقط لمطوّرها صاحب الإضافة — راجع [ExtensionRepository.deleteExtension].
     */
    private fun confirmDeleteExtension() {
        val uid = AuthRepository.currentUid()
        if (uid == null || uid != ext.developerUid) return

        AlertDialog.Builder(this)
            .setTitle(getString(R.string.ext_delete_confirm_title))
            .setMessage(getString(R.string.ext_delete_confirm_message, ext.name))
            .setNegativeButton(R.string.ext_action_cancel, null)
            .setPositiveButton(R.string.ext_action_delete) { _, _ ->
                if (!NetworkMonitor.isOnline(this)) {
                    Toast.makeText(this, R.string.ext_delete_requires_connection, Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                btnDeleteExtension.isEnabled = false
                ExtensionRepository.deleteExtension(ext.id, uid) { success, error ->
                    btnDeleteExtension.isEnabled = true
                    if (success) {
                        Toast.makeText(this, getString(R.string.ext_deleted_toast, ext.name), Toast.LENGTH_SHORT).show()
                        finish()
                    } else {
                        Toast.makeText(
                            this,
                            error ?: getString(R.string.ext_delete_failed),
                            Toast.LENGTH_LONG
                        ).show()
                    }
                }
            }
            .show()
    }
}
