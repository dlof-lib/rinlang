package com.dlof.rinlang

import android.graphics.drawable.GradientDrawable
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.core.graphics.ColorUtils
import androidx.recyclerview.widget.RecyclerView

/**
 * محوّل قائمة حوار "بنية الملف" ([RinContainerTags.OutlineEntry]) إلى صفوف مُنسَّقة بصرياً بدل
 * سطور نصية مسطّحة (AlertDialog.setItems() السابقة): شارة دائرية بلون ورمز مميّز حسب نوع الوسم
 * (حاوية/عرض/ثيم/عنصر/لوب/كائن/قسم)، خطوط إرشاد شجرية حقيقية بعمق التعشيش الفعلي
 * ([OutlineTreeGuideView]) بدل المسافات البادئة النصية، ورقم السطر داخل حبّة منفصلة.
 */
class OutlineDialogAdapter(
    private val entries: List<RinContainerTags.OutlineEntry>,
    private val lineLabel: (Int) -> String,
    private val onEntryClick: (RinContainerTags.OutlineEntry) -> Unit
) : RecyclerView.Adapter<OutlineDialogAdapter.ViewHolder>() {

    /** تصنيف نوع الوسم من نص العنصر، لتحديد رمز ولون شارته — رموز محايدة عن اللغة (بلا حروف
     * عربية/لاتينية) لأن التطبيق مُعرَّب لأكثر من لغة واجهة (ar/en/es). */
    private enum class Kind(val glyph: String, val colorRes: Int) {
        CONTAINER("▣", R.color.syntax_container_keyword),
        VIEW("▤", R.color.syntax_keyword),
        THEME("◐", R.color.syntax_style_keyword),
        ELEMENT("◆", R.color.syntax_tag),
        LOOP("↻", R.color.syntax_string),
        OBJECT("●", R.color.syntax_make_directive),
        SECTION("§", R.color.rin_accent_green),
        OTHER("•", R.color.rin_on_toolbar_dim)
    }

    private fun classify(label: String): Kind {
        val t = label.trim()
        return when {
            t.startsWith("@view.") -> Kind.VIEW
            t.startsWith("@theme") -> Kind.THEME
            t.startsWith("@element.") -> Kind.ELEMENT
            t.startsWith("@loop") -> Kind.LOOP
            t.startsWith(".object(") -> Kind.OBJECT
            t.startsWith("Section") || t.startsWith("Translations") -> Kind.SECTION
            t.startsWith("@") -> Kind.CONTAINER
            else -> Kind.OTHER
        }
    }

    /** لكل صف: هل يستمر خط رأسي عند كل مستوى سلف سابق (لأب ذلك المستوى أشقاء لاحقون)، وهل هذا
     * الصف آخر شقيق في مستواه هو. محسوبة مسبقاً بمسح تطلّعي بسيط — القوائم هنا صغيرة (عادة
     * عشرات الأسطر)، فلا داعي لخوارزمية شجرية أعقد. */
    private data class RowGuide(val ancestorContinues: BooleanArray, val isLastChild: Boolean)

    private val guides: List<RowGuide> = entries.mapIndexed { i, entry ->
        val d = entry.depth
        var isLast = true
        for (j in i + 1 until entries.size) {
            val dj = entries[j].depth
            if (dj < d) break
            if (dj == d) { isLast = false; break }
        }
        val continues = BooleanArray(d)
        for (k in 0 until d) {
            var cont = false
            for (j in i + 1 until entries.size) {
                val dj = entries[j].depth
                if (dj < k) break
                if (dj == k) { cont = true; break }
            }
            continues[k] = cont
        }
        RowGuide(continues, isLast)
    }

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val guide: OutlineTreeGuideView = view.findViewById(R.id.outlineTreeGuide)
        val badge: TextView = view.findViewById(R.id.outlineKindBadge)
        val label: TextView = view.findViewById(R.id.outlineLabel)
        val lineChip: TextView = view.findViewById(R.id.outlineLineChip)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_outline_entry, parent, false)
        return ViewHolder(view)
    }

    override fun getItemCount(): Int = entries.size

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val entry = entries[position]
        val guide = guides[position]
        val kind = classify(entry.label)
        val color = ContextCompat.getColor(holder.itemView.context, kind.colorRes)

        holder.guide.setDepth(entry.depth, guide.ancestorContinues, guide.isLastChild, color)

        holder.badge.text = kind.glyph
        holder.badge.setTextColor(color)
        holder.badge.background = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(ColorUtils.setAlphaComponent(color, 38))
        }

        holder.label.text = entry.label
        holder.lineChip.text = lineLabel(entry.lineNumber)

        holder.itemView.setOnClickListener { onEntryClick(entry) }
    }
}
