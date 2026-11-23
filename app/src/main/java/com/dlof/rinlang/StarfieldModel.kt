package com.dlof.rinlang

import kotlin.math.PI
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.cos

/**
 * نموذج حقل نجوم متحرّك (منطق خالص): نجوم بثلاث طبقات عمق تنجرف ببطء (بارالاكس) وتتلألأ،
 * مع «وميضات» لامعة نادرة، ونيازك عابرة (حتى اثنين معاً)، وانجراف بطيء للسدم.
 * الإحداثيات مطبَّعة 0..1 فلا يعتمد على حجم الشاشة؛ [StarfieldDrawable] يرسمه.
 */
class StarfieldModel(seed: Long = 7L, count: Int = 140) {
    class Star(
        var x: Float, var y: Float,
        val size: Float,        // نصف قطر بالـ dp تقريباً (0.5..2)
        val base: Float,        // سطوع أساسي 0.35..1
        val phase: Float, val speed: Float,   // وميض
        val layer: Int,         // 0 بعيد … 2 قريب
        val tint: Int           // 0 أبيض، 1 مائل للأزرق، 2 مائل للذهبي
    )

    class Meteor(var x: Float, var y: Float, val vx: Float, val vy: Float, var life: Float, val maxLife: Float)

    private val rnd = java.util.Random(seed)
    val stars: List<Star> = List(count) {
        val layer = when { it % 6 == 0 -> 2; it % 2 == 0 -> 1; else -> 0 }
        Star(
            rnd.nextFloat(), rnd.nextFloat(),
            size = 0.5f + layer * 0.45f + rnd.nextFloat() * 0.5f,
            base = 0.35f + rnd.nextFloat() * 0.65f,
            phase = rnd.nextFloat() * (2 * PI).toFloat(),
            speed = 0.8f + rnd.nextFloat() * 2.2f,
            layer = layer,
            tint = when (rnd.nextInt(10)) { 0, 1 -> 1; 2 -> 2; else -> 0 }
        )
    }

    /** النيازك الحالية (حتى [MAX_METEORS])؛ [meteor] أولها للتوافق. */
    val meteors = ArrayList<Meteor>(MAX_METEORS)
    val meteor: Meteor? get() = meteors.firstOrNull()
    private var nextMeteorIn = 3f + rnd.nextFloat() * 4f
    var time = 0f
        private set

    /** سرعة الانجراف لكل طبقة (جزء من العرض بالثانية). */
    private val drift = floatArrayOf(0.004f, 0.009f, 0.016f)

    fun step(dtSec: Float) {
        val dt = dtSec.coerceIn(0f, 0.25f)
        time += dt
        for (s in stars) {
            s.x -= drift[s.layer] * dt
            s.y += drift[s.layer] * 0.15f * dt
            if (s.x < 0f) s.x += 1f
            if (s.y > 1f) s.y -= 1f
        }
        val it = meteors.iterator()
        while (it.hasNext()) {
            val m = it.next()
            m.x += m.vx * dt; m.y += m.vy * dt; m.life += dt
            if (m.life >= m.maxLife || m.x < -0.3f || m.y > 1.3f) it.remove()
        }
        nextMeteorIn -= dt
        if (nextMeteorIn <= 0f && meteors.size < MAX_METEORS) {
            nextMeteorIn = 5f + rnd.nextFloat() * 8f
            val x0 = 0.45f + rnd.nextFloat() * 0.65f
            meteors += Meteor(
                x0, -0.05f + rnd.nextFloat() * 0.3f,
                vx = -(0.55f + rnd.nextFloat() * 0.35f), vy = 0.28f + rnd.nextFloat() * 0.22f,
                life = 0f, maxLife = 1.0f + rnd.nextFloat() * 0.5f
            )
        }
    }

    /** سطوع النجمة الآن (0..1): تلألؤ ناعم + وميض لامع نادر للطبقتين القريبتين. */
    fun alpha(s: Star): Float {
        var v = s.base * (0.62f + 0.38f * sin(s.phase + s.speed * time))
        if (s.layer >= 1) {
            val sp = max(0f, sin(s.phase * 3f + s.speed * time * 0.35f)).pow(14)
            v += sp * 0.55f
        }
        return v.coerceIn(0f, 1f)
    }

    /** شفافية النيزك: يظهر ثم يخبو. */
    fun meteorAlpha(m: Meteor): Float {
        val t = m.life / m.maxLife
        return (if (t < 0.15f) t / 0.15f else 1f - (t - 0.15f) / 0.85f).coerceIn(0f, 1f)
    }

    /** انجراف السدم البطيء كجزء من العرض/الارتفاع (±3% / ±2%). */
    fun nebulaDx(): Float = sin(time * 0.05f) * 0.03f
    fun nebulaDy(): Float = cos(time * 0.04f) * 0.02f

    companion object { const val MAX_METEORS = 2 }
}
