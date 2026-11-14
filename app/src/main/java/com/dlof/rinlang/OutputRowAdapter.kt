package com.dlof.rinlang

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Typeface
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.util.LruCache
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import java.util.Collections

/** حالة سطر المؤشر الأخير في أسفل القائمة. */
enum class OutFooterMode { HIDDEN, QUEUED, RUNNING, PROMPT }

/**
 * يعرض صفوف [RinOutputTypewriter] في RecyclerView. الصفوف **تظهر تدريجياً**: لا يُعرض إلا ما بلغه
 * مؤشر الكتابة (الصف الجاري + ما قبله)، فيبدو الناتج وهو يُطبع فعلاً لا كأنه محمَّل كله ثم مخفيّ.
 *
 * يدعم فلتر مستوى وبحثاً نصياً بدون إعادة بناء الصفوف نفسها: [positions] قائمة مرتّبة بفهارس الصفوف
 * المطابقة، وتحديث نص صف واحد أثناء الكتابة يمرّ عبر payload خفيف (setText فقط).
 */
class OutputRowAdapter(
    private val context: Context,
    private val writer: RinOutputTypewriter
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    /** يُستدعى عند نقر صف (فهرس الصف في [RinOutputTypewriter.rows]). */
    var onRowClick: ((Int) -> Unit)? = null
    var onRowLongClick: ((Int) -> Unit)? = null

    var footerMode: OutFooterMode = OutFooterMode.HIDDEN
        private set
    private var blinkOn = true
    private var dots = 0

    /** فلتر المستوى الحالي (null = الكل). مجموعة مستويات مقبولة. */
    private var levelFilter: Set<LogKind>? = null
    private var query: String = ""

    /** فهارس الصفوف الظاهرة فعلاً (مرتّبة تصاعدياً). */
    private val positions = ArrayList<Int>()

    /** عدد الصفوف التي كُشفت (فهارس أقل منه مسموح ظهورها). */
    private var revealed = 0

    private val dp = context.resources.displayMetrics.density
    private val thumbCache = object : LruCache<String, Bitmap>(8) {}

    private companion object {
        const val TYPE_ROW = 0
        const val TYPE_FOOTER = 1
        const val PAYLOAD_TEXT = "text"
        const val PAYLOAD_FOOTER = "footer"
        const val CURSOR = "▌"
    }

    // ------------------------------------------------------------------ بيانات/فلاتر

    private fun matches(row: OutRow): Boolean {
        val f = levelFilter
        if (f != null) {
            // صفوف الحالة/السؤال/الجواب لا تُخفى بفلتر المستوى (تبقى سياقاً للقراءة).
            val isOutput = row.kind == OutRowKind.LINE || row.kind == OutRowKind.IMAGE
            if (isOutput && row.line.kind !in f) return false
        }
        if (query.isNotEmpty() && !row.text.contains(query, ignoreCase = true)) return false
        return true
    }

    fun setFilter(kinds: Set<LogKind>?, newQuery: String) {
        levelFilter = kinds
        query = newQuery.trim()
        rebuild()
    }

    /** يعيد بناء القائمة الظاهرة من الصفر (بعد تغيّر فلتر/بحث أو الانتقال إلى تشغيل آخر). */
    fun rebuild() {
        positions.clear()
        for (i in 0 until minOf(revealed, writer.rows.size)) {
            if (matches(writer.rows[i])) positions.add(i)
        }
        notifyDataSetChanged()
    }

    fun reset() {
        revealed = 0
        positions.clear()
        notifyDataSetChanged()
    }

    /** يكشف الصفوف حتى [newRevealed] (حصرياً) ويُدرج المطابق منها. */
    fun reveal(newRevealed: Int) {
        val target = minOf(newRevealed, writer.rows.size)
        if (target <= revealed) return
        val startPos = positions.size
        for (i in revealed until target) {
            if (matches(writer.rows[i])) positions.add(i)
        }
        revealed = target
        val added = positions.size - startPos
        if (added > 0) notifyItemRangeInserted(startPos, added)
    }

    val revealedCount: Int get() = revealed

    /** نص صف تغيّر (كُتب منه حرف أو انتقل إليه/عنه المؤشر). */
    fun changedRow(rowIndex: Int) {
        if (rowIndex >= revealed) return
        val pos = Collections.binarySearch(positions, rowIndex)
        if (pos >= 0) notifyItemChanged(pos, PAYLOAD_TEXT)
    }

    fun rowIndexAt(position: Int): Int = positions.getOrElse(position) { -1 }

    val footerPosition: Int get() = positions.size

    fun setFooter(mode: OutFooterMode) {
        if (mode == footerMode) return
        footerMode = mode
        notifyItemChanged(footerPosition, PAYLOAD_FOOTER)
    }

    /** نبضة المؤشر الوامض / نقاط «قيد التشغيل». */
    fun tickBlink(on: Boolean, dotCount: Int) {
        blinkOn = on
        dots = dotCount
        if (footerMode != OutFooterMode.HIDDEN) notifyItemChanged(footerPosition, PAYLOAD_FOOTER)
    }

    // ------------------------------------------------------------------ RecyclerView

    override fun getItemCount(): Int = positions.size + 1

    override fun getItemViewType(position: Int): Int =
        if (position == positions.size) TYPE_FOOTER else TYPE_ROW

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return if (viewType == TYPE_FOOTER) {
            FooterHolder(inflater.inflate(R.layout.item_output_footer, parent, false))
        } else {
            RowHolder(inflater.inflate(R.layout.item_output_row, parent, false))
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        if (holder is FooterHolder) holder.bindFooter() else (holder as RowHolder).bindFull(position)
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int, payloads: MutableList<Any>) {
        if (payloads.isEmpty()) {
            onBindViewHolder(holder, position)
            return
        }
        if (holder is FooterHolder) holder.bindFooter() else (holder as RowHolder).bindText(position)
    }

    // ------------------------------------------------------------------ الفوتر

    private inner class FooterHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val txt: TextView = view.findViewById(R.id.txtOutFooter)

        fun bindFooter() {
            val accent = ContextCompat.getColor(context, R.color.rin_accent)
            val cursor = ContextCompat.getColor(context, R.color.out_cursor)
            val dim = ContextCompat.getColor(context, R.color.out_dim)
            val sb = SpannableStringBuilder()
            when (footerMode) {
                OutFooterMode.HIDDEN -> Unit
                OutFooterMode.QUEUED -> sb.append("○ ").append(context.getString(R.string.output_queued))
                OutFooterMode.RUNNING -> {
                    sb.append("● ").append(context.getString(R.string.output_running))
                    sb.append(".".repeat(dots.coerceIn(0, 3)))
                    sb.setSpan(ForegroundColorSpan(cursor), 0, 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                }
                OutFooterMode.PROMPT -> {
                    sb.append("❯ ")
                    sb.setSpan(ForegroundColorSpan(accent), 0, 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    val start = sb.length
                    sb.append(CURSOR)
                    sb.setSpan(
                        ForegroundColorSpan(if (blinkOn) cursor else 0x00000000),
                        start, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                }
            }
            txt.setTextColor(dim)
            txt.text = sb
        }
    }

    // ------------------------------------------------------------------ الصفوف

    private inner class RowHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val root: View = view.findViewById(R.id.rowRoot)
        private val gutter: TextView = view.findViewById(R.id.txtOutGutter)
        private val rail: View = view.findViewById(R.id.viewOutRail)
        private val content: View = view.findViewById(R.id.llOutContent)
        private val icon: ImageView = view.findViewById(R.id.imgOutIcon)
        private val line: TextView = view.findViewById(R.id.txtOutLine)
        private val thumb: ImageView = view.findViewById(R.id.imgOutThumb)

        private var boundRow = -1

        init {
            root.setOnClickListener { if (boundRow >= 0) onRowClick?.invoke(boundRow) }
            root.setOnLongClickListener {
                if (boundRow >= 0) { onRowLongClick?.invoke(boundRow); true } else false
            }
        }

        private fun colorOf(row: OutRow): Int = when (row.kind) {
            OutRowKind.PROMPT -> ContextCompat.getColor(context, R.color.rin_accent)
            OutRowKind.ANSWER -> ContextCompat.getColor(context, R.color.out_dim)
            OutRowKind.STATUS -> ContextCompat.getColor(context, row.line.kind.colorRes)
            else -> {
                val mark = row.line.container
                when {
                    mark != null && mark.role == ContainerRole.OPEN -> mark.family.accent
                    mark != null && mark.role == ContainerRole.CLOSE -> dimmed(mark.family.accent)
                    row.line.kind == LogKind.PLAIN -> ContextCompat.getColor(context, R.color.out_text)
                    else -> ContextCompat.getColor(context, row.line.kind.colorRes)
                }
            }
        }

        private fun dimmed(color: Int): Int = (color and 0x00FFFFFF) or (0xB0 shl 24)

        /** ربط كامل: كل خصائص الصف (يُستدعى عند أول ربط/إعادة تدوير). */
        fun bindFull(position: Int) {
            val idx = positions[position]
            val row = writer.rows[idx]
            boundRow = idx

            gutter.text = if (row.number > 0) row.number.toString() else ""

            val railColor = row.railColor
            rail.setBackgroundColor(railColor?.let { (it and 0x00FFFFFF) or (0x99 shl 24) } ?: 0x00000000)

            val indentPx = (row.depth * 12 * dp + row.line.indent.coerceAtMost(40) * 3 * dp).toInt()
            content.setPaddingRelative(indentPx + (4 * dp).toInt(), content.paddingTop, content.paddingEnd, content.paddingBottom)

            val tint = colorOf(row)
            val iconRes = when (row.kind) {
                OutRowKind.PROMPT -> null
                OutRowKind.ANSWER, OutRowKind.STATUS -> null
                else -> row.line.kind.icon
            }
            if (iconRes != null && row.line.container?.role != ContainerRole.CLOSE) {
                icon.setImageResource(iconRes)
                icon.imageTintList = android.content.res.ColorStateList.valueOf(tint)
                icon.visibility = View.VISIBLE
            } else {
                icon.setImageDrawable(null)
                icon.visibility = if (row.kind == OutRowKind.STATUS || row.kind == OutRowKind.PROMPT || row.kind == OutRowKind.ANSWER)
                    View.GONE else View.INVISIBLE
            }

            root.setBackgroundColor(
                when {
                    row.kind != OutRowKind.LINE -> 0x00000000
                    row.line.kind == LogKind.ERROR -> ContextCompat.getColor(context, R.color.out_row_error_bg)
                    row.line.kind == LogKind.WARNING -> ContextCompat.getColor(context, R.color.out_row_warn_bg)
                    else -> 0x00000000
                }
            )

            line.setTextColor(tint)
            line.setTypeface(
                Typeface.MONOSPACE,
                if (row.kind == OutRowKind.STATUS || row.line.container?.role == ContainerRole.OPEN) Typeface.BOLD else Typeface.NORMAL
            )
            bindText(position)
        }

        /** تحديث خفيف: نص الصف + المؤشر + الصورة المصغّرة. */
        fun bindText(position: Int) {
            val idx = positions.getOrNull(position) ?: return
            val row = writer.rows[idx]
            boundRow = idx
            val shown = row.shownText()
            val sb = SpannableStringBuilder()
            if (row.kind == OutRowKind.PROMPT) sb.append("❯ ")
            if (row.kind == OutRowKind.ANSWER) sb.append("↳ ")
            val base = sb.length
            sb.append(shown)

            // تظليل نتائج البحث داخل الجزء المكتوب فقط.
            if (query.isNotEmpty()) {
                var from = 0
                while (true) {
                    val at = shown.indexOf(query, from, ignoreCase = true)
                    if (at < 0) break
                    sb.setSpan(
                        BackgroundColorSpan(0x66E8B23D), base + at, base + at + query.length,
                        Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    from = at + maxOf(query.length, 1)
                }
            }

            val typingHere = writer.isTyping && writer.cursorRow == idx
            if (typingHere) {
                val s = sb.length
                sb.append(CURSOR)
                sb.setSpan(
                    ForegroundColorSpan(ContextCompat.getColor(context, R.color.out_cursor)),
                    s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            } else if (row.isDone && row.line.errorLine != null && row.kind == OutRowKind.LINE) {
                // تلميح «اذهب إلى السطر» بعد اكتمال كتابة سطر الخطأ.
                val s = sb.length
                sb.append("  ↗ L").append((row.line.errorLine ?: 0).toString())
                sb.setSpan(
                    ForegroundColorSpan(ContextCompat.getColor(context, R.color.out_dim)),
                    s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                sb.setSpan(StyleSpan(Typeface.ITALIC), s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            line.text = sb

            bindThumb(row)
        }

        private fun bindThumb(row: OutRow) {
            val rel = row.line.imageRelPath
            if (row.kind != OutRowKind.IMAGE || rel == null || !row.isDone) {
                thumb.visibility = View.GONE
                thumb.setImageDrawable(null)
                return
            }
            val baseDir = try { RinEngine.currentBaseDir() } catch (t: Throwable) { "" }
            val file = RinConsoleFormatter.resolveExistingFile(rel, baseDir)
            if (file == null) {
                thumb.visibility = View.GONE
                return
            }
            val widthDp = (row.line.imageWidthDp ?: 220f).coerceIn(60f, 320f)
            val widthPx = (widthDp * dp).toInt()
            val key = file.absolutePath + "@" + file.lastModified() + "@" + widthPx
            val bmp = thumbCache.get(key) ?: decode(file, widthPx)?.also { thumbCache.put(key, it) }
            if (bmp == null) {
                thumb.visibility = View.GONE
                return
            }
            thumb.layoutParams = thumb.layoutParams.apply { width = widthPx }
            thumb.setImageBitmap(bmp)
            thumb.visibility = View.VISIBLE
        }

        private fun decode(file: File, reqWidthPx: Int): Bitmap? = try {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, bounds)
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
                null
            } else {
                var sample = 1
                while (bounds.outWidth / (sample * 2) >= reqWidthPx) sample *= 2
                BitmapFactory.decodeFile(file.absolutePath, BitmapFactory.Options().apply { inSampleSize = sample })
            }
        } catch (t: Throwable) {
            null
        }
    }
}
