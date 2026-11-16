package com.dlof.rinlang.apk.dex

/**
 * الواجهة الأمامية لمُصرِّف Rin → DEX: محلّل لغوي + نحوي لنطاق اللغة الإجرائي الأساسي (نفس نطاق
 * `compiler/rinc.cpp` بدلالات المفسّر الأصلي). كل ما خرج عن هذا النطاق يُرمى كـ [DexUnsupported]
 * (أو [DexParseFailure] إن لم يُفهَم النص أصلاً) فيتراجع المُصدِّر إلى وضع المفسّر المُضمَّن بدل
 * إنتاج dex بدلالة مختلفة بصمت. لا تُعدّ أخطاء هذا المحلّل أخطاء مستخدم: المرجع الوحيد لصحّة
 * الشيفرة هو المفسّر الأصلي نفسه (قد يقبل نحواً أوسع من هذا المحلّل).
 */

/** ميزة صالحة في Rin لكنها خارج نطاق التصريف إلى DEX. */
open class DexUnsupported(message: String, val line: Int) : RuntimeException(message)

/** تعذّر على هذا المحلّل فهم النص (قد يكون نحواً أوسع مما يدعمه، أو خطأ حقيقياً). */
class DexParseFailure(message: String, line: Int) : DexUnsupported(message, line)

enum class T {
    NUMBER, STRING, IDENT,
    LET, PRINT, IF, ELSE, WHILE, FOR, FUN, RETURN, TRUE, FALSE, NIL, AND, OR,
    BREAK, CONTINUE, RINOPEN,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQUAL, EQUAL_EQUAL, BANG, BANG_EQUAL,
    LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COLON, COMMA, SEMICOLON, DOT, PIPE,
    EOF
}

class Token(val type: T, val lexeme: String, val line: Int, val number: Double = 0.0)

class DexLexer(private val src: String) {
    private val tokens = ArrayList<Token>()
    private var start = 0
    private var cur = 0
    private var line = 1

    fun scan(): List<Token> {
        while (!atEnd()) { start = cur; scanToken() }
        tokens.add(Token(T.EOF, "", line))
        return tokens
    }

    private fun atEnd() = cur >= src.length
    private fun adv() = src[cur++]
    private fun peek(): Char = if (atEnd()) '\u0000' else src[cur]
    private fun peekNext(): Char = if (cur + 1 >= src.length) '\u0000' else src[cur + 1]
    private fun match(c: Char): Boolean { if (atEnd() || src[cur] != c) return false; cur++; return true }
    private fun add(t: T) { tokens.add(Token(t, src.substring(start, cur), line)) }
    private fun isDigit(c: Char) = c in '0'..'9'
    private fun isAlpha(c: Char) = c in 'a'..'z' || c in 'A'..'Z' || c == '_'

    private fun scanString() {
        val sb = StringBuilder()
        while (peek() != '"' && !atEnd()) {
            val c = peek()
            if (c == '\n') { line++; sb.append(adv()); continue }
            if (c == '\\') {
                adv()
                if (atEnd()) break
                when (val e = adv()) {
                    'n' -> sb.append('\n'); 't' -> sb.append('\t'); 'r' -> sb.append('\r')
                    '"' -> sb.append('"'); '\\' -> sb.append('\\')
                    '\n' -> line++
                    else -> sb.append(e)
                }
                continue
            }
            sb.append(adv())
        }
        if (atEnd()) throw DexParseFailure("نص غير منتهٍ", line)
        adv()
        tokens.add(Token(T.STRING, sb.toString(), line))
    }

    private fun scanNumber() {
        while (isDigit(peek())) adv()
        if (peek() == '.' && isDigit(peekNext())) { adv(); while (isDigit(peek())) adv() }
        // 1e3 / 0x10 / 1_000 … صياغات يقبلها المفسّر وليست في نطاق هذا المحلّل: لا نقرأها كأرقام ناقصة.
        val nx = peek()
        if (isAlpha(nx)) throw DexParseFailure("صيغة رقم غير مدعومة في مصرِّف DEX", line)
        val text = src.substring(start, cur)
        tokens.add(Token(T.NUMBER, text, line, text.toDouble()))
    }

    private val keywords = mapOf(
        "let" to T.LET, "print" to T.PRINT, "if" to T.IF, "else" to T.ELSE, "while" to T.WHILE,
        "for" to T.FOR, "fun" to T.FUN, "return" to T.RETURN, "true" to T.TRUE, "false" to T.FALSE,
        "nil" to T.NIL, "and" to T.AND, "or" to T.OR, "break" to T.BREAK, "continue" to T.CONTINUE,
        "rinopen" to T.RINOPEN
    )

    private fun scanIdent() {
        while (isAlpha(peek()) || isDigit(peek())) adv()
        val text = src.substring(start, cur)
        tokens.add(Token(keywords[text] ?: T.IDENT, text, line))
    }

    private fun scanToken() {
        val c = adv()
        when (c) {
            '(' -> add(T.LPAREN); ')' -> add(T.RPAREN); '{' -> add(T.LBRACE); '}' -> add(T.RBRACE)
            '[' -> add(T.LBRACKET); ']' -> add(T.RBRACKET); ':' -> add(T.COLON); ',' -> add(T.COMMA)
            ';' -> add(T.SEMICOLON); '+' -> add(T.PLUS); '-' -> add(T.MINUS); '*' -> add(T.STAR)
            '%' -> add(T.PERCENT); '.' -> add(T.DOT)
            '|' -> if (match('>')) add(T.PIPE) else throw DexParseFailure("'|' غير متوقعة", line)
            '/' -> if (match('/')) { while (peek() != '\n' && !atEnd()) adv() } else add(T.SLASH)
            '=' -> add(if (match('=')) T.EQUAL_EQUAL else T.EQUAL)
            '!' -> add(if (match('=')) T.BANG_EQUAL else T.BANG)
            '<' -> add(if (match('=')) T.LESS_EQUAL else T.LESS)
            '>' -> add(if (match('=')) T.GREATER_EQUAL else T.GREATER)
            ' ', '\r', '\t' -> {}
            '\n' -> line++
            '"' -> scanString()
            else -> when {
                isDigit(c) -> scanNumber()
                isAlpha(c) -> scanIdent()
                else -> throw DexParseFailure("محرف غير مدعوم '$c' في مصرِّف DEX", line)
            }
        }
    }
}

// ================================================================= AST
sealed class Expr { var line = 0 }
class NumberE(val value: Double) : Expr()
class StringE(val value: String) : Expr()
class BoolE(val value: Boolean) : Expr()
class NilE : Expr()
class VarE(val name: String) : Expr()
class AssignE(val name: String, val value: Expr) : Expr()
class BinaryE(val left: Expr, val op: T, val right: Expr) : Expr()
class LogicalE(val left: Expr, val op: T, val right: Expr) : Expr()
class UnaryE(val op: T, val right: Expr) : Expr()
class CallE(val callee: String, val args: List<Expr>) : Expr()
class ArrayE(val elements: List<Expr>) : Expr()
class MapE(val keys: List<Expr>, val values: List<Expr>) : Expr()
class IndexE(val obj: Expr, val index: Expr) : Expr()
class IndexSetE(val obj: Expr, val index: Expr, val value: Expr) : Expr()

sealed class Stmt { var line = 0 }
class ExprS(val expr: Expr) : Stmt()
class PrintS(val exprs: List<Expr>) : Stmt()
class LetS(val name: String, val init: Expr?) : Stmt()
class BlockS(val stmts: List<Stmt>) : Stmt()
class IfS(val cond: Expr, val thenB: Stmt, val elseB: Stmt?) : Stmt()
class WhileS(val cond: Expr, val body: Stmt) : Stmt()
class ForS(val init: Stmt?, val cond: Expr?, val incr: Expr?, val body: Stmt) : Stmt()
class FunS(val name: String, val params: List<String>, val body: BlockS) : Stmt()
class ReturnS(val value: Expr?) : Stmt()
class BreakS : Stmt()
class ContinueS : Stmt()

// ================================================================= المحلّل النحوي
class DexParser(private val toks: List<Token>) {
    private var cur = 0
    private var loopDepth = 0

    fun parse(): List<Stmt> {
        val out = ArrayList<Stmt>()
        while (!atEnd()) out.add(declaration())
        return out
    }

    private fun atEnd() = peek().type == T.EOF
    private fun peek() = toks[cur]
    private fun prev() = toks[cur - 1]
    private fun adv(): Token { if (!atEnd()) cur++; return prev() }
    private fun check(t: T) = !atEnd() && peek().type == t
    private fun checkNext(t: T) = cur + 1 < toks.size && toks[cur + 1].type == t
    private fun match(vararg ts: T): Boolean { for (t in ts) if (check(t)) { adv(); return true }; return false }
    private fun consume(t: T, msg: String): Token {
        if (check(t)) return adv()
        throw DexParseFailure("$msg (وُجد '${peek().lexeme}')", peek().line)
    }
    private fun identIs(s: String) = check(T.IDENT) && peek().lexeme == s

    /** كلمات سياقية/محجوزة في المفسّر لها نحو خاص لا يدعمه هذا المحلّل. */
    private val unsupportedWords = setOf(
        "text", "container", "Containers", "Group", "Volume", "Section", "Translations", "translation",
        "link", "tying", "merge", "installation", "simplified", "save", "file", "route", "row", "style",
        "document", "use", "from", "class", "enum", "each", "repeat", "loop", "match", "try", "catch",
        "import", "export", "interface", "struct", "async", "await", "throw", "goal", "achieve", "retry"
    )

    private fun declaration(): Stmt {
        if (identIs("plus") && checkNext(T.DOT)) throw DexUnsupported("plus.condition", peek().line)
        if (check(T.IDENT) && peek().lexeme in unsupportedWords) {
            val nxt = toks[cur + 1].type
            // اسم متغيّر عادي (save = 5; / save(x); / save[0] = ...) يبقى مقبولاً؛ أما ما يشبه عبارة حاوية فلا.
            if (nxt == T.IDENT || nxt == T.STRING || nxt == T.SEMICOLON || nxt == T.LBRACE || nxt == T.LESS || nxt == T.COLON)
                throw DexUnsupported("الكلمة '${peek().lexeme}' من نحو لا يدعمه مصرِّف DEX", peek().line)
        }
        if (match(T.LET)) return letDecl()
        if (match(T.FUN)) return funDecl()
        return statement()
    }

    private fun letDecl(): Stmt {
        val name = consume(T.IDENT, "اسم المتغيّر بعد let")
        var init: Expr? = null
        if (match(T.EQUAL)) init = expression()
        consume(T.SEMICOLON, "؛ بعد التصريح")
        return LetS(name.lexeme, init).also { it.line = name.line }
    }

    private fun funDecl(): Stmt {
        val name = consume(T.IDENT, "اسم الدالة")
        consume(T.LPAREN, "( بعد اسم الدالة")
        val params = ArrayList<String>()
        if (!check(T.RPAREN)) {
            do { params.add(consume(T.IDENT, "اسم معامل").lexeme) } while (match(T.COMMA))
        }
        consume(T.RPAREN, ") بعد المعاملات")
        consume(T.LBRACE, "{ قبل متن الدالة")
        val saved = loopDepth; loopDepth = 0
        val body = block()
        loopDepth = saved
        return FunS(name.lexeme, params, body).also { it.line = name.line }
    }

    private fun statement(): Stmt {
        if (match(T.PRINT)) return printStmt()
        if (match(T.IF)) return ifStmt()
        if (identIs("when") && checkNext(T.LPAREN)) { adv(); return whenStmt() }
        if (match(T.WHILE)) return whileStmt()
        if (match(T.RINOPEN)) return rinopenStmt()
        if (match(T.FOR)) return forStmt()
        if (match(T.RETURN)) return returnStmt()
        if (match(T.BREAK)) {
            val k = prev(); if (loopDepth == 0) throw DexParseFailure("break خارج حلقة", k.line)
            consume(T.SEMICOLON, "؛ بعد break"); return BreakS().also { it.line = k.line }
        }
        if (match(T.CONTINUE)) {
            val k = prev(); if (loopDepth == 0) throw DexParseFailure("continue خارج حلقة", k.line)
            consume(T.SEMICOLON, "؛ بعد continue"); return ContinueS().also { it.line = k.line }
        }
        if (check(T.LBRACE)) { adv(); return block() }
        return exprStmt()
    }

    private fun printStmt(): Stmt {
        val tok = prev()
        val exprs = ArrayList<Expr>()
        exprs.add(expression())
        while (match(T.COMMA)) exprs.add(expression())
        // سمات print (sep= / end= / pretty= …) لها دلالة خاصة في المفسّر — خارج النطاق عمداً.
        if (check(T.IDENT) && checkNext(T.EQUAL)) throw DexUnsupported("سمات print (${peek().lexeme}=…)", peek().line)
        if (exprs.any { it is AssignE }) throw DexUnsupported("إسناد داخل print (سمة؟)", tok.line)
        consume(T.SEMICOLON, "؛ بعد print")
        return PrintS(exprs).also { it.line = tok.line }
    }

    private fun ifStmt(): Stmt {
        consume(T.LPAREN, "( بعد if")
        val c = expression()
        consume(T.RPAREN, ") بعد شرط if")
        val t = statement()
        var e: Stmt? = null
        if (match(T.ELSE)) e = statement()
        return IfS(c, t, e).also { it.line = c.line }
    }

    private fun whenStmt(): Stmt {
        consume(T.LPAREN, "( بعد when")
        val c = expression()
        consume(T.RPAREN, ") بعد شرط when")
        val t = statement()
        var e: Stmt? = null
        if (identIs("otherwise")) { adv(); e = statement() }
        return IfS(c, t, e).also { it.line = c.line }
    }

    private fun whileStmt(): Stmt {
        consume(T.LPAREN, "( بعد while")
        val c = expression()
        consume(T.RPAREN, ") بعد شرط while")
        loopDepth++; val b = statement(); loopDepth--
        return WhileS(c, b).also { it.line = c.line }
    }

    private fun rinopenStmt(): Stmt {
        val tok = prev()
        consume(T.LPAREN, "( بعد rinopen")
        val c = expression()
        consume(T.RPAREN, ") بعد شرط rinopen")
        consume(T.LBRACE, "{ قبل متن rinopen")
        loopDepth++; val b = block(); loopDepth--
        return WhileS(c, b).also { it.line = tok.line }
    }

    private fun forStmt(): Stmt {
        val ftok = prev()
        consume(T.LPAREN, "( بعد for")
        val init: Stmt? = when {
            match(T.SEMICOLON) -> null
            match(T.LET) -> letDecl()
            else -> exprStmt()
        }
        val cond = if (!check(T.SEMICOLON)) expression() else null
        consume(T.SEMICOLON, "؛ بعد شرط for")
        val incr = if (!check(T.RPAREN)) expression() else null
        consume(T.RPAREN, ") بعد عبارات for")
        loopDepth++; val body = statement(); loopDepth--
        return ForS(init, cond, incr, body).also { it.line = ftok.line }
    }

    private fun returnStmt(): Stmt {
        val k = prev()
        val v = if (!check(T.SEMICOLON)) expression() else null
        consume(T.SEMICOLON, "؛ بعد return")
        return ReturnS(v).also { it.line = k.line }
    }

    private fun block(): BlockS {
        val out = ArrayList<Stmt>()
        while (!check(T.RBRACE) && !atEnd()) out.add(declaration())
        consume(T.RBRACE, "} بعد الكتلة")
        return BlockS(out)
    }

    private fun exprStmt(): Stmt {
        val e = expression()
        consume(T.SEMICOLON, "؛ بعد التعبير")
        return ExprS(e).also { it.line = e.line }
    }

    // ---------------- تعابير (نفس تسلسل أسبقيات المفسّر الأصلي) ----------------
    private fun <E : Expr> E.at(line: Int): E { this.line = line; return this }

    private fun expression(): Expr = assignment()

    private fun assignment(): Expr {
        val e = pipeline()
        if (match(T.EQUAL)) {
            val eq = prev()
            val v = assignment()
            if (e is VarE) return AssignE(e.name, v).at(eq.line)
            if (e is IndexE) return IndexSetE(e.obj, e.index, v).at(eq.line)
            throw DexParseFailure("هدف إسناد غير صالح", eq.line)
        }
        return e
    }

    private fun pipeline(): Expr {
        var e = logicOr()
        while (match(T.PIPE)) {
            val op = prev()
            val rhs = logicOr()
            val call = when (rhs) {
                is CallE -> rhs
                is VarE -> CallE(rhs.name, emptyList()).at(rhs.line)
                else -> throw DexParseFailure("متوقع نداء دالة بعد |>", op.line)
            }
            e = CallE(call.callee, listOf(e) + call.args).at(op.line)
        }
        return e
    }

    private fun logicOr(): Expr {
        var e = logicAnd()
        while (match(T.OR)) { val r = logicAnd(); e = LogicalE(e, T.OR, r).at(e.line) }
        return e
    }

    private fun logicAnd(): Expr {
        var e = equality()
        while (match(T.AND)) { val r = equality(); e = LogicalE(e, T.AND, r).at(e.line) }
        return e
    }

    private fun equality(): Expr {
        var e = comparison()
        while (match(T.EQUAL_EQUAL, T.BANG_EQUAL)) { val op = prev().type; val r = comparison(); e = BinaryE(e, op, r).at(e.line) }
        return e
    }

    private fun comparison(): Expr {
        var e = term()
        while (match(T.LESS, T.LESS_EQUAL, T.GREATER, T.GREATER_EQUAL)) { val op = prev().type; val r = term(); e = BinaryE(e, op, r).at(e.line) }
        return e
    }

    private fun term(): Expr {
        var e = factor()
        while (match(T.PLUS, T.MINUS)) { val op = prev().type; val r = factor(); e = BinaryE(e, op, r).at(e.line) }
        return e
    }

    private fun factor(): Expr {
        var e = unary()
        while (match(T.STAR, T.SLASH, T.PERCENT)) { val op = prev().type; val r = unary(); e = BinaryE(e, op, r).at(e.line) }
        return e
    }

    private fun unary(): Expr {
        if (match(T.BANG, T.MINUS)) { val op = prev().type; val r = unary(); return UnaryE(op, r).at(r.line) }
        return call()
    }

    private fun call(): Expr {
        var e = primary()
        while (true) {
            if (match(T.LPAREN)) {
                val v = e as? VarE ?: throw DexParseFailure("يمكن نداء الدوال بأسمائها فقط", prev().line)
                val args = ArrayList<Expr>()
                if (!check(T.RPAREN)) { do { args.add(expression()) } while (match(T.COMMA)) }
                consume(T.RPAREN, ") بعد الوسائط")
                e = CallE(v.name, args).at(prev().line)
            } else if (match(T.LBRACKET)) {
                val br = prev()
                val idx = expression()
                consume(T.RBRACKET, "] بعد الفهرس")
                e = IndexE(e, idx).at(br.line)
            } else break
        }
        return e
    }

    private fun primary(): Expr {
        if (match(T.FALSE)) return BoolE(false).at(prev().line)
        if (match(T.TRUE)) return BoolE(true).at(prev().line)
        if (match(T.NIL)) return NilE().at(prev().line)
        if (match(T.NUMBER)) return NumberE(prev().number).at(prev().line)
        if (match(T.STRING)) return StringE(prev().lexeme).at(prev().line)
        if (match(T.IDENT)) return VarE(prev().lexeme).at(prev().line)
        if (match(T.LPAREN)) {
            val e = expression()
            consume(T.RPAREN, ") بعد التعبير")
            return e
        }
        if (match(T.LBRACKET)) {
            val st = prev()
            val els = ArrayList<Expr>()
            if (!check(T.RBRACKET)) { do { els.add(expression()) } while (match(T.COMMA)) }
            consume(T.RBRACKET, "] بعد عناصر المصفوفة")
            return ArrayE(els).at(st.line)
        }
        if (match(T.LBRACE)) {
            val st = prev()
            val ks = ArrayList<Expr>(); val vs = ArrayList<Expr>()
            if (!check(T.RBRACE)) {
                do {
                    if (check(T.RBRACE)) break
                    val key: Expr = when {
                        check(T.STRING) -> StringE(adv().lexeme).at(prev().line)
                        check(T.IDENT) -> StringE(adv().lexeme).at(prev().line)
                        else -> throw DexParseFailure("مفتاح غير صالح في القاموس", peek().line)
                    }
                    consume(T.COLON, ": بعد المفتاح")
                    ks.add(key); vs.add(expression())
                } while (match(T.COMMA))
            }
            consume(T.RBRACE, "} بعد عناصر القاموس")
            return MapE(ks, vs).at(st.line)
        }
        throw DexParseFailure("متوقع تعبير (وُجد '${peek().lexeme}')", peek().line)
    }
}
