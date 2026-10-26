package com.dlof.rinlang

import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.packs.LanguagePacks
import com.dlof.rinlang.packs.PackDownloadDialog

/**
 * Language picker: gradient hero with the current language, search, family filter chips,
 * country cards with flags and a pack status pill (built-in / installed / download).
 */
class LanguageActivity : RinBaseActivity() {
    data class LanguageCountry(val flag:String, val name:String, val nativeName:String, val tag:String, val group:String)

    private lateinit var list: LinearLayout
    private lateinit var search: EditText
    private lateinit var empty: View
    private lateinit var chips: LinearLayout
    private lateinit var clear: View
    private var groupFilter: String? = null
    private val all = listOf(
        // Arabic — all 22 Arab League countries; Syria intentionally first.
        LanguageCountry("🇸🇾","Syria","سورية","ar-SY","Arabic"), LanguageCountry("🇩🇿","Algeria","الجزائر","ar-DZ","Arabic"),
        LanguageCountry("🇧🇭","Bahrain","البحرين","ar-BH","Arabic"), LanguageCountry("🇰🇲","Comoros","جزر القمر","ar-KM","Arabic"),
        LanguageCountry("🇩🇯","Djibouti","جيبوتي","ar-DJ","Arabic"), LanguageCountry("🇪🇬","Egypt","مصر","ar-EG","Arabic"),
        LanguageCountry("🇮🇶","Iraq","العراق","ar-IQ","Arabic"), LanguageCountry("🇯🇴","Jordan","الأردن","ar-JO","Arabic"),
        LanguageCountry("🇰🇼","Kuwait","الكويت","ar-KW" ,"Arabic"), LanguageCountry("🇱🇧","Lebanon","لبنان","ar-LB","Arabic"),
        LanguageCountry("🇱🇾","Libya","ليبيا","ar-LY","Arabic"), LanguageCountry("🇲🇷","Mauritania","موريتانيا","ar-MR","Arabic"),
        LanguageCountry("🇲🇦","Morocco","المغرب","ar-MA","Arabic"), LanguageCountry("🇴🇲","Oman","عُمان","ar-OM","Arabic"),
        LanguageCountry("🇵🇸","Palestine","فلسطين","ar-PS","Arabic"), LanguageCountry("🇶🇦","Qatar","قطر","ar-QA","Arabic"),
        LanguageCountry("🇸🇦","Saudi Arabia","السعودية","ar-SA","Arabic"), LanguageCountry("🇸🇴","Somalia","الصومال","ar-SO","Arabic"),
        LanguageCountry("🇸🇩","Sudan","السودان","ar-SD","Arabic"), LanguageCountry("🇹🇳","Tunisia","تونس","ar-TN","Arabic"),
        LanguageCountry("🇦🇪","United Arab Emirates","الإمارات العربية المتحدة","ar-AE","Arabic"), LanguageCountry("🇾🇪","Yemen","اليمن","ar-YE","Arabic"),
        // English — major countries where English is official or a principal national language.
        LanguageCountry("🇺🇸","United States","English","en-US","English"), LanguageCountry("🇬🇧","United Kingdom","English","en-GB","English"),
        LanguageCountry("🇨🇦","Canada","English","en-CA","English"), LanguageCountry("🇦🇺","Australia","English","en-AU","English"),
        LanguageCountry("🇳🇿","New Zealand","English","en-NZ","English"), LanguageCountry("🇮🇪","Ireland","English","en-IE","English"),
        LanguageCountry("🇿🇦","South Africa","English","en-ZA","English"), LanguageCountry("🇮🇳","India","English","en-IN","English"),
        LanguageCountry("🇵🇰","Pakistan","English","en-PK","English"), LanguageCountry("🇵🇭","Philippines","English","en-PH","English"),
        LanguageCountry("🇸🇬","Singapore","English","en-SG","English"), LanguageCountry("🇲🇹","Malta","English","en-MT","English"),
        LanguageCountry("🇳🇬","Nigeria","English","en-NG","English"), LanguageCountry("🇬🇭","Ghana","English","en-GH","English"),
        LanguageCountry("🇰🇪","Kenya","English","en-KE","English"), LanguageCountry("🇺🇬","Uganda","English","en-UG","English"),
        LanguageCountry("🇹🇿","Tanzania","English","en-TZ","English"), LanguageCountry("🇿🇲","Zambia","English","en-ZM","English"),
        LanguageCountry("🇿🇼","Zimbabwe","English","en-ZW","English"), LanguageCountry("🇧🇼","Botswana","English","en-BW","English"),
        LanguageCountry("🇳🇦","Namibia","English","en-NA","English"), LanguageCountry("🇲🇼","Malawi","English","en-MW","English"),
        LanguageCountry("🇸🇱","Sierra Leone","English","en-SL","English"), LanguageCountry("🇱🇷","Liberia","English","en-LR","English"),
        LanguageCountry("🇬🇲","The Gambia","English","en-GM","English"), LanguageCountry("🇧🇿","Belize","English","en-BZ","English"),
        LanguageCountry("🇧🇸","Bahamas","English","en-BS","English"), LanguageCountry("🇧🇧","Barbados","English","en-BB","English"),
        LanguageCountry("🇯🇲","Jamaica","English","en-JM","English"), LanguageCountry("🇹🇹","Trinidad and Tobago","English","en-TT","English"),
        LanguageCountry("🇬🇾","Guyana","English","en-GY","English"), LanguageCountry("🇫🇯","Fiji","English","en-FJ","English"),
        // Spanish — Spain + Spanish-speaking Latin America.
        LanguageCountry("🇪🇸","Spain","España","es-ES","Spanish"), LanguageCountry("🇲🇽","Mexico","México","es-MX","Spanish"),
        LanguageCountry("🇬🇹","Guatemala","Guatemala","es-GT","Spanish"), LanguageCountry("🇧🇿","Belize","Belice","es-BZ","Spanish"),
        LanguageCountry("🇭🇳","Honduras","Honduras","es-HN","Spanish"), LanguageCountry("🇸🇻","El Salvador","El Salvador","es-SV","Spanish"),
        LanguageCountry("🇳🇮","Nicaragua","Nicaragua","es-NI","Spanish"), LanguageCountry("🇨🇷","Costa Rica","Costa Rica","es-CR","Spanish"),
        LanguageCountry("🇵🇦","Panama","Panamá","es-PA","Spanish"), LanguageCountry("🇨🇺","Cuba","Cuba","es-CU","Spanish"),
        LanguageCountry("🇩🇴","Dominican Republic","República Dominicana","es-DO","Spanish"), LanguageCountry("🇵🇷","Puerto Rico","Puerto Rico","es-PR","Spanish"),
        LanguageCountry("🇨🇴","Colombia","Colombia","es-CO","Spanish"), LanguageCountry("🇻🇪","Venezuela","Venezuela","es-VE","Spanish"),
        LanguageCountry("🇪🇨","Ecuador","Ecuador","es-EC","Spanish"), LanguageCountry("🇵🇪","Peru","Perú","es-PE","Spanish"),
        LanguageCountry("🇧🇴","Bolivia","Bolivia","es-BO","Spanish"), LanguageCountry("🇨🇱","Chile","Chile","es-CL","Spanish"),
        LanguageCountry("🇦🇷","Argentina","Argentina","es-AR","Spanish"), LanguageCountry("🇵🇾","Paraguay","Paraguay","es-PY","Spanish"),
        LanguageCountry("🇺🇾","Uruguay","Uruguay","es-UY","Spanish")
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_language)
        findViewById<TextView>(R.id.txtLanguageTitle).text = getString(R.string.language_picker_title)
        findViewById<TextView>(R.id.txtLanguageSubtitle).text = getString(R.string.language_picker_subtitle)
        findViewById<View>(R.id.btnLanguageBack).setOnClickListener { finish() }
        search = findViewById(R.id.editLanguageSearch)
        list = findViewById(R.id.languageList)
        empty = findViewById(R.id.languageEmptyState)
        chips = findViewById(R.id.languageChips)
        clear = findViewById(R.id.btnLanguageClear)
        search.hint = getString(R.string.language_picker_search)
        clear.contentDescription = getString(R.string.language_search_clear)
        clear.setOnClickListener { search.setText("") }
        search.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, a: Int) {}
            override fun onTextChanged(s: CharSequence?, st: Int, b: Int, c: Int) {
                clear.visibility = if (s.isNullOrEmpty()) View.GONE else View.VISIBLE
                render()
            }
            override fun afterTextChanged(e: Editable?) {}
        })
        bindCurrentLanguage()
        buildChips()
        render()
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density + 0.5f).toInt()

    /** Language code currently applied; Arabic is the built-in default. */
    private fun currentLang(): String = LocaleHelper.getCurrentAppLocaleTag()?.substringBefore('-') ?: LocaleHelper.LANG_ARABIC

    private fun languageDisplayName(lang: String): String = when (lang) {
        LocaleHelper.LANG_ARABIC -> getString(R.string.lang_name_ar)
        LocaleHelper.LANG_ENGLISH -> getString(R.string.lang_name_en)
        LocaleHelper.LANG_SPANISH -> getString(R.string.lang_name_es)
        else -> lang
    }

    private fun bindCurrentLanguage() {
        val lang = currentLang()
        val rep = all.firstOrNull { it.tag.substringBefore('-') == lang } ?: all.first()
        findViewById<TextView>(R.id.txtCurrentFlag).text = rep.flag
        findViewById<TextView>(R.id.txtCurrentLabel).text = getString(R.string.language_picker_current)
        findViewById<TextView>(R.id.txtCurrentName).text = languageDisplayName(lang)
    }

    private fun buildChips() {
        chips.removeAllViews()
        val options = listOf<Pair<String?, String>>(
            null to getString(R.string.language_filter_all),
            "Arabic" to getString(R.string.lang_name_ar),
            "English" to getString(R.string.lang_name_en),
            "Spanish" to getString(R.string.lang_name_es)
        )
        options.forEach { (group, label) ->
            val selected = group == groupFilter
            val chip = TextView(this)
            chip.text = label
            chip.textSize = 13f
            chip.setTypeface(null, Typeface.BOLD)
            chip.gravity = Gravity.CENTER
            chip.setPadding(dp(16), 0, dp(16), 0)
            chip.setBackgroundResource(if (selected) R.drawable.bg_lang_chip_selected else R.drawable.bg_lang_chip)
            chip.setTextColor(if (selected) Color.WHITE else getColor(R.color.rin_on_toolbar))
            chip.isClickable = true
            chip.isFocusable = true
            chip.setOnClickListener { groupFilter = group; buildChips(); render() }
            val lp = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, dp(38))
            lp.marginEnd = dp(8)
            chips.addView(chip, lp)
        }
    }

    private fun render() {
        list.removeAllViews()
        val q = search.text.toString().trim().lowercase()
        val current = currentLang()
        val filtered = all.filter { item ->
            (groupFilter == null || item.group == groupFilter) &&
                (q.isEmpty() || item.name.lowercase().contains(q) || item.nativeName.lowercase().contains(q) || item.tag.lowercase().contains(q))
        }
        empty.visibility = if (filtered.isEmpty()) View.VISIBLE else View.GONE
        var currentGroup = ""
        filtered.forEach { item ->
            if (item.group != currentGroup) {
                currentGroup = item.group
                val groupName = currentGroup
                addHeader(groupName, filtered.count { it.group == groupName })
            }
            addCountry(item, current)
        }
    }

    private fun addHeader(group: String, count: Int) {
        val header = layoutInflater.inflate(R.layout.item_language_header, list, false) as TextView
        val title = when (group) {
            "Arabic" -> getString(R.string.language_group_arabic)
            "English" -> getString(R.string.language_group_english)
            else -> getString(R.string.language_group_spanish)
        }
        header.text = title + "  ·  " + getString(R.string.language_picker_count_format, count)
        list.addView(header)
    }

    private fun addCountry(item: LanguageCountry, current: String) {
        val lang = item.tag.substringBefore('-')
        val selected = lang == current
        val row = layoutInflater.inflate(R.layout.item_language_country, list, false)
        row.setBackgroundResource(if (selected) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
        row.findViewById<TextView>(R.id.txtLanguageFlag).text = item.flag
        row.findViewById<TextView>(R.id.txtLanguageName).text = item.name
        row.findViewById<TextView>(R.id.txtLanguageNative).text = item.nativeName + "  •  " + item.tag
        row.findViewById<TextView>(R.id.txtLanguageCheck).visibility = if (selected) View.VISIBLE else View.INVISIBLE

        val status = row.findViewById<TextView>(R.id.txtLanguageStatus)
        when {
            LanguagePacks.isBuiltin(lang) -> {
                status.setText(R.string.language_status_builtin)
                status.setBackgroundResource(R.drawable.bg_lang_status_builtin)
                status.setTextColor(getColor(R.color.rin_accent_green))
            }
            LanguagePacks.isAvailable(this, lang) -> {
                status.setText(R.string.language_status_installed)
                status.setBackgroundResource(R.drawable.bg_lang_status_installed)
                status.setTextColor(getColor(R.color.rin_accent))
            }
            else -> {
                status.setText(R.string.language_status_download)
                status.setBackgroundResource(R.drawable.bg_lang_status_download)
                status.setTextColor(getColor(R.color.rin_on_toolbar_dim))
            }
        }
        row.contentDescription = item.name + ", " + item.nativeName
        row.setOnClickListener { selectLanguage(item) }
        list.addView(row)
    }

    /**
     * Arabic is built into the app and applies instantly; any other language (English/Spanish)
     * is downloaded as a language pack first (to keep the app small) and applied once installed.
     */
    private fun selectLanguage(item: LanguageCountry) {
        val lang = item.tag.substringBefore('-')
        val applyNow = {
            LocaleHelper.setAppLocale(lang)
            Toast.makeText(this, getString(R.string.language_picker_applied, item.nativeName), Toast.LENGTH_SHORT).show()
            finish()
        }
        if (LanguagePacks.isAvailable(this, lang)) {
            applyNow()
        } else {
            val name = when (lang) {
                LocaleHelper.LANG_ENGLISH -> getString(R.string.lang_name_en)
                LocaleHelper.LANG_SPANISH -> getString(R.string.lang_name_es)
                else -> item.nativeName
            }
            PackDownloadDialog.ensure(this, LanguagePacks.packId(lang), name) { applyNow() }
        }
    }
}
