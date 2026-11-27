package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.text.SpannableString
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import java.io.RandomAccessFile
import kotlin.math.min

/**
 * عارض الملفات الثنائية (bin/dat/...): جدول الإزاحة | البايتات | ASCII، بوضعين (سداسي عشري / ثنائي 0-1)،
 * قراءة كسولة عبر RandomAccessFile (يتحمّل ملفات ضخمة)، عدد البايتات بالسطر يتكيّف مع عرض الشاشة،
 * بحث (hex أو نص) مع تمييز البايتات، انتقال لإزاحة (@1F0 / 0x1F0 / #512)، نسخ سطر بالضغط المطوّل،
 * وتكبير الخط بالقرص. يتعرّف على أشهر التواقيع (PNG/ZIP/ELF/PDF...).
 */
class HexViewController(private val a: Activity) {

    companion object {
        private val MAGICS: List<Pair<ByteArray, String>> = listOf(
            byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47) to "PNG",
            byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte()) to "JPEG",
            byteArrayOf(0x47, 0x49, 0x46, 0x38) to "GIF",
            byteArrayOf(0x50, 0x4B, 0x03, 0x04) to "ZIP",
            byteArrayOf(0x7F, 0x45, 0x4C, 0x46) to "ELF",
            byteArrayOf(0x25, 0x50, 0x44, 0x46) to "PDF",
            byteArrayOf(0x1F, 0x8B.toByte()) to "GZIP",
            byteArrayOf(0x52, 0x49, 0x46, 0x46) to "RIFF",
            byteArrayOf(0x4D, 0x5A) to "EXE/DLL",
            byteArrayOf(0x37, 0x7A, 0xBC.toByte(), 0xAF.toByte()) to "7Z",
            byteArrayOf(0x00, 0x61, 0x73, 0x6D) to "WASM",
            byteArrayOf(0x64, 0x65, 0x78, 0x0A) to "DEX"
        )
        fun detect(head: ByteArray): String? =
            MAGICS.firstOrNull { (sig, _) -> head.size >= sig.size && sig.indices.all { head[it] == sig[it] } }?.second
    }

    private val d = a.resources.displayMetrics.density
    private fun dp(v: Int) = (v * d).toInt()

    private val host: View = a.findViewById(R.id.layoutHexHost)
    private val rv: RecyclerView = a.findViewById(R.id.rvHex)
    private val pos: TextView = a.findViewById(R.id.txtHexPos)
    private val prefs = a.getSharedPreferences("rin_hex_viewer", Context.MODE_PRIVATE)
    private val handler = Handler(Looper.getMainLooper())
    private val hidePos = Runnable { pos.animate().alpha(0f).setDuration(200L).withEndAction { pos.visibility = View.GONE }.start() }

    private var raf: RandomAccessFile? = null
    private var file: File? = null
    private var size = 0L
    private var binary = false
    private var bpr = 8
    private var sizeSp = prefs.getFloat("size", 12f)
    private var offDigits = 6
    private var hitStart = -1L
    private var hitEnd = -1L
    private var lastHit = -1L
    private val lm = LinearLayoutManager(a)

    val active: Boolean get() = host.visibility == View.VISIBLE
    val modeLabel: String get() = if (binary) "BIN" else "HEX"

    private val scale = ScaleGestureDetector(a, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(det: ScaleGestureDetector): Boolean {
            sizeSp = (sizeSp * det.scaleFactor).coerceIn(8f, 24f); relayout(); return true
        }
        override fun onScaleEnd(det: ScaleGestureDetector) { prefs.edit().putFloat("size", sizeSp).apply() }
    })

    private val adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
        override fun getItemCount(): Int = if (size == 0L) 0 else ((size + bpr - 1) / bpr).toInt()
        override fun onCreateViewHolder(p: ViewGroup, t: Int): RecyclerView.ViewHolder {
            val tv = TextView(p.context).apply {
                typeface = Typeface.MONOSPACE
                setTextColor(0xFFE6EFE9.toInt())
                maxLines = 1
                setPadding(dp(12), dp(3), dp(12), dp(3))
                layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            }
            return object : RecyclerView.ViewHolder(tv) {}
        }
        override fun onBindViewHolder(h: RecyclerView.ViewHolder, row: Int) {
            val tv = h.itemView as TextView
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, if (binary) sizeSp - 1.5f else sizeSp)
            val (sp, plain) = buildRow(row)
            tv.text = sp
            tv.setOnLongClickListener {
                val cm = a.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("row", plain))
                Toast.makeText(a, R.string.media_hex_copied, Toast.LENGTH_SHORT).show(); true
            }
        }
    }

    init {
        rv.layoutManager = lm
        rv.adapter = adapter
        rv.setHasFixedSize(false)
        rv.addOnScrollListener(object : RecyclerView.OnScrollListener() {
            override fun onScrolled(r: RecyclerView, dx: Int, dy: Int) { if (dy != 0) showPos() }
        })
        rv.addOnLayoutChangeListener { _, l, _, r, _, oldL, _, oldR, _ -> if (r - l != oldR - oldL) relayout() }
    }

    fun dispatch(ev: MotionEvent): Boolean {
        if (!active) return false
        scale.onTouchEvent(ev)
        return scale.isInProgress
    }

    fun applyInsets(top: Int) { host.setPadding(0, dp(78) + top, 0, 0) }

    /** يفتح الملف؛ يعيد وصفاً قصيراً (النوع المكتشف) أو null عند الفشل. */
    fun load(f: File): String? {
        return try {
            close()
            val r = RandomAccessFile(f, "r")
            raf = r; file = f; size = r.length()
            offDigits = if (size > 0xFFFFFFL) 8 else 6
            val head = ByteArray(min(16L, size).toInt()); r.seek(0); r.readFully(head)
            hitStart = -1; hitEnd = -1; lastHit = -1
            host.visibility = View.VISIBLE
            relayout()
            lm.scrollToPositionWithOffset(0, 0)
            detect(head) ?: ""
        } catch (t: Throwable) { null }
    }

    fun hide() { host.visibility = View.GONE }

    fun close() { try { raf?.close() } catch (_: Throwable) {}; raf = null }

    fun toggleMode() {
        val firstOff = lm.findFirstVisibleItemPosition().coerceAtLeast(0).toLong() * bpr
        binary = !binary
        relayout()
        lm.scrollToPositionWithOffset((firstOff / bpr).toInt(), 0)
    }

    /** عدد البايتات بالسطر حسب عرض الشاشة وحجم الخط والوضع. */
    private fun relayout() {
        val w = rv.width
        if (w <= 0) { rv.post { if (rv.width > 0) relayout() }; return }
        val paint = Paint().apply {
            typeface = Typeface.MONOSPACE
            textSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, if (binary) sizeSp - 1.5f else sizeSp, a.resources.displayMetrics)
        }
        val chars = ((w - dp(24)) / paint.measureText("0")).toInt()
        val fixed = offDigits + 2 + 1
        bpr = if (binary) ((chars - fixed) / 10).coerceIn(1, 16) else (((chars - fixed) / 4) / 4 * 4).coerceIn(4, 32)
        adapter.notifyDataSetChanged()
    }

    private fun showPos() {
        val row = lm.findFirstVisibleItemPosition().coerceAtLeast(0)
        val off = row.toLong() * bpr
        val pct = if (size > 0) (off * 100 / size).toInt() else 0
        pos.text = "0x%X / 0x%X  (%d%%)".format(off, size, pct)
        handler.removeCallbacks(hidePos)
        pos.visibility = View.VISIBLE
        pos.animate().alpha(1f).setDuration(80L).start()
        handler.postDelayed(hidePos, 900L)
    }

    private fun buildRow(row: Int): Pair<SpannableString, String> {
        val start = row.toLong() * bpr
        val n = min(bpr.toLong(), size - start).toInt().coerceAtLeast(0)
        val buf = ByteArray(n)
        try { synchronized(this) { raf?.let { it.seek(start); it.readFully(buf) } } } catch (_: Throwable) {}
        val sb = StringBuilder()
        sb.append(("%0" + offDigits + "X").format(start)).append("  ")
        val hexStart = sb.length
        val cell = if (binary) 9 else 3
        for (i in 0 until bpr) {
            if (i < n) {
                val v = buf[i].toInt() and 0xFF
                if (binary) sb.append(Integer.toBinaryString(v or 0x100).substring(1)) else sb.append("%02X".format(v))
                sb.append(' ')
            } else sb.append(" ".repeat(cell))
        }
        sb.append(' ')
        val asciiStart = sb.length
        for (i in 0 until n) { val v = buf[i].toInt() and 0xFF; sb.append(if (v in 32..126) v.toChar() else '\u00B7') }
        val sp = SpannableString(sb.toString())
        val ex = Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        sp.setSpan(ForegroundColorSpan(0xFF4F6157.toInt()), 0, offDigits, ex)
        sp.setSpan(ForegroundColorSpan(0xFF8FB5A0.toInt()), asciiStart, sp.length, ex)
        val cw = if (binary) 8 else 2
        for (i in 0 until n) {
            val abs = start + i
            val cs = hexStart + i * cell
            if (abs in hitStart until hitEnd) {
                sp.setSpan(BackgroundColorSpan(0xFF3DF59A.toInt()), cs, cs + cw, ex)
                sp.setSpan(ForegroundColorSpan(0xFF00140B.toInt()), cs, cs + cw, ex)
                sp.setSpan(BackgroundColorSpan(0x663DF59A), asciiStart + i, asciiStart + i + 1, ex)
            } else if (buf[i].toInt() == 0) {
                sp.setSpan(ForegroundColorSpan(0xFF3A4A41.toInt()), cs, cs + cw, ex)
            }
        }
        return sp to sb.toString()
    }

    // ---------------- بحث / انتقال ----------------

    fun showSearchDialog() {
        val edt = EditText(a).apply {
            hint = a.getString(R.string.media_hex_search_hint)
            setSingleLine(); typeface = Typeface.MONOSPACE
            setPadding(dp(24), dp(16), dp(24), dp(12))
        }
        AlertDialog.Builder(a)
            .setTitle(R.string.media_hex_search_title)
            .setView(edt)
            .setPositiveButton(R.string.media_hex_search_btn) { _, _ -> runQuery(edt.text.toString().trim()) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun runQuery(q: String) {
        if (q.isEmpty()) return
        val jump: Long? = when {
            q.startsWith("@") -> q.substring(1).trim().toLongOrNull(16)
            q.startsWith("0x", true) -> q.substring(2).trim().toLongOrNull(16)
            q.startsWith("#") -> q.substring(1).trim().toLongOrNull()
            else -> null
        }
        if (jump != null) { goTo(jump.coerceIn(0, maxOf(size - 1, 0)), 1); return }
        val hexOnly = Regex("^([0-9A-Fa-f]{2}\\s*)+$").matches(q)
        val pattern: ByteArray = if (hexOnly) q.replace(Regex("\\s+"), "").chunked(2).map { it.toInt(16).toByte() }.toByteArray()
                                 else q.toByteArray(Charsets.UTF_8)
        val ci = !hexOnly
        val f = file ?: return
        val from = (if (lastHit >= 0) lastHit + 1 else lm.findFirstVisibleItemPosition().coerceAtLeast(0).toLong() * bpr)
        Thread {
            val found = search(f, pattern, from, ci).let { if (it < 0 && from > 0) search(f, pattern, 0, ci) else it }
            a.runOnUiThread {
                if (found < 0) Toast.makeText(a, R.string.media_hex_not_found, Toast.LENGTH_SHORT).show()
                else { lastHit = found; goTo(found, pattern.size) }
            }
        }.start()
    }

    private fun goTo(off: Long, len: Int) {
        hitStart = off; hitEnd = off + len
        adapter.notifyDataSetChanged()
        lm.scrollToPositionWithOffset((off / bpr).toInt(), dp(24))
        showPos()
    }

    private fun search(f: File, p: ByteArray, from: Long, ci: Boolean): Long {
        if (p.isEmpty()) return -1
        fun fold(b: Byte): Int { val v = b.toInt() and 0xFF; return if (ci && v in 65..90) v + 32 else v }
        val pat = IntArray(p.size) { fold(p[it]) }
        try {
            RandomAccessFile(f, "r").use { r ->
                val chunk = 64 * 1024
                val buf = ByteArray(chunk + p.size)
                var pos = from
                while (pos < r.length()) {
                    r.seek(pos)
                    val read = r.read(buf, 0, buf.size)
                    if (read <= 0) break
                    var i = 0
                    while (i <= read - p.size) {
                        var k = 0
                        while (k < p.size && fold(buf[i + k]) == pat[k]) k++
                        if (k == p.size) return pos + i
                        i++
                    }
                    if (read < buf.size) break
                    pos += chunk
                }
            }
        } catch (_: Throwable) {}
        return -1
    }
}
