package com.dlof.rinlang.store

/**
 * مقارنة إصدارات مبسّطة (semver-lite) للتحقق من تبعيات حزم المتجر.
 * يدعم أرقام إصدار من الشكل x.y.z (أجزاء غير رقمية تُهمَل) وثلاث صيغ للشرط:
 *  - "^1.2.0"  → متوافق: نفس الرقم الرئيسي (major) وإصدار أكبر أو يساوي المطلوب.
 *  - ">=1.2.0" → أكبر من أو يساوي المطلوب فقط (بلا قيد على major).
 *  - "1.2.0"   → مطابقة تامة (افتراضي عند غياب أي رمز).
 */
object VersionUtils {

    /** يحوّل "1.2.3-beta" إلى [1,2,3]؛ أي جزء غير رقمي يُقرأ كصفر. */
    fun parse(version: String): List<Int> =
        version.trim().split(".").map { part ->
            part.takeWhile { it.isDigit() }.toIntOrNull() ?: 0
        }

    /** يقارن إصدارين: سالب إن كان [a] أقدم، صفر إن تساويا، موجب إن كان [a] أحدث. */
    fun compare(a: String, b: String): Int {
        val pa = parse(a)
        val pb = parse(b)
        val size = maxOf(pa.size, pb.size)
        for (i in 0 until size) {
            val diff = (pa.getOrElse(i) { 0 }) - (pb.getOrElse(i) { 0 })
            if (diff != 0) return diff
        }
        return 0
    }

    /** هل يحقّق [installedVersion] شرط [requirement]؟ */
    fun satisfies(installedVersion: String, requirement: String): Boolean {
        val req = requirement.trim()
        return when {
            req.startsWith("^") -> {
                val target = req.removePrefix("^")
                val sameMajor = parse(installedVersion).getOrElse(0) { 0 } == parse(target).getOrElse(0) { 0 }
                sameMajor && compare(installedVersion, target) >= 0
            }
            req.startsWith(">=") -> compare(installedVersion, req.removePrefix(">=")) >= 0
            req.isBlank() -> true
            else -> compare(installedVersion, req) == 0
        }
    }
}
