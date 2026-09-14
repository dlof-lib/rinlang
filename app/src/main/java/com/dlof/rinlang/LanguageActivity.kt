package com.dlof.rinlang

import android.graphics.Color
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity

/** Professional language picker with language families, country flags, search and instant locale switching. */
class LanguageActivity : AppCompatActivity() {
    data class LanguageCountry(val flag:String, val name:String, val nativeName:String, val tag:String, val group:String)

    private lateinit var list: LinearLayout
    private lateinit var search: EditText
    private lateinit var empty: TextView
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
        search = findViewById(R.id.editLanguageSearch); list = findViewById(R.id.languageList); empty = findViewById(R.id.txtLanguageEmpty)
        search.hint = getString(R.string.language_picker_search)
        search.addTextChangedListener(object: TextWatcher { override fun beforeTextChanged(s:CharSequence?,st:Int,c:Int,a:Int){}; override fun onTextChanged(s:CharSequence?,st:Int,b:Int,c:Int){ render(s?.toString().orEmpty()) }; override fun afterTextChanged(e:Editable?){}})
        render("")
    }

    private fun render(query:String) {
        list.removeAllViews(); val q=query.trim().lowercase()
        var currentGroup=""
        val filtered=all.filter { q.isEmpty() || it.name.lowercase().contains(q) || it.nativeName.lowercase().contains(q) || it.tag.lowercase().contains(q) }
        empty.visibility=if(filtered.isEmpty()) View.VISIBLE else View.GONE
        filtered.forEach { item ->
            if(item.group != currentGroup){ currentGroup=item.group; addHeader(item.group) }
            addCountry(item)
        }
    }
    private fun addHeader(group:String){ val t=TextView(this); t.text=when(group){"Arabic"->getString(R.string.language_group_arabic);"English"->getString(R.string.language_group_english);else->getString(R.string.language_group_spanish)}; t.setTextColor(getColor(R.color.rin_accent)); t.textSize=13f; t.setTypeface(null,1); t.setPadding(6,20,6,8); list.addView(t) }
    private fun addCountry(item:LanguageCountry){
        val card=LinearLayout(this); card.orientation=LinearLayout.HORIZONTAL; card.gravity=Gravity.CENTER_VERTICAL; card.setPadding(16,10,12,10); card.background=getDrawable(if(LocaleHelper.getCurrentAppLocaleTag()==item.tag.substringBefore('-')) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card); card.isClickable=true; card.isFocusable=true
        val flag=TextView(this); flag.text=item.flag; flag.textSize=27f; flag.gravity=Gravity.CENTER; card.addView(flag,LinearLayout.LayoutParams(52,52))
        val col=LinearLayout(this); col.orientation=LinearLayout.VERTICAL; col.layoutParams=LinearLayout.LayoutParams(0,-2,1f); val name=TextView(this); name.text=item.name; name.textSize=15f; name.setTextColor(getColor(R.color.rin_on_toolbar)); name.setTypeface(null,1); val native=TextView(this); native.text=item.nativeName+"  •  "+item.tag; native.textSize=11f; native.setTextColor(getColor(R.color.rin_editor_hint)); native.setPadding(0,3,0,0); col.addView(name); col.addView(native); card.addView(col)
        val check=TextView(this); check.text=if(LocaleHelper.getCurrentAppLocaleTag()==item.tag.substringBefore('-')) "✓" else ""; check.textSize=20f; check.setTextColor(getColor(R.color.rin_accent)); check.gravity=Gravity.CENTER; card.addView(check,LinearLayout.LayoutParams(40,52))
        val lp=LinearLayout.LayoutParams(-1,68); lp.setMargins(0,4,0,4); list.addView(card,lp)
        card.setOnClickListener { LocaleHelper.setAppLocale(item.tag.substringBefore('-')); Toast.makeText(this,getString(R.string.language_picker_applied,item.nativeName),Toast.LENGTH_SHORT).show(); finish() }
    }
}
