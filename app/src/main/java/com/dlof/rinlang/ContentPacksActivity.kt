package com.dlof.rinlang

import android.graphics.Typeface
import android.os.Bundle
import android.text.format.Formatter
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import com.dlof.rinlang.packs.ContentPack
import com.dlof.rinlang.packs.InstalledPack
import com.dlof.rinlang.packs.LanguagePacks
import com.dlof.rinlang.packs.PackCatalog
import com.dlof.rinlang.packs.PackDownloadDialog
import com.dlof.rinlang.packs.PackDownloader
import com.dlof.rinlang.packs.PackException
import com.dlof.rinlang.packs.PackStore
import com.dlof.rinlang.packs.PackType

/**
 * شاشة «الحزم والتنزيلات»: تعرض حزم اللغات والأصول القابلة للتنزيل (من catalog.json) مع حالة كل حزمة
 * (غير منزّلة / مثبّتة / يتوفر تحديث) وأزرار التنزيل والحذف وتقدّم حقيقي، والمساحة التي تشغلها على الجهاز.
 * تعيد استخدام تخطيط activity_language.xml (الشريط العلوي + القائمة) دون XML جديد.
 */
class ContentPacksActivity : RinBaseActivity() {

    private lateinit var list: LinearLayout
    private lateinit var empty: TextView
    private var catalog: List<ContentPack> = emptyList()

    private class RowViews(val bar: RinProgressBar, val status: TextView)

    private val rows = HashMap<String, RowViews>()
    private val listeners = HashMap<String, PackDownloader.Listener>()
    private val density by lazy { resources.displayMetrics.density }

    private fun dp(v: Int) = (v * density).toInt()
    private fun size(bytes: Long) = Formatter.formatShortFileSize(this, bytes)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_language)
        findViewById<TextView>(R.id.txtLanguageTitle).text = getString(R.string.packs_title)
        findViewById<TextView>(R.id.txtLanguageSubtitle).text = getString(R.string.packs_subtitle)
        findViewById<View>(R.id.btnLanguageBack).setOnClickListener { finish() }
        // شريط البحث وسطر اللغات الثابت في activity_language غير لازمين هنا.
        (findViewById<EditText>(R.id.editLanguageSearch).parent as View).visibility = View.GONE
        list = findViewById(R.id.languageList)
        empty = findViewById(R.id.txtLanguageEmpty)
        empty.text = getString(R.string.packs_empty)

        catalog = PackCatalog.cached(this)
        render()
        refreshCatalog()
    }

    override fun onDestroy() {
        listeners.forEach { (id, l) -> PackDownloader.detach(id, l) }
        listeners.clear()
        super.onDestroy()
    }

    private fun refreshCatalog() {
        PackCatalog.refresh(this) { packs, error ->
            if (isFinishing || isDestroyed) return@refresh
            catalog = packs
            if (error != null) Toast.makeText(this, R.string.packs_catalog_failed, Toast.LENGTH_LONG).show()
            render()
        }
    }

    // ---- الرسم -------------------------------------------------------------------------------

    private fun render() {
        list.removeAllViews()
        rows.clear()
        val installed = PackStore.listInstalled(this).associateBy { it.id }

        addHeader(getString(R.string.packs_storage_used_format, size(installed.values.sumOf { it.bytesOnDisk })), accent = false)

        addHeader(getString(R.string.packs_section_languages))
        addBuiltinRow(getString(R.string.lang_name_ar))
        catalog.filter { it.type == PackType.LANGUAGE }.forEach { addPackRow(it, installed[it.id]) }

        val assets = catalog.filter { it.type == PackType.ASSET }
        if (assets.isNotEmpty()) {
            addHeader(getString(R.string.packs_section_assets))
            assets.forEach { addPackRow(it, installed[it.id]) }
        }

        // حزم مثبّتة لم تعد في الكتالوج (أو لم يُجلب بعد): تبقى قابلة للحذف على الأقل.
        val known = catalog.map { it.id }.toSet()
        installed.values.filter { it.id !in known }.forEach { addOrphanRow(it) }

        val hasAnything = catalog.isNotEmpty() || installed.isNotEmpty()
        empty.visibility = if (hasAnything) View.GONE else View.VISIBLE

        addActionRow(getString(R.string.packs_refresh)) { refreshCatalog() }
    }

    private fun addHeader(text: String, accent: Boolean = true) {
        list.addView(TextView(this).apply {
            this.text = text
            setTextColor(ContextCompat.getColor(context, if (accent) R.color.rin_accent else R.color.rin_editor_hint))
            textSize = if (accent) 13f else 12f
            if (accent) setTypeface(null, Typeface.BOLD)
            setPadding(dp(4), dp(if (accent) 18 else 12), dp(4), dp(8))
        })
    }

    private fun card(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = ContextCompat.getDrawable(context, R.drawable.bg_list_card)
        setPadding(dp(16), dp(12), dp(12), dp(12))
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { setMargins(0, dp(4), 0, dp(4)) }
    }

    private fun titleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 15f
        setTypeface(null, Typeface.BOLD)
        setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
    }

    private fun subtitleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 11.5f
        setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
        setPadding(0, dp(3), 0, 0)
    }

    private fun buttonView(text: String, color: Int, onClick: () -> Unit) = TextView(this).apply {
        this.text = text
        textSize = 13f
        setTypeface(null, Typeface.BOLD)
        setTextColor(ContextCompat.getColor(context, color))
        gravity = Gravity.CENTER
        setPadding(dp(14), dp(10), dp(14), dp(10))
        isClickable = true
        isFocusable = true
        setOnClickListener { onClick() }
    }

    private fun addBuiltinRow(name: String) {
        val c = card()
        c.addView(titleView(name))
        c.addView(subtitleView(getString(R.string.packs_builtin)))
        list.addView(c)
    }

    private fun addActionRow(label: String, onClick: () -> Unit) {
        val c = card()
        c.addView(buttonView(label, R.color.rin_accent, onClick))
        list.addView(c)
    }

    private fun addOrphanRow(info: InstalledPack) {
        val c = card()
        c.addView(titleView(info.id))
        c.addView(subtitleView(getString(R.string.packs_installed) + " • " + size(info.bytesOnDisk)))
        c.addView(buttonView(getString(R.string.packs_action_delete), R.color.log_kind_error) {
            confirmDelete(info.id, info.id, if (info.id.startsWith("lang-")) info.id.removePrefix("lang-") else null)
        })
        list.addView(c)
    }

    private fun addPackRow(pack: ContentPack, info: InstalledPack?) {
        val c = card()
        c.addView(titleView(pack.title))
        val hasUpdate = info != null && info.sha256 != pack.sha256
        val stateText = when {
            info == null -> size(pack.sizeBytes)
            hasUpdate -> getString(R.string.packs_update_available) + " • " + size(info.bytesOnDisk)
            else -> getString(R.string.packs_installed) + " • " + size(info.bytesOnDisk)
        }
        val desc = if (pack.description.isNotBlank()) pack.description + "\n" + stateText else stateText
        c.addView(subtitleView(desc))

        if (PackDownloader.isActive(pack.id)) {
            val bar = RinProgressBar(this).apply { setProgress(0, animate = false) }
            c.addView(bar, LinearLayout.LayoutParams(-1, dp(8)).apply { topMargin = dp(10) })
            val status = subtitleView("")
            c.addView(status)
            rows[pack.id] = RowViews(bar, status)
            c.addView(buttonView(getString(R.string.packs_action_cancel), R.color.log_kind_error) { cancel(pack.id) })
            attach(pack)
        } else {
            val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.END }
            if (info == null) {
                actions.addView(buttonView(getString(R.string.packs_action_download, size(pack.sizeBytes)), R.color.rin_accent) { startDownload(pack) })
            } else {
                if (hasUpdate) {
                    actions.addView(buttonView(getString(R.string.packs_action_update, size(pack.sizeBytes)), R.color.rin_accent) { startDownload(pack) })
                }
                actions.addView(buttonView(getString(R.string.packs_action_delete), R.color.log_kind_error) { confirmDelete(pack.id, pack.title, pack.language) })
            }
            c.addView(actions)
        }
        list.addView(c)
    }

    // ---- الإجراءات ---------------------------------------------------------------------------

    private fun startDownload(pack: ContentPack) {
        attach(pack)
        render()
    }

    private fun attach(pack: ContentPack) {
        if (listeners.containsKey(pack.id)) return
        val l = object : PackDownloader.Listener {
            override fun onProgress(stage: PackDownloader.Stage, doneBytes: Long, totalBytes: Long) {
                val r = rows[pack.id] ?: return
                r.status.text = PackDownloadDialog.stageText(this@ContentPacksActivity, stage, doneBytes, totalBytes)
                if (stage == PackDownloader.Stage.DOWNLOADING) {
                    if (totalBytes > 0) r.bar.setProgress(((doneBytes * 100) / totalBytes).toInt().coerceIn(0, 100))
                } else {
                    r.bar.setProgress(100)
                }
            }

            override fun onSuccess(pack: ContentPack) {
                listeners.remove(pack.id)
                if (isFinishing || isDestroyed) return
                Toast.makeText(this@ContentPacksActivity, getString(R.string.packs_done_toast, pack.title), Toast.LENGTH_SHORT).show()
                render()
            }

            override fun onError(pack: ContentPack, error: PackException) {
                listeners.remove(pack.id)
                if (isFinishing || isDestroyed) return
                Toast.makeText(this@ContentPacksActivity, error.userMessage(this@ContentPacksActivity), Toast.LENGTH_LONG).show()
                render()
            }
        }
        listeners[pack.id] = l
        PackDownloader.start(this, pack, l)
    }

    private fun cancel(id: String) = PackDownloader.cancel(id)

    private fun confirmDelete(id: String, title: String, language: String?) {
        AlertDialog.Builder(this)
            .setTitle(R.string.packs_delete_title)
            .setMessage(getString(R.string.packs_delete_message_format, title))
            .setPositiveButton(R.string.packs_action_delete) { _, _ ->
                val wasActiveLanguage = language != null && LocaleHelper.getCurrentAppLocaleTag() == language
                PackStore.delete(this, id)
                Toast.makeText(this, getString(R.string.packs_deleted_toast, title), Toast.LENGTH_SHORT).show()
                // لغة الواجهة الحالية فقدت حزمتها: نعود للعربية المدمجة بدل واجهة بلغة مختلطة.
                if (wasActiveLanguage) LocaleHelper.setAppLocale(LanguagePacks.BUILTIN.first())
                else render()
            }
            .setNegativeButton(R.string.packs_action_cancel, null)
            .show()
    }
}
