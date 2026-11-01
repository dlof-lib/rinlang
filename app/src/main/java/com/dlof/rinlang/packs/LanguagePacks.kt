package com.dlof.rinlang.packs

import android.content.Context
import android.content.res.Resources
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import org.json.JSONObject
import java.io.File
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap

/**
 * نصوص لغة واحدة محمّلة من حزمة لغة منزَّلة (strings.json). للقيم المفقودة يُستخدم نص الـ APK الافتراضي (العربية).
 */
class LanguageStrings(
    val strings: Map<String, String>,
    val plurals: Map<String, Map<String, String>>,
    val arrays: Map<String, List<String>>
)

/**
 * طبقة اللغة وقت التشغيل: العربية (الافتراضية) مدمجة في الـ APK، وباقي اللغات تُنزَّل كحزم
 * (`lang-<code>`) ثم تُطبَّق فوق الموارد بدل أن تُشحن داخل الـ APK:
 *
 * 1. [OverlayResources] يعترض `getText/getString/getQuantityString/getStringArray` بالاسم، فيغطي كل
 *    استدعاءات `getString(R.string.x)` وحوارات AlertDialog في الكود.
 * 2. [PackLayoutInflater] يعيد ترجمة النصوص المأخوذة من ملفات XML (`android:text="@string/..."`)،
 *    لأن inflate يقرأها مباشرة من AssetManager متجاوزاً Resources. تُطابَق عبر نص الـ APK الأصلي.
 *
 * يُفعَّل كلاهما فقط من [com.dlof.rinlang.RinBaseActivity] وفقط عندما تكون لغة الواجهة الحالية حزمة منزَّلة.
 */
object LanguagePacks {

    /** اللغات المدمجة في الـ APK (values/ + values-ar/). أي لغة أخرى تحتاج حزمة. */
    val BUILTIN = setOf("ar")

    fun packId(language: String) = "lang-$language"

    fun isBuiltin(language: String) = language in BUILTIN

    fun isAvailable(context: Context, language: String): Boolean =
        isBuiltin(language) || PackStore.isInstalled(context, packId(language))

    private val cache = ConcurrentHashMap<String, LanguageStrings>()
    private val missing = ConcurrentHashMap.newKeySet<String>()

    /** يفرغ الذاكرة المؤقتة بعد تثبيت/حذف/تحديث أي حزمة. */
    fun invalidateAll() {
        cache.clear()
        missing.clear()
    }

    /** يحمّل نصوص [language] من حزمتها المنزّلة، أو null إن لم تُنزَّل/كانت تالفة. */
    fun load(context: Context, language: String): LanguageStrings? {
        if (isBuiltin(language) || !ContentPack.isValidId(packId(language))) return null
        cache[language]?.let { return it }
        if (language in missing) return null
        val loaded = try {
            val f = File(PackStore.dirOf(context, packId(language)), "strings.json")
            if (!PackStore.isInstalled(context, packId(language)) || !f.isFile) null
            else parse(f.readText(Charsets.UTF_8))
        } catch (e: Exception) {
            null
        }
        if (loaded == null) missing.add(language) else cache[language] = loaded
        return loaded
    }

    private fun parse(text: String): LanguageStrings {
        val root = JSONObject(text)
        val strings = HashMap<String, String>()
        root.optJSONObject("strings")?.let { o -> o.keys().forEach { strings[it] = o.getString(it) } }
        val plurals = HashMap<String, Map<String, String>>()
        root.optJSONObject("plurals")?.let { o ->
            o.keys().forEach { name ->
                val q = o.getJSONObject(name)
                plurals[name] = q.keys().asSequence().associateWith { q.getString(it) }
            }
        }
        val arrays = HashMap<String, List<String>>()
        root.optJSONObject("arrays")?.let { o ->
            o.keys().forEach { name ->
                val a = o.getJSONArray(name)
                arrays[name] = (0 until a.length()).map { a.getString(it) }
            }
        }
        return LanguageStrings(strings, plurals, arrays)
    }

    // ---- تغليف الموارد ----------------------------------------------------------------------

    private class OverlayHolder(val base: Resources, val overlay: OverlayResources, val language: String)

    /**
     * يعيد Resources تُطبّق حزمة اللغة المنزَّلة على [base]، أو [base] نفسها إن كانت اللغة الحالية مدمجة
     * أو غير منزَّلة. يُستدعى من getResources() في كل مرة، لذا النتيجة تُخزَّن لكل Activity في [holder].
     */
    internal fun wrap(context: Context, base: Resources, holder: Array<Any?>): Resources {
        val language = base.configuration.locales.let { if (it.isEmpty) "" else it[0].language }
        val pack = if (language.isEmpty()) null else load(context, language)
        if (pack == null) {
            holder[0] = null
            return base
        }
        val h = holder[0] as? OverlayHolder
        if (h != null && h.base === base && h.language == language &&
            h.overlay.configuration.diff(base.configuration) == 0 && h.overlay.pack === pack
        ) {
            return h.overlay
        }
        val locale: Locale = base.configuration.locales[0]
        val overlay = OverlayResources(base, pack, locale, context.packageName)
        holder[0] = OverlayHolder(base, overlay, language)
        return overlay
    }

    /** ينشئ معيد ترجمة للـ XML، أو null إن لم تكن هناك حزمة لغة فعّالة على [resources]. */
    internal fun retranslatorFor(resources: Resources): ((View) -> Unit)? {
        val overlay = resources as? OverlayResources ?: return null
        val map = overlay.reverseMap
        if (map.isEmpty()) return null
        return { root -> retranslate(root, map) }
    }

    private fun retranslate(view: View, map: Map<String, String>) {
        if (view is TextView) {
            if (view !is EditText) {
                val t = view.text
                if (!t.isNullOrEmpty()) map[t.toString()]?.let { view.text = it }
            }
            val hint = view.hint
            if (!hint.isNullOrEmpty()) map[hint.toString()]?.let { view.hint = it }
        }
        val cd = view.contentDescription
        if (!cd.isNullOrEmpty()) map[cd.toString()]?.let { view.contentDescription = it }
        if (view is ViewGroup) {
            for (i in 0 until view.childCount) retranslate(view.getChildAt(i), map)
        }
    }
}

/**
 * Resources تُرجع نص الحزمة بالاسم عندما يكون موجوداً فيها، وإلا نص الـ APK. تتشارك AssetManager مع الأصل،
 * فكل الموارد الأخرى (ألوان، أبعاد، drawables، themes) تعمل كما هي.
 */
@Suppress("DEPRECATION")
internal class OverlayResources(
    private val base: Resources,
    val pack: LanguageStrings,
    private val locale: Locale,
    private val packageName: String
) : Resources(base.assets, base.displayMetrics, base.configuration) {

    private val names = ConcurrentHashMap<Int, String>() // "" = ليس نصاً قابلاً للاستبدال

    private fun nameOf(id: Int, type: String): String {
        val key = names.getOrPut(id) {
            try {
                if (base.getResourceTypeName(id) == type) base.getResourceEntryName(id) else ""
            } catch (e: Resources.NotFoundException) {
                ""
            }
        }
        return key
    }

    // نص الـ APK الأصلي (العربية) -> الترجمة، لإعادة ترجمة ما يملؤه inflate مباشرة من XML.
    // النصوص المتكررة بترجمات مختلفة تُستبعد حتى لا تُترجم بشكل خاطئ.
    val reverseMap: Map<String, String> by lazy {
        val map = HashMap<String, String>()
        val ambiguous = HashSet<String>()
        for ((name, translated) in pack.strings) {
            val id = base.getIdentifier(name, "string", packageName)
            if (id == 0) continue
            val source = try { base.getText(id).toString() } catch (e: Resources.NotFoundException) { continue }
            if (source.isBlank() || source.contains('%') || source == translated) continue
            val prev = map[source]
            if (prev != null && prev != translated) ambiguous.add(source) else map[source] = translated
        }
        ambiguous.forEach { map.remove(it) }
        map
    }

    override fun getText(id: Int): CharSequence {
        val key = nameOf(id, "string")
        if (key.isNotEmpty()) pack.strings[key]?.let { return it }
        return super.getText(id)
    }

    override fun getText(id: Int, def: CharSequence?): CharSequence? {
        val key = nameOf(id, "string")
        if (key.isNotEmpty()) pack.strings[key]?.let { return it }
        return super.getText(id, def)
    }

    override fun getTextArray(id: Int): Array<CharSequence> {
        val key = nameOf(id, "array")
        if (key.isNotEmpty()) pack.arrays[key]?.let { return it.toTypedArray<CharSequence>() }
        return super.getTextArray(id)
    }

    override fun getStringArray(id: Int): Array<String> {
        val key = nameOf(id, "array")
        if (key.isNotEmpty()) pack.arrays[key]?.let { return it.toTypedArray() }
        return super.getStringArray(id)
    }

    override fun getQuantityText(id: Int, quantity: Int): CharSequence {
        val key = nameOf(id, "plurals")
        if (key.isNotEmpty()) {
            val forms = pack.plurals[key]
            if (forms != null) {
                val keyword = try {
                    android.icu.text.PluralRules.forLocale(locale).select(quantity.toDouble())
                } catch (e: Throwable) {
                    "other"
                }
                (forms[keyword] ?: forms["other"])?.let { return it }
            }
        }
        return super.getQuantityText(id, quantity)
    }
}

/** LayoutInflater يعيد ترجمة كل View شجرة تُنفَخ من XML (انظر [LanguagePacks]). */
internal class PackLayoutInflater(
    original: android.view.LayoutInflater,
    newContext: Context,
    private val retranslate: (View) -> Unit
) : android.view.LayoutInflater(original, newContext) {

    override fun cloneInContext(newContext: Context): android.view.LayoutInflater =
        PackLayoutInflater(this, newContext, retranslate)

    override fun inflate(parser: org.xmlpull.v1.XmlPullParser, root: ViewGroup?, attachToRoot: Boolean): View {
        val result = super.inflate(parser, root, attachToRoot)
        retranslate(result)
        return result
    }
}
