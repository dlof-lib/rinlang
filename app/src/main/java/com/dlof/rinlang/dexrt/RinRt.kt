@file:JvmName("RinRt")
@file:Suppress("unused", "FunctionName")

package com.dlof.rinlang.dexrt

import java.math.BigDecimal
import java.math.MathContext
import java.math.RoundingMode

/*
 * مكتبة تشغيل الشيفرة المُصرَّفة إلى DEX. الـ bytecode المولَّد (DexCodegen) يستدعي هذه الدوال الثابتة
 * مباشرة؛ دلالاتها منقولة من المفسّر الأصلي (rin_interpreter.cpp): نفس الأخطاء والرموز E00xx
 * ونفس تنسيق الأرقام والنصوص (بايتات UTF-8) والمساواة التركيبية.
 * القيم: nil=null · رقم=Double · منطقي=Boolean · نص=String · مصفوفة=RinArray · قاموس=RinMap.
 * ملاحظة: يجب إبقاء هذه الحزمة في proguard (-keep) لأن dex المولَّد يشير إليها بالاسم.
 */

class RinArray(@JvmField val items: ArrayList<Any?> = ArrayList())

class RinMap {
    @JvmField val keys = ArrayList<Any?>()
    @JvmField val vals = ArrayList<Any?>()
    fun find(k: Any?): Int { for (i in keys.indices) if (valuesEqual(keys[i], k)) return i; return -1 }
    fun put(k: Any?, v: Any?) { val i = find(k); if (i >= 0) vals[i] = v else { keys.add(k); vals.add(v) } }
}

class RinRuntimeError(val code: String, message: String, @JvmField val line: Int) : RuntimeException(message)

/** وجهة المخرجات (يضبطها المُشغِّل؛ الافتراضي System.out). */
@JvmField var out: ((String) -> Unit)? = null
@JvmField var line: Int = 0
@JvmField val UNDEF: Any = Any()

private fun err(code: String, msg: String): Nothing = throw RinRuntimeError(code, msg, line)

// ------------------------------------------------------------------ أنواع وعرض
fun typeName(v: Any?): String = when (v) {
    null -> "nil"; is Double -> "number"; is String -> "string"; is Boolean -> "bool"
    is RinArray -> "array"; is RinMap -> "map"; else -> "value"
}

fun fmtNum(n: Double): String {
    if (n.isNaN()) return "nan"
    if (n.isInfinite()) return if (n > 0) "inf" else "-inf"
    if (n >= -9.223372036854775808E18 && n < 9.223372036854775808E18 && n == n.toLong().toDouble()) return n.toLong().toString()
    return fmtG(n, 6)
}

/** مكافئ دقيق لـ printf("%.{p}g") (ما يفعله ostream الافتراضي في C++). */
private fun fmtG(v: Double, p: Int): String {
    val bd = BigDecimal(v).round(MathContext(p, RoundingMode.HALF_EVEN))
    val neg = bd.signum() < 0
    val abs = bd.abs()
    val unscaled = abs.unscaledValue().toString().trimEnd('0').ifEmpty { "0" }
    val exp = abs.precision() - abs.scale() - 1
    val sb = StringBuilder()
    if (neg) sb.append('-')
    if (exp < -4 || exp >= p) {
        sb.append(unscaled[0])
        if (unscaled.length > 1) sb.append('.').append(unscaled, 1, unscaled.length)
        sb.append('e').append(if (exp < 0) '-' else '+')
        val e = Math.abs(exp)
        if (e < 10) sb.append('0')
        sb.append(e)
    } else if (exp >= 0) {
        if (unscaled.length <= exp + 1) { sb.append(unscaled); repeat(exp + 1 - unscaled.length) { sb.append('0') } }
        else sb.append(unscaled, 0, exp + 1).append('.').append(unscaled, exp + 1, unscaled.length)
    } else {
        sb.append("0.")
        repeat(-exp - 1) { sb.append('0') }
        sb.append(unscaled)
    }
    return sb.toString()
}

private fun quote(s: String): String {
    val sb = StringBuilder("\"")
    for (c in s) when (c) { '"' -> sb.append("\\\""); '\\' -> sb.append("\\\\"); '\n' -> sb.append("\\n"); '\t' -> sb.append("\\t"); '\r' -> sb.append("\\r"); else -> sb.append(c) }
    return sb.append('"').toString()
}

fun display(v: Any?, top: Boolean = true): String = when (v) {
    null -> "nil"
    is Double -> fmtNum(v)
    is Boolean -> if (v) "true" else "false"
    is String -> if (top) v else quote(v)
    is RinArray -> v.items.joinToString(", ", "[", "]") { display(it, false) }
    is RinMap -> (0 until v.keys.size).joinToString(", ", "{", "}") { display(v.keys[it], false) + ": " + display(v.vals[it], false) }
    else -> v.toString()
}

fun valuesEqual(a: Any?, b: Any?): Boolean {
    if (a == null || b == null) return a == null && b == null
    return when (a) {
        is Double -> b is Double && a.toDouble() == b.toDouble()
        is String -> b is String && a == b
        is Boolean -> b is Boolean && a == b
        is RinArray -> b is RinArray && (a === b || (a.items.size == b.items.size && a.items.indices.all { valuesEqual(a.items[it], b.items[it]) }))
        is RinMap -> b is RinMap && (a === b || (a.keys.size == b.keys.size && a.keys.indices.all { i ->
            b.keys.indices.any { j -> valuesEqual(a.keys[i], b.keys[j]) && valuesEqual(a.vals[i], b.vals[j]) } }))
        else -> a === b
    }
}

// ------------------------------------------------------------------ نصوص بايتية (UTF-8)
private fun isAscii(s: String): Boolean { for (c in s) if (c.code >= 0x80) return false; return true }
private fun u8(s: String): ByteArray = s.toByteArray(Charsets.UTF_8)
private fun fromU8(b: ByteArray, off: Int = 0, len: Int = b.size - off): String = String(b, off, len, Charsets.UTF_8)
private fun byteLen(s: String): Int = if (isAscii(s)) s.length else u8(s).size

// ------------------------------------------------------------------ بناء القيم
@JvmField val NIL_UNUSED: Any? = null
fun num(d: Double): Any? = d
fun mkArr0(): Any? = RinArray()
fun mkArr(a: Array<Any?>): Any? = RinArray(ArrayList<Any?>(a.size).also { for (x in a) it.add(x) })
fun mkMap0(): Any? = RinMap()
fun mkMap(a: Array<Any?>): Any? { val m = RinMap(); var i = 0; while (i < a.size) { m.put(a[i], a[i + 1]); i += 2 }; return m }

fun truthy(v: Any?): Boolean = when (v) { null -> false; is Boolean -> v; is Double -> v != 0.0; else -> true }

fun g(v: Any?, name: String): Any? { if (v === UNDEF) err("E0001", "undefined variable `$name`"); return v }
fun undefinedVar(name: String): Any? = err("E0001", "undefined variable `$name`")
fun undefinedFn(name: String): Any? = err("E0006", "`$name` is not a function")
fun arityError(name: String, got: Int, expected: Int): Any? {
    if (expected < 0) err("E0007", "invalid number of arguments for `$name`")
    when (name) {
        "substr" -> err("E0007", "'substr' expects 2 or 3 argument(s) but got $got")
        "min", "max" -> err("E0007", "'$name' expects at least 1 argument")
    }
    err("E0007", "'$name' expects $expected argument(s) but got $got")
}

fun print(items: Array<Any?>) {
    val sb = StringBuilder()
    for (i in items.indices) { if (i > 0) sb.append(' '); sb.append(display(items[i])) }
    sb.append('\n')
    val o = out
    if (o != null) o(sb.toString()) else System.out.print(sb)
}

// ------------------------------------------------------------------ المعاملات
private fun needNums(a: Any?, b: Any?, op: String) {
    if (a !is Double || b !is Double) err("E0004", "operator `$op` requires two numbers, but found a `${typeName(if (a !is Double) a else b)}`")
}

fun add(a: Any?, b: Any?): Any? {
    if (a is Double && b is Double) return a + b
    if (a is String || b is String) return display(a) + display(b)
    err("E0004", "`+` operands must be numbers or strings, found `${typeName(a)}` and `${typeName(b)}`")
}
fun sub(a: Any?, b: Any?): Any? { needNums(a, b, "-"); return (a as Double) - (b as Double) }
fun mul(a: Any?, b: Any?): Any? { needNums(a, b, "*"); return (a as Double) * (b as Double) }
fun div(a: Any?, b: Any?): Any? { needNums(a, b, "/"); if (b as Double == 0.0) err("E0035", "division by zero"); return (a as Double) / b }
fun mod(a: Any?, b: Any?): Any? { needNums(a, b, "%"); if (b as Double == 0.0) err("E0035", "division by zero"); return (a as Double).IEEErem0(b) }
private fun Double.IEEErem0(b: Double): Double = this % b // % في JVM = fmod في C
fun lt(a: Any?, b: Any?): Any? { needNums(a, b, "<"); return (a as Double) < (b as Double) }
fun le(a: Any?, b: Any?): Any? { needNums(a, b, "<="); return (a as Double) <= (b as Double) }
fun gt(a: Any?, b: Any?): Any? { needNums(a, b, ">"); return (a as Double) > (b as Double) }
fun ge(a: Any?, b: Any?): Any? { needNums(a, b, ">="); return (a as Double) >= (b as Double) }
fun eq(a: Any?, b: Any?): Any? = valuesEqual(a, b)
fun ne(a: Any?, b: Any?): Any? = !valuesEqual(a, b)
fun neg(a: Any?): Any? { if (a !is Double) err("E0004", "unary `-` operand must be a number, found `${typeName(a)}`"); return -a }
fun not(a: Any?): Any? = !truthy(a)

// ------------------------------------------------------------------ الفهرسة
fun index(o: Any?, i: Any?): Any? {
    when (o) {
        is RinArray -> {
            if (i !is Double) err("E0021", "array index must be a number")
            val k = i.toLong()
            if (k < 0 || k >= o.items.size) err("E0021", "index out of range: $k")
            return o.items[k.toInt()]
        }
        is RinMap -> { val k = o.find(i); return if (k >= 0) o.vals[k] else null }
        is String -> {
            if (i !is Double) err("E0021", "string index must be a number")
            val k = i.toLong()
            if (isAscii(o)) { if (k < 0 || k >= o.length) err("E0021", "index out of range: $k"); return o[k.toInt()].toString() }
            val b = u8(o)
            if (k < 0 || k >= b.size) err("E0021", "index out of range: $k")
            return fromU8(b, k.toInt(), 1)
        }
    }
    err("E0004", "cannot index a value of type `${typeName(o)}`")
}

fun indexSet(o: Any?, i: Any?, v: Any?): Any? {
    when (o) {
        is RinArray -> {
            if (i !is Double) err("E0021", "array index must be a number")
            val k = i.toLong()
            if (k < 0 || k >= o.items.size) err("E0021", "index out of range: $k")
            o.items[k.toInt()] = v; return v
        }
        is RinMap -> { o.put(i, v); return v }
    }
    err("E0004", "cannot assign by index into a value of type `${typeName(o)}`")
}

// ------------------------------------------------------------------ الدوال الأصلية
private fun expStr(v: Any?, fn: String): String { if (v !is String) err("E0004", "'$fn' expects a string but got ${typeName(v)}"); return v }
private fun expNum(v: Any?, fn: String): Double { if (v !is Double) err("E0004", "'$fn' expects a number but got ${typeName(v)}"); return v }
private fun numArr(v: Any?, fn: String): DoubleArray {
    if (v !is RinArray) err("E0004", "'$fn' expects an array of numbers but got ${typeName(v)}")
    val out = DoubleArray(v.items.size)
    for (i in out.indices) { val x = v.items[i]; if (x !is Double) err("E0004", "'$fn' expects an array of numbers, found a ${typeName(x)} element"); out[i] = x }
    if (out.isEmpty()) err("E0004", "'$fn' لا يقبل مصفوفة فارغة (empty array)")
    return out
}

fun n_len(a: Any?): Any? = when (a) {
    is String -> byteLen(a).toDouble(); is RinArray -> a.items.size.toDouble(); is RinMap -> a.keys.size.toDouble()
    else -> err("E0004", "'len' expects a string, array or map but got ${typeName(a)}")
}
private fun asciiMap(s: String, up: Boolean): String {
    val sb = StringBuilder(s.length)
    for (c in s) sb.append(if (up && c in 'a'..'z') (c - 32) else if (!up && c in 'A'..'Z') (c + 32) else c)
    return sb.toString()
}
fun n_upper(a: Any?): Any? = asciiMap(expStr(a, "upper"), true)
fun n_lower(a: Any?): Any? = asciiMap(expStr(a, "lower"), false)
fun n_trim(a: Any?): Any? = expStr(a, "trim").trim { it == ' ' || it == '\t' || it == '\r' || it == '\n' }
fun n_substr(s: Any?, st: Any?): Any? = substr(s, st, null, false)
fun n_substr(s: Any?, st: Any?, ln: Any?): Any? = substr(s, st, ln, true)
private fun substr(sa: Any?, sta: Any?, lna: Any?, has: Boolean): Any? {
    val s = expStr(sa, "substr")
    var start = expNum(sta, "substr").toLong()
    val b = if (isAscii(s)) null else u8(s)
    val n = b?.size ?: s.length
    if (start < 0) start = 0
    if (start > n) start = n.toLong()
    var len = if (has) expNum(lna, "substr").toLong() else n - start
    if (len < 0) len = 0
    if (len > n - start) len = n - start
    return if (b == null) s.substring(start.toInt(), (start + len).toInt()) else fromU8(b, start.toInt(), len.toInt())
}
fun n_split(a: Any?, b: Any?): Any? {
    val s = expStr(a, "split"); val sep = expStr(b, "split")
    val r = RinArray()
    if (sep.isEmpty()) { if (isAscii(s)) for (c in s) r.items.add(c.toString()) else { val bb = u8(s); for (i in bb.indices) r.items.add(fromU8(bb, i, 1)) } }
    else { var pos = 0; while (true) { val nx = s.indexOf(sep, pos); if (nx < 0) { r.items.add(s.substring(pos)); break }; r.items.add(s.substring(pos, nx)); pos = nx + sep.length } }
    return r
}
fun n_join(a: Any?, b: Any?): Any? {
    if (a !is RinArray) err("E0004", "'join' expects an array as the first argument")
    val sep = expStr(b, "join")
    return a.items.joinToString(sep) { display(it) }
}
fun n_indexOf(a: Any?, b: Any?): Any? {
    val s = expStr(a, "indexOf"); val sub = expStr(b, "indexOf")
    val i = s.indexOf(sub)
    if (i < 0) return -1.0
    return if (isAscii(s)) i.toDouble() else byteLen(s.substring(0, i)).toDouble()
}
fun n_replace(a: Any?, b: Any?, c: Any?): Any? {
    val s = expStr(a, "replace"); val f = expStr(b, "replace"); val t = expStr(c, "replace")
    return if (f.isEmpty()) s else s.replace(f, t)
}
fun n_contains(a: Any?, b: Any?): Any? {
    if (a is String) return a.contains(expStr(b, "contains"))
    if (a is RinArray) return a.items.any { valuesEqual(it, b) }
    err("E0004", "'contains' expects a string or array as the first argument")
}
fun n_charAt(a: Any?, b: Any?): Any? {
    val s = expStr(a, "charAt"); val i = expNum(b, "charAt").toLong()
    if (isAscii(s)) { if (i < 0 || i >= s.length) err("E0035", "'charAt': index out of range"); return s[i.toInt()].toString() }
    val bb = u8(s); if (i < 0 || i >= bb.size) err("E0035", "'charAt': index out of range"); return fromU8(bb, i.toInt(), 1)
}
fun n_toString(a: Any?): Any? = display(a)
fun n_str(a: Any?): Any? = display(a)
fun n_toNumber(a: Any?): Any? {
    if (a is Double) return a
    val s = expStr(a, "toNumber")
    // std::stod: يتجاهل المسافات البادئة ويقرأ أطول بادئة رقمية صالحة
    val m = Regex("^\\s*[+-]?(?:(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?|[iI][nN][fF](?:[iI][nN][iI][tT][yY])?|[nN][aA][nN])").find(s)
        ?: err("E0035", "'toNumber': \"$s\" ليست رقماً صالحاً")
    val t = m.value.trim().lowercase()
    return when { t.endsWith("inf") || t.endsWith("infinity") -> if (t.startsWith("-")) Double.NEGATIVE_INFINITY else Double.POSITIVE_INFINITY
        t.endsWith("nan") -> Double.NaN; else -> t.toDouble() }
}
fun n_toBool(a: Any?): Any? = when (a) {
    is Boolean -> a; is Double -> a != 0.0; null -> false
    is String -> when (asciiMap(a, false)) { "true" -> true; "false" -> false; else -> err("E0035", "'toBool': \"$a\" ليست true/false صالحة") }
    else -> err("E0035", "'toBool' لا يدعم تحويل نوع ${typeName(a)} إلى Boolean")
}
fun n_isBool(a: Any?): Any? = a is Boolean
fun n_abs(a: Any?): Any? = Math.abs(expNum(a, "abs"))
fun n_sqrt(a: Any?): Any? { val n = expNum(a, "sqrt"); if (n < 0) err("E0004", "'sqrt' لا يقبل عدداً سالباً"); return Math.sqrt(n) }
fun n_pow(a: Any?, b: Any?): Any? = Math.pow(expNum(a, "pow"), expNum(b, "pow"))
fun n_floor(a: Any?): Any? = Math.floor(expNum(a, "floor"))
fun n_ceil(a: Any?): Any? = Math.ceil(expNum(a, "ceil"))
fun n_round(a: Any?): Any? { val d = expNum(a, "round"); return if (d.isNaN() || d.isInfinite()) d else if (d < 0) -Math.floor(-d + 0.5) else Math.floor(d + 0.5) }

private fun extremum(fn: String, items: List<Any?>, wantMax: Boolean): Any? {
    if (items.isEmpty()) return null
    val t0 = items[0]
    if (t0 !is Double && t0 !is String) err("E0004", "'$fn' expects numbers or strings but got ${typeName(t0)}")
    var best = 0
    for (i in 1 until items.size) {
        val x = items[i]
        if ((x is Double) != (t0 is Double) || (x is String) != (t0 is String)) err("E0004", "'$fn' cannot compare a ${typeName(t0)} with a ${typeName(x)}")
        val better = if (t0 is Double) (if (wantMax) (x as Double) > (items[best] as Double) else (x as Double) < (items[best] as Double))
        else { val c = (x as String).compareTo(items[best] as String); if (wantMax) c > 0 else c < 0 }
        if (better) best = i
    }
    return items[best]
}
private fun ext1(fn: String, a: Any?, mx: Boolean): Any? {
    if (a !is RinArray) err("E0004", "'$fn' with one argument expects an array but got ${typeName(a)}")
    return extremum(fn, a.items, mx)
}
fun n_min(a: Any?): Any? = ext1("min", a, false)
fun n_min(a: Any?, b: Any?): Any? = extremum("min", listOf(a, b), false)
fun n_min(a: Any?, b: Any?, c: Any?): Any? = extremum("min", listOf(a, b, c), false)
fun n_min(a: Any?, b: Any?, c: Any?, d: Any?): Any? = extremum("min", listOf(a, b, c, d), false)
fun n_min(a: Any?, b: Any?, c: Any?, d: Any?, e: Any?): Any? = extremum("min", listOf(a, b, c, d, e), false)
fun n_min(a: Any?, b: Any?, c: Any?, d: Any?, e: Any?, f: Any?): Any? = extremum("min", listOf(a, b, c, d, e, f), false)
fun n_max(a: Any?): Any? = ext1("max", a, true)
fun n_max(a: Any?, b: Any?): Any? = extremum("max", listOf(a, b), true)
fun n_max(a: Any?, b: Any?, c: Any?): Any? = extremum("max", listOf(a, b, c), true)
fun n_max(a: Any?, b: Any?, c: Any?, d: Any?): Any? = extremum("max", listOf(a, b, c, d), true)
fun n_max(a: Any?, b: Any?, c: Any?, d: Any?, e: Any?): Any? = extremum("max", listOf(a, b, c, d, e), true)
fun n_max(a: Any?, b: Any?, c: Any?, d: Any?, e: Any?, f: Any?): Any? = extremum("max", listOf(a, b, c, d, e, f), true)

fun n_push(a: Any?, v: Any?): Any? { if (a !is RinArray) err("E0004", "'push' expects an array"); a.items.add(v); return a.items.size.toDouble() }
fun n_pop(a: Any?): Any? {
    if (a !is RinArray) err("E0004", "'pop' expects an array")
    if (a.items.isEmpty()) err("E0004", "'pop': المصفوفة فارغة")
    return a.items.removeAt(a.items.size - 1)
}
fun n_sort(a: Any?): Any? {
    if (a !is RinArray) err("E0004", "'sort' expects an array")
    val it = a.items
    if (it.size > 1) {
        val allNum = it.all { x -> x is Double }; val allStr = it.all { x -> x is String }
        if (!allNum && !allStr) err("E0035", "'sort' يتطلب أن تكون كل العناصر أرقاماً أو نصوصاً فقط")
        if (allNum) it.sortWith { x, y -> (x as Double).compareTo(y as Double) } else it.sortWith { x, y -> (x as String).compareTo(y as String) }
    }
    return a
}
fun n_keys(a: Any?): Any? { if (a !is RinMap) err("E0004", "'keys' expects a map"); return RinArray(ArrayList(a.keys)) }
fun n_values(a: Any?): Any? { if (a !is RinMap) err("E0004", "'values' expects a map"); return RinArray(ArrayList(a.vals)) }
fun n_has(a: Any?, n: Any?): Any? = when {
    a is RinMap -> a.find(n) >= 0
    a is RinArray -> a.items.any { valuesEqual(it, n) }
    a is String && n is String -> a.contains(n)
    else -> err("E0004", "'has' يتطلب map أو array، أو string مع string")
}
fun n_remove(a: Any?, k: Any?): Any? {
    if (a !is RinMap) err("E0004", "'remove' expects a map")
    val i = a.find(k); if (i < 0) return false
    a.keys.removeAt(i); a.vals.removeAt(i); return true
}
fun n_sum(a: Any?): Any? { var t = 0.0; for (x in numArr(a, "sum")) t += x; return t }
fun n_mean(a: Any?): Any? { val n = numArr(a, "mean"); var t = 0.0; for (x in n) t += x; return t / n.size }
fun n_maxOf(a: Any?): Any? { val n = numArr(a, "maxOf"); var m = n[0]; for (x in n) if (x > m) m = x; return m }
fun n_minOf(a: Any?): Any? { val n = numArr(a, "minOf"); var m = n[0]; for (x in n) if (x < m) m = x; return m }
fun n_chr(a: Any?): Any? {
    val i = expNum(a, "chr").toLong()
    if (i < 0 || i > 255) err("E0004", "'chr': القيمة يجب أن تكون بين 0 و255 (تلقّت $i)")
    return fromU8(byteArrayOf(i.toByte()))
}
fun n_ord(a: Any?): Any? {
    val s = expStr(a, "ord"); if (s.isEmpty()) err("E0004", "'ord': النص فارغ")
    return (u8(s)[0].toInt() and 0xff).toDouble()
}
