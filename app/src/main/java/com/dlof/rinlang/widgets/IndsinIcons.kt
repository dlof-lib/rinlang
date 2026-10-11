package com.dlof.rinlang.widgets

import android.content.Context
import org.json.JSONObject

/**
 * مكتبة أيقونات indsin المتجهية داخل RinStudio: تقرأ `assets/indsin-icons/icons.json`
 * (يُولَّد من tools/icons/gen_indsin_icons.py — لا يُعدَّل يدوياً) مرة واحدة وتخزّنها.
 * كل أيقونة مسار SVG على شبكة 24×24 يرسمه [IndsinIconView].
 */
object IndsinIcons {

    data class Icon(
        val name: String,
        val category: String,
        val aliases: List<String>,
        val path: String,
        val filled: Boolean
    )

    data class Category(val id: String, val ar: String, val en: String)

    class Data(val icons: List<Icon>, val categories: List<Category>) {
        private val byName: Map<String, Icon> = icons.associateBy { it.name }
        private val byAlias: Map<String, String> =
            icons.flatMap { ic -> ic.aliases.map { it to ic.name } }.toMap()

        /** يقبل الاسم الأساسي أو البديل (بلا حساسية لحالة الأحرف)؛ null إن لم يوجد. */
        fun find(name: String): Icon? {
            val n = name.trim().lowercase()
            return byName[n] ?: byAlias[n]?.let { byName[it] }
        }

        /** بحث بالاسم أو الأسماء البديلة، مع فلترة اختيارية على فئة. */
        fun search(query: String, category: String? = null): List<Icon> {
            val q = query.trim().lowercase()
            return icons.filter { ic ->
                (category == null || ic.category == category) &&
                    (q.isEmpty() || ic.name.contains(q) || ic.aliases.any { it.contains(q) })
            }
        }
    }

    @Volatile
    private var cache: Data? = null

    fun load(context: Context): Data {
        cache?.let { return it }
        synchronized(this) {
            cache?.let { return it }
            val text = context.applicationContext.assets.open("indsin-icons/icons.json")
                .bufferedReader(Charsets.UTF_8).use { it.readText() }
            val root = JSONObject(text)
            val cats = root.getJSONArray("categories")
            val categories = (0 until cats.length()).map {
                val c = cats.getJSONObject(it)
                Category(c.getString("id"), c.getString("ar"), c.getString("en"))
            }
            val arr = root.getJSONArray("icons")
            val rank = categories.withIndex().associate { it.value.id to it.index }
            val icons = (0 until arr.length()).map {
                val o = arr.getJSONObject(it)
                val al = o.getJSONArray("aliases")
                Icon(
                    name = o.getString("name"),
                    category = o.getString("category"),
                    aliases = (0 until al.length()).map { i -> al.getString(i) },
                    path = o.getString("path"),
                    filled = o.optBoolean("filled", false)
                )
            }.sortedWith(compareBy({ rank[it.category] ?: 99 }, { it.name }))
            return Data(icons, categories).also { cache = it }
        }
    }

    /** سطر indsin جاهز للإدراج في المحرر لأيقونة معيّنة. */
    fun snippet(name: String): String =
        "@view.Icon=icon_${name.replace('-', '_')}\n    icon=\"$name\";\n    size=\"lg\";\n.end/view"
}
