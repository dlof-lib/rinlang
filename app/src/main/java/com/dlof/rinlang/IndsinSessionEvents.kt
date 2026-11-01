package com.dlof.rinlang

import android.view.Choreographer
import org.json.JSONException
import org.json.JSONObject

/**
 * نتيجة مطبَّعة لأي حدث جلسة (tap / longPress / doubleTap / hover / tick / updateSource).
 *
 * الشكل المصدري: `{"ok":true,"handled":bool,"targetId":N,"changed":[...],"animating":bool,
 * "fabric":{...},"error":"..."}`. كل الحقول اختيارية عدا [ok]؛ [fabricJson] يُترك نصاً خاماً كي لا
 * نعيد تحليل إطار كبير في كل لمسة (المُرسِم هو من يقرؤه عند الحاجة).
 */
data class IndsinEvent(
    val ok: Boolean,
    val handled: Boolean,
    val targetId: Int?,
    val changed: List<String>,
    val animating: Boolean,
    val fabricJson: String?,
    val error: String?,
) {
    /** يجب إعادة رسم الإطار: حالة Warp تغيّرت، أو حركة جارية، أو وصلت لقطة جديدة. */
    val needsRedraw: Boolean get() = ok && (changed.isNotEmpty() || animating || fabricJson != null)

    companion object {
        fun failure(message: String) = IndsinEvent(false, false, null, emptyList(), false, null, message)

        /** لا يرمي أبداً: JSON تالف أو فارغ يتحوّل إلى [failure]. */
        fun parse(json: String?): IndsinEvent {
            if (json.isNullOrBlank()) return failure("empty result")
            return try {
                val o = JSONObject(json)
                val changed = o.optJSONArray("changed")?.let { a -> List(a.length()) { a.opt(it).toString() } }
                    ?: emptyList()
                IndsinEvent(
                    ok = o.optBoolean("ok", false),
                    handled = o.optBoolean("handled", false),
                    targetId = if (o.has("targetId") && !o.isNull("targetId")) o.optInt("targetId") else null,
                    changed = changed,
                    animating = o.optBoolean("animating", false),
                    fabricJson = o.optJSONObject("fabric")?.toString(),
                    error = o.optString("error").takeIf { o.has("error") && it.isNotEmpty() },
                )
            } catch (e: JSONException) {
                failure("malformed result: ${e.message}")
            }
        }
    }
}

// ---- واجهة مطبَّعة فوق الدوال الخام (الخام يبقى متاحاً لمن يحتاجه) --------------------------------

fun RinEngine.IndsinSession.tapEvent(x: Double, y: Double) = IndsinEvent.parse(tap(x, y))
fun RinEngine.IndsinSession.longPressEvent(x: Double, y: Double) = IndsinEvent.parse(longPress(x, y))
fun RinEngine.IndsinSession.doubleTapEvent(x: Double, y: Double) = IndsinEvent.parse(doubleTap(x, y))
fun RinEngine.IndsinSession.hoverEvent(x: Double, y: Double, entering: Boolean) =
    IndsinEvent.parse(hover(x, y, entering))
fun RinEngine.IndsinSession.tickEvent() = IndsinEvent.parse(tick())
fun RinEngine.IndsinSession.updateSourceEvent(newSource: String) = IndsinEvent.parse(updateSource(newSource))

/** مثل `use {}` لكن للجلسة (لا تنفّذ AutoCloseable حتى لا نغيّر توقيعها العام). */
inline fun <R> RinEngine.IndsinSession.use(block: (RinEngine.IndsinSession) -> R): R =
    try { block(this) } finally { close() }

/**
 * حلقة الحركة: تُجدول `tick()` على كل إطار (Choreographer) ما دامت آخر نتيجة `animating=true`،
 * وتتوقف ذاتياً عند انتهاء الحركة أو إغلاق الجلسة. يجب استدعاؤها من خيط الواجهة (Choreographer
 * مرتبط بـ Looper الخيط الحالي). [onFrame] يُستدعى بكل نتيجة لإعادة الرسم.
 *
 * أعدها بعد كل حدث أعاد `animating=true`؛ استدعاءات متتالية آمنة (تُلغي الحلقة السابقة).
 * @return [Cancellable] لإيقافها يدوياً (مثلاً في onPause).
 */
fun RinEngine.IndsinSession.animate(onFrame: (IndsinEvent) -> Unit): Cancellable {
    val choreographer = Choreographer.getInstance()
    val handle = Cancellable()
    val callback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (handle.cancelled || isClosed) return
            val event = tickEvent()
            onFrame(event)
            if (event.ok && event.animating) choreographer.postFrameCallback(this)
        }
    }
    choreographer.postFrameCallback(callback)
    handle.onCancel = { choreographer.removeFrameCallback(callback) }
    return handle
}

class Cancellable internal constructor() {
    @Volatile var cancelled = false
        private set
    internal var onCancel: (() -> Unit)? = null
    fun cancel() { if (!cancelled) { cancelled = true; onCancel?.invoke() } }
}
