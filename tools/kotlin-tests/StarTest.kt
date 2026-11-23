import com.dlof.rinlang.*
fun main() {
    val a = StarfieldModel(1L, 90); val b = StarfieldModel(1L, 90)
    check(a.stars.size == 90 && a.stars.map { it.x } == b.stars.map { it.x })
    var sawMeteor = false; var maxA = 0f; var minA = 1f
    repeat(60 * 60) {   // دقيقة بـ 60fps
        a.step(1 / 60f)
        for (s in a.stars) { check(s.x in 0f..1f && s.y in 0f..1f.plus(0f)); val al = a.alpha(s); maxA = maxOf(maxA, al); minA = minOf(minA, al); check(al in 0f..1f) }
        a.meteor?.let { sawMeteor = true; check(a.meteorAlpha(it) in 0f..1f) }
    }
    check(sawMeteor); check(maxA > minA)
    val x0 = a.stars[0].x; a.step(100f); check(a.stars[0].x in 0f..1f)   // قفزة كبيرة تُقيَّد
    check(a.time > 60f)
    val c = StarfieldModel(2L, 30); val xs = c.stars.map { it.x }; c.step(1f)
    check(c.stars.map { it.x } != xs)   // تتحرك
    // نيزكان كحدٍّ أقصى، وكلها ضمن النطاق
    val d = StarfieldModel(3L); var maxM = 0
    repeat(60 * 120) { d.step(1 / 60f); maxM = maxOf(maxM, d.meteors.size); check(d.meteors.size <= StarfieldModel.MAX_METEORS) }
    check(maxM >= 1)
    val ox = d.nebulaDx(); check(kotlin.math.abs(ox) <= 0.0301f && kotlin.math.abs(d.nebulaDy()) <= 0.0201f)
    println("OK")
}
