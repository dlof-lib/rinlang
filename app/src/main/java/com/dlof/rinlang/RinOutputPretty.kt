package com.dlof.rinlang

import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan

/** تنسيقات عرض لمخرجات Output (JSON مرتّب + تلوين قيمه). */
object RinOutputPretty {

    /** أقل طول لسطر JSON حتى يُنسَّق على عدّة أسطر (الأقصر يبقى سطراً واحداً مقروءاً). */
    private const val MIN_LENGTH = 60
    private const val MAX_LENGTH = 20_000

    private const val C_KEY = 0xFF7FB2FF.toInt()
    private const val C_STRING = 0xFF9AD17B.toInt()
    private const val C_NUMBER = 0xFFE5A05A.toInt()
    private const val C_LITERAL = 0xFFC792EA.toInt()
    private const val C_PUNCT = 0xFF7C8899.toInt()

    // نص بين علامتي تنصيص (مع الهروب) | رقم | true/false/null — يُفحص يساراً ليميناً فلا تُلوَّن أرقام داخل نص.
    private val TOKEN = Regex("\"(?:[^\"\\\\]|\\\\.)*\"?|-?\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?|\\b(?:true|false|null)\\b|[{}\\[\\],:]")

    /** هل يبدو [text] قيمة JSON (يبدأ بـ { أو [)؟ يُستعمل لتقرير التلوين. */
    fun looksLikeJson(text: String): Boolean {
        val t = text.trimStart()
        return t.length >= 2 && (t[0] == '{' || t[0] == '[') && (t.contains('"') || t.contains(':') || t.contains(','))
    }

    /** يلوّن مفاتيح/نصوص/أرقام/ثوابت JSON داخل [sb] ابتداءً من الإزاحة [from] (الجزء المكتوب فقط). */
    fun colorize(sb: SpannableStringBuilder, from: Int, to: Int) {
        val s = sb.substring(from, to)
        for (m in TOKEN.findAll(s)) {
            val tok = m.value
            val color = when {
                tok[0] == '"' -> {
                    val after = s.substring(m.range.last + 1).trimStart()
                    if (after.startsWith(":")) C_KEY else C_STRING
                }
                tok[0].isDigit() || tok[0] == '-' -> C_NUMBER
                tok[0].isLetter() -> C_LITERAL
                else -> C_PUNCT
            }
            sb.setSpan(
                ForegroundColorSpan(color), from + m.range.first, from + m.range.last + 1,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
        }
    }

    /**
     * يعيد [text] منسّقاً بإزاحة سطر لكل عنصر إن كان JSON متوازن الأقواس (`{...}` أو `[...]`) وطويلاً،
     * وإلا null (فيُعرض كما هو). لا يغيّر القيم: يضيف فواصل أسطر ومسافات خارج النصوص فقط.
     */
    fun json(text: String): String? {
        val t = text.trim()
        if (t.length < MIN_LENGTH || t.length > MAX_LENGTH) return null
        val open = t[0]
        val close = t[t.length - 1]
        if (!((open == '{' && close == '}') || (open == '[' && close == ']'))) return null
        if (!balanced(t)) return null

        val out = StringBuilder(t.length + t.length / 4)
        var depth = 0
        var inString = false
        var escaped = false
        fun newline() { out.append('\n'); repeat(depth) { out.append("  ") } }
        var i = 0
        while (i < t.length) {
            val c = t[i]
            if (inString) {
                out.append(c)
                if (escaped) escaped = false
                else if (c == '\\') escaped = true
                else if (c == '"') inString = false
            } else when (c) {
                '"' -> { inString = true; out.append(c) }
                '{', '[' -> {
                    out.append(c)
                    var j = i + 1
                    while (j < t.length && t[j].isWhitespace()) j++
                    if (j < t.length && (t[j] == '}' || t[j] == ']')) {
                        out.append(t[j]) // حاوية فارغة تبقى مدمجة: {} أو []
                        i = j
                    } else {
                        depth++
                        newline()
                    }
                }
                '}', ']' -> { depth--; newline(); out.append(c) }
                ',' -> { out.append(c); newline() }
                ':' -> out.append(": ")
                ' ', '\t', '\n', '\r' -> Unit
                else -> out.append(c)
            }
            i++
        }
        return out.toString()
    }

    private fun balanced(t: String): Boolean {
        var depth = 0
        var inString = false
        var escaped = false
        for (c in t) {
            if (inString) {
                if (escaped) escaped = false
                else if (c == '\\') escaped = true
                else if (c == '"') inString = false
                continue
            }
            when (c) {
                '"' -> inString = true
                '{', '[' -> depth++
                '}', ']' -> { depth--; if (depth < 0) return false }
            }
        }
        return depth == 0 && !inString
    }
}
