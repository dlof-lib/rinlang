package com.dlof.rinlang.store.languages


/**
 * محتوى لغة "Illust" المضمَّن كاملاً كسلاسل Kotlin خام — لغة رسم/SVG جاهزة بُنيت فوق
 * langkit.og.rin بنفس بنية أي "لغة مخصصة" في المستودع (راجع examples/customlang/illust/
 * للنسخة القابلة للقراءة/التعديل مباشرة على القرص، وdocs/custom-languages.md للفكرة العامة).
 *
 * هذه النسخة Kotlin تُستخدم فقط لتضمين اللغة "جاهزة من داخل التطبيق" (زر "مشروع جديد" ←
 * نوع Illust في ProjectsActivity)، عبر [CustomLanguageProjectScaffolder.installBundledIllust]،
 * الذي يكتب هذه الملفات إلى مشروع جديد ويسجّلها في [CustomLanguageRegistry] فوراً — فتُفتح
 * ملفات .illust بتلوين صحيح من أول لحظة، بلا أي خطوة يدوية من المستخدم.
 *
 * كل الملفات هنا اختُبرت فعلياً بتشغيلها على مفسّر Rin حقيقي مبنيّ من مصدر هذا المستودع
 * (Interpreter.rin يُخرج SVG صالحاً، وCodeGen.rin يُخرج كود Rin مستقل ينتج نفس الـSVG).
 */
object BundledIllustLanguage {

    const val LANGUAGE_ID = "illust"
    const val LANGUAGE_NAME = "Illust"
    const val FILE_EXTENSION = "illust"
    const val DEVELOPER = "Rin Team"
    const val DESCRIPTION = "لغة رسم/جرافيكس صغيرة فوق Rin: أوامر نصية (canvas/rect/circle/ellipse/polygon/path/line/text/fill/stroke/group/rotate) مع متغيرات وشروط وحلقات ودوال قابلة لإعادة الاستخدام، تتحول لمخرجات SVG حقيقية."

    val LEXER_RIN: String = """
// ============================================================================
//  Lexer.rin — المرحلة الأولى من لغة "Illust": نص مصدر .illust -> tokens
//  Illust لغة رسم صغيرة: أوامر رسم (canvas/rect/circle/line/text/fill/stroke)
//  + متغيرات وشروط وحلقات، تتحوّل لاحقاً (Interpreter.rin) إلى SVG حقيقي.
// ============================================================================

@import "langkit";

fun isKeyword(word) {
    let keywords = [
        "let", "canvas", "fill", "stroke", "none",
        "rect", "circle", "ellipse", "polygon", "path", "line", "text",
        "if", "else", "while", "group", "rotate", "fun", "return", "print"
    ];
    return contains(keywords, word);
}

fun lex(source) {
    let tokens = [];
    let i = 0;
    let line = 1;
    let n = len(source);

    while (i < n) {
        let ch = charAt(source, i);

        if (isSpaceChar(ch)) {
            i = i + 1;
        } else if (isNewlineChar(ch)) {
            line = line + 1;
            i = i + 1;

        // تعليق سطر واحد //...
        } else if (ch == "/" and i + 1 < n and charAt(source, i + 1) == "/") {
            while (i < n and isNewlineChar(charAt(source, i)) == false) {
                i = i + 1;
            }

        // أرقام: 123 أو 12.5 (تدعم سالب عبر عامل الطرح الأحادي في الـParser، ليس هنا)
        } else if (isDigitChar(ch)) {
            let start = i;
            while (i < n and (isDigitChar(charAt(source, i)) or charAt(source, i) == ".")) {
                i = i + 1;
            }
            push(tokens, makeToken("NUMBER", substr(source, start, i - start), line));

        // معرّفات وكلمات مفتاحية: canvas, fill, myVar ...
        } else if (isAlphaChar(ch)) {
            let start = i;
            while (i < n and isAlnumChar(charAt(source, i))) {
                i = i + 1;
            }
            let word = substr(source, start, i - start);
            if (isKeyword(word)) {
                push(tokens, makeToken("KEYWORD", word, line));
            } else {
                push(tokens, makeToken("IDENT", word, line));
            }

        // نصوص "..." (تُستخدم لألوان hex ولنص text(...))
        } else if (ch == "\"") {
            let start = i + 1;
            i = i + 1;
            while (i < n and charAt(source, i) != "\"") {
                i = i + 1;
            }
            push(tokens, makeToken("STRING", substr(source, start, i - start), line));
            i = i + 1;

        // عوامل مزدوجة قبل المفردة: == != <= >=
        } else if (ch == "=" and i + 1 < n and charAt(source, i + 1) == "=") {
            push(tokens, makeToken("OP", "==", line)); i = i + 2;
        } else if (ch == "!" and i + 1 < n and charAt(source, i + 1) == "=") {
            push(tokens, makeToken("OP", "!=", line)); i = i + 2;
        } else if (ch == "<" and i + 1 < n and charAt(source, i + 1) == "=") {
            push(tokens, makeToken("OP", "<=", line)); i = i + 2;
        } else if (ch == ">" and i + 1 < n and charAt(source, i + 1) == "=") {
            push(tokens, makeToken("OP", ">=", line)); i = i + 2;

        } else if (contains(["+", "-", "*", "/", "=", "(", ")", "{", "}", "[", "]", ";", ",", "<", ">"], ch)) {
            push(tokens, makeToken("OP", ch, line));
            i = i + 1;

        } else {
            push(tokens, langError("Lexer", "محرف غير متوقَّع: '" + ch + "'", line));
            i = i + 1;
        }
    }

    push(tokens, eofToken(line));
    return tokens;
}
"""

    val PARSER_RIN: String = """
// ============================================================================
//  Parser.rin — المرحلة الثانية من "Illust": tokens -> شجرة AST
//  محلّل نازل بالتكرار (recursive descent). قواعد النحو (v0.2 موسّعة):
//
//    program    -> statement* EOF
//    statement  -> "let" IDENT "=" expr ";"
//                | IDENT "=" expr ";"                                  // إعادة إسناد
//                | "canvas" "(" expr "," expr ")" ";"
//                | "fill" "(" fillArg ")" ";"
//                | "stroke" "(" strokeArg ")" ";"
//                | "rect" "(" expr×4 ")" ";"
//                | "circle" "(" expr×3 ")" ";"
//                | "ellipse" "(" expr×4 ")" ";"
//                | "polygon" "(" expr ")" ";"        // expr عادة مصفوفة [x1,y1,x2,y2,...]
//                | "path" "(" expr ")" ";"           // expr نص سمة d الخام في SVG
//                | "line" "(" expr×4 ")" ";"
//                | "text" "(" expr×3 ")" ";"
//                | "print" "(" expr ")" ";"
//                | "if" "(" expr ")" "{" statement* "}" ("else" "{" statement* "}")?
//                | "while" "(" expr ")" "{" statement* "}"
//                | "group" "(" expr "," expr ")" "{" statement* "}"    // <g transform="translate(dx,dy)">
//                | "rotate" "(" expr ")" "{" statement* "}"            // <g transform="rotate(a)">
//                | "fun" IDENT "(" (IDENT ("," IDENT)*)? ")" "{" statement* "}"
//                | "return" expr ";"
//                | callExpr ";"                                        // نداء دالة كجملة مستقلة
//    fillArg    -> "none" | expr
//    strokeArg  -> "none" | expr "," expr
//    expr       -> comparison
//    comparison -> term (("=="|"!="|"<"|"<="|">"|">=") term)*
//    term       -> factor (("+"|"-") factor)*
//    factor     -> unary (("*"|"/") unary)*
//    unary      -> "-" unary | callOrPrimary
//    callOrPrimary -> IDENT "(" (expr ("," expr)*)? ")"                 // نداء دالة كتعبير
//                   | primary
//    primary    -> NUMBER | STRING | IDENT | "(" expr ")" | "[" (expr ("," expr)*)? "]"
// ============================================================================

@import "./Lexer.rin";

fun parse(tokens) {
    let pos = 0;
    let statements = [];
    while (pAtEnd(tokens, pos) == false) {
        let r = parseStatement(tokens, pos);
        if (isLangError(r["node"])) { return r["node"]; }
        push(statements, r["node"]);
        pos = r["pos"];
    }
    return astNode("Program", 0, { body: statements });
}

fun parseBlock(tokens, pos) {
    // يستهلك "{" statement* "}" ويُعيد { body: [...], pos: ... }
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // '{'
    let body = [];
    while (pCheckValue(tokens, pos, "OP", "}") == false and pAtEnd(tokens, pos) == false) {
        let s = parseStatement(tokens, pos);
        if (isLangError(s["node"])) { return { body: s["node"], pos: pos, isErr: true }; }
        push(body, s["node"]);
        pos = s["pos"];
    }
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '}'
    return { body: body, pos: pos, isErr: false };
}

fun parseStatement(tokens, pos) {
    if (pCheckValue(tokens, pos, "KEYWORD", "let")) { return parseLet(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "canvas")) { return parseCanvas(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "fill")) { return parseFill(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "stroke")) { return parseStroke(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "rect")) { return parseShape4(tokens, pos, "Rect", ["x", "y", "w", "h"]); }
    if (pCheckValue(tokens, pos, "KEYWORD", "circle")) { return parseShape3(tokens, pos, "Circle", ["cx", "cy", "r"]); }
    if (pCheckValue(tokens, pos, "KEYWORD", "ellipse")) { return parseShape4(tokens, pos, "Ellipse", ["cx", "cy", "rx", "ry"]); }
    if (pCheckValue(tokens, pos, "KEYWORD", "polygon")) { return parseShape1(tokens, pos, "Polygon", "points"); }
    if (pCheckValue(tokens, pos, "KEYWORD", "path")) { return parseShape1(tokens, pos, "PathEl", "d"); }
    if (pCheckValue(tokens, pos, "KEYWORD", "line")) { return parseShape4(tokens, pos, "Line", ["x1", "y1", "x2", "y2"]); }
    if (pCheckValue(tokens, pos, "KEYWORD", "text")) { return parseText(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "print")) { return parsePrint(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "if")) { return parseIf(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "while")) { return parseWhile(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "group")) { return parseGroup(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "rotate")) { return parseRotate(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "fun")) { return parseFunDecl(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "return")) { return parseReturn(tokens, pos); }
    if (pCheck(tokens, pos, "IDENT") and pCheckValue(tokens, pos + 1, "OP", "=")) { return parseAssign(tokens, pos); }
    if (pCheck(tokens, pos, "IDENT") and pCheckValue(tokens, pos + 1, "OP", "(")) { return parseCallStatement(tokens, pos); }
    let tok = pPeek(tokens, pos);
    return { node: langError("Parser", "جملة غير معروفة تبدأ بـ '" + toString(tok["value"]) + "'", tok["line"]), pos: pos };
}

// إعادة إسناد لمتغيّر موجود مسبقاً: IDENT "=" expr ";"  (بعكس "let" الذي يُعرِّف متغيّراً جديداً)
fun parseAssign(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let nameTok = pPeek(tokens, pos);
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // IDENT
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '='
    let r = parseExpr(tokens, pos);
    if (isLangError(r["node"])) { return r; }
    pos = r["pos"];
    let a3 = pExpect(tokens, pos, "OP", "Parser"); // ';'
    pos = a3["pos"];
    return { node: astNode("Assign", line, { name: nameTok["value"], value: r["node"] }), pos: pos };
}

// نداء دالة مستخدم كجملة مستقلة: foo(1, 2);
fun parseCallStatement(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let r = parseExpr(tokens, pos);
    if (isLangError(r["node"])) { return r; }
    pos = r["pos"];
    let a = pExpect(tokens, pos, "OP", "Parser"); // ';'
    pos = a["pos"];
    return { node: astNode("ExprStmt", line, { expr: r["node"] }), pos: pos };
}

fun parseLet(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'let'
    let nameTok = pPeek(tokens, pos);
    let a2 = pExpect(tokens, pos, "IDENT", "Parser");
    if (isLangError(a2["tok"])) { return { node: a2["tok"], pos: pos }; }
    pos = a2["pos"];
    let a3 = pExpect(tokens, pos, "OP", "Parser"); // '='
    pos = a3["pos"];
    let r = parseExpr(tokens, pos);
    if (isLangError(r["node"])) { return r; }
    pos = r["pos"];
    let a4 = pExpect(tokens, pos, "OP", "Parser"); // ';'
    pos = a4["pos"];
    return { node: astNode("Let", line, { name: nameTok["value"], value: r["node"] }), pos: pos };
}

fun parseCanvas(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'canvas'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let w = parseExpr(tokens, pos);
    if (isLangError(w["node"])) { return w; }
    pos = w["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ','
    let h = parseExpr(tokens, pos);
    if (isLangError(h["node"])) { return h; }
    pos = h["pos"];
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ')'
    let a5 = pAdvance(tokens, pos); pos = a5["pos"]; // ';'
    return { node: astNode("Canvas", line, { w: w["node"], h: h["node"] }), pos: pos };
}

fun parseFill(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'fill'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    if (pCheckValue(tokens, pos, "KEYWORD", "none")) {
        let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // 'none'
        let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ')'
        let a5 = pAdvance(tokens, pos); pos = a5["pos"]; // ';'
        return { node: astNode("Fill", line, { none: true, color: nil }), pos: pos };
    }
    let c = parseExpr(tokens, pos);
    if (isLangError(c["node"])) { return c; }
    pos = c["pos"];
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ')'
    let a5 = pAdvance(tokens, pos); pos = a5["pos"]; // ';'
    return { node: astNode("Fill", line, { none: false, color: c["node"] }), pos: pos };
}

fun parseStroke(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'stroke'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    if (pCheckValue(tokens, pos, "KEYWORD", "none")) {
        let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // 'none'
        let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ')'
        let a5 = pAdvance(tokens, pos); pos = a5["pos"]; // ';'
        return { node: astNode("Stroke", line, { none: true, color: nil, width: nil }), pos: pos };
    }
    let c = parseExpr(tokens, pos);
    if (isLangError(c["node"])) { return c; }
    pos = c["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ','
    let w = parseExpr(tokens, pos);
    if (isLangError(w["node"])) { return w; }
    pos = w["pos"];
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ')'
    let a5 = pAdvance(tokens, pos); pos = a5["pos"]; // ';'
    return { node: astNode("Stroke", line, { none: false, color: c["node"], width: w["node"] }), pos: pos };
}

// أشكال بأربعة معطيات رقمية: rect(x,y,w,h) و line(x1,y1,x2,y2)
fun parseShape4(tokens, pos, kind, names) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // اسم الأمر
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let v1 = parseExpr(tokens, pos); if (isLangError(v1["node"])) { return v1; } pos = v1["pos"];
    let s1 = pAdvance(tokens, pos); pos = s1["pos"]; // ','
    let v2 = parseExpr(tokens, pos); if (isLangError(v2["node"])) { return v2; } pos = v2["pos"];
    let s2 = pAdvance(tokens, pos); pos = s2["pos"]; // ','
    let v3 = parseExpr(tokens, pos); if (isLangError(v3["node"])) { return v3; } pos = v3["pos"];
    let s3 = pAdvance(tokens, pos); pos = s3["pos"]; // ','
    let v4 = parseExpr(tokens, pos); if (isLangError(v4["node"])) { return v4; } pos = v4["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ';'
    let props = {};
    props[names[0]] = v1["node"];
    props[names[1]] = v2["node"];
    props[names[2]] = v3["node"];
    props[names[3]] = v4["node"];
    return { node: astNode(kind, line, props), pos: pos };
}

// أشكال بثلاثة معطيات: circle(cx,cy,r)
fun parseShape3(tokens, pos, kind, names) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"];
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let v1 = parseExpr(tokens, pos); if (isLangError(v1["node"])) { return v1; } pos = v1["pos"];
    let s1 = pAdvance(tokens, pos); pos = s1["pos"]; // ','
    let v2 = parseExpr(tokens, pos); if (isLangError(v2["node"])) { return v2; } pos = v2["pos"];
    let s2 = pAdvance(tokens, pos); pos = s2["pos"]; // ','
    let v3 = parseExpr(tokens, pos); if (isLangError(v3["node"])) { return v3; } pos = v3["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ';'
    let props = {};
    props[names[0]] = v1["node"];
    props[names[1]] = v2["node"];
    props[names[2]] = v3["node"];
    return { node: astNode(kind, line, props), pos: pos };
}

// أشكال بمعطى واحد فقط: polygon(pointsArrayExpr) و path(dStringExpr)
fun parseShape1(tokens, pos, kind, name) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // اسم الأمر
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let v1 = parseExpr(tokens, pos); if (isLangError(v1["node"])) { return v1; } pos = v1["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ';'
    let props = {};
    props[name] = v1["node"];
    return { node: astNode(kind, line, props), pos: pos };
}

fun parseText(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'text'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let x = parseExpr(tokens, pos); if (isLangError(x["node"])) { return x; } pos = x["pos"];
    let s1 = pAdvance(tokens, pos); pos = s1["pos"]; // ','
    let y = parseExpr(tokens, pos); if (isLangError(y["node"])) { return y; } pos = y["pos"];
    let s2 = pAdvance(tokens, pos); pos = s2["pos"]; // ','
    let str = parseExpr(tokens, pos); if (isLangError(str["node"])) { return str; } pos = str["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ';'
    return { node: astNode("Text", line, { x: x["node"], y: y["node"], str: str["node"] }), pos: pos };
}

fun parsePrint(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'print'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let v = parseExpr(tokens, pos); if (isLangError(v["node"])) { return v; } pos = v["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ';'
    return { node: astNode("PrintDebug", line, { value: v["node"] }), pos: pos };
}

fun parseIf(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'if'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let cond = parseExpr(tokens, pos);
    if (isLangError(cond["node"])) { return cond; }
    pos = cond["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let thenBlk = parseBlock(tokens, pos);
    if (thenBlk["isErr"]) { return { node: thenBlk["body"], pos: thenBlk["pos"] }; }
    pos = thenBlk["pos"];
    let elseBody = [];
    if (pCheckValue(tokens, pos, "KEYWORD", "else")) {
        let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // 'else'
        let elseBlk = parseBlock(tokens, pos);
        if (elseBlk["isErr"]) { return { node: elseBlk["body"], pos: elseBlk["pos"] }; }
        pos = elseBlk["pos"];
        elseBody = elseBlk["body"];
    }
    return { node: astNode("If", line, { cond: cond["node"], thenBody: thenBlk["body"], elseBody: elseBody }), pos: pos };
}

fun parseWhile(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'while'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let cond = parseExpr(tokens, pos);
    if (isLangError(cond["node"])) { return cond; }
    pos = cond["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let blk = parseBlock(tokens, pos);
    if (blk["isErr"]) { return { node: blk["body"], pos: blk["pos"] }; }
    pos = blk["pos"];
    return { node: astNode("While", line, { cond: cond["node"], body: blk["body"] }), pos: pos };
}

fun parseGroup(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'group'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let dx = parseExpr(tokens, pos); if (isLangError(dx["node"])) { return dx; } pos = dx["pos"];
    let s1 = pAdvance(tokens, pos); pos = s1["pos"]; // ','
    let dy = parseExpr(tokens, pos); if (isLangError(dy["node"])) { return dy; } pos = dy["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let blk = parseBlock(tokens, pos);
    if (blk["isErr"]) { return { node: blk["body"], pos: blk["pos"] }; }
    pos = blk["pos"];
    return { node: astNode("Group", line, { dx: dx["node"], dy: dy["node"], body: blk["body"] }), pos: pos };
}

fun parseRotate(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'rotate'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let angle = parseExpr(tokens, pos); if (isLangError(angle["node"])) { return angle; } pos = angle["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let blk = parseBlock(tokens, pos);
    if (blk["isErr"]) { return { node: blk["body"], pos: blk["pos"] }; }
    pos = blk["pos"];
    return { node: astNode("Rotate", line, { angle: angle["node"], body: blk["body"] }), pos: pos };
}

fun parseFunDecl(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'fun'
    let nameTok = pPeek(tokens, pos);
    let a2 = pExpect(tokens, pos, "IDENT", "Parser");
    if (isLangError(a2["tok"])) { return { node: a2["tok"], pos: pos }; }
    pos = a2["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // '('
    let params = [];
    while (pCheckValue(tokens, pos, "OP", ")") == false) {
        let pTok = pPeek(tokens, pos);
        let a = pExpect(tokens, pos, "IDENT", "Parser");
        if (isLangError(a["tok"])) { return { node: a["tok"], pos: pos }; }
        pos = a["pos"];
        push(params, pTok["value"]);
        if (pCheckValue(tokens, pos, "OP", ",")) {
            let c = pAdvance(tokens, pos); pos = c["pos"];
        }
    }
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // ')'
    let blk = parseBlock(tokens, pos);
    if (blk["isErr"]) { return { node: blk["body"], pos: blk["pos"] }; }
    pos = blk["pos"];
    return { node: astNode("FunDecl", line, { name: nameTok["value"], params: params, body: blk["body"] }), pos: pos };
}

fun parseReturn(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'return'
    let v = parseExpr(tokens, pos); if (isLangError(v["node"])) { return v; } pos = v["pos"];
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // ';'
    return { node: astNode("Return", line, { value: v["node"] }), pos: pos };
}

fun parseExpr(tokens, pos) { return parseComparison(tokens, pos); }

fun parseComparison(tokens, pos) {
    let left = parseTerm(tokens, pos);
    if (isLangError(left["node"])) { return left; }
    pos = left["pos"];
    let node = left["node"];
    while (pCheckValue(tokens, pos, "OP", "==") or pCheckValue(tokens, pos, "OP", "!=")
        or pCheckValue(tokens, pos, "OP", "<") or pCheckValue(tokens, pos, "OP", "<=")
        or pCheckValue(tokens, pos, "OP", ">") or pCheckValue(tokens, pos, "OP", ">=")) {
        let opTok = pPeek(tokens, pos);
        let a = pAdvance(tokens, pos); pos = a["pos"];
        let right = parseTerm(tokens, pos);
        if (isLangError(right["node"])) { return right; }
        pos = right["pos"];
        node = astNode("Binary", opTok["line"], { op: opTok["value"], left: node, right: right["node"] });
    }
    return { node: node, pos: pos };
}

fun parseTerm(tokens, pos) {
    let left = parseFactor(tokens, pos);
    if (isLangError(left["node"])) { return left; }
    pos = left["pos"];
    let node = left["node"];
    while (pCheckValue(tokens, pos, "OP", "+") or pCheckValue(tokens, pos, "OP", "-")) {
        let opTok = pPeek(tokens, pos);
        let a = pAdvance(tokens, pos); pos = a["pos"];
        let right = parseFactor(tokens, pos);
        if (isLangError(right["node"])) { return right; }
        pos = right["pos"];
        node = astNode("Binary", opTok["line"], { op: opTok["value"], left: node, right: right["node"] });
    }
    return { node: node, pos: pos };
}

fun parseFactor(tokens, pos) {
    let left = parseUnary(tokens, pos);
    if (isLangError(left["node"])) { return left; }
    pos = left["pos"];
    let node = left["node"];
    while (pCheckValue(tokens, pos, "OP", "*") or pCheckValue(tokens, pos, "OP", "/")) {
        let opTok = pPeek(tokens, pos);
        let a = pAdvance(tokens, pos); pos = a["pos"];
        let right = parseUnary(tokens, pos);
        if (isLangError(right["node"])) { return right; }
        pos = right["pos"];
        node = astNode("Binary", opTok["line"], { op: opTok["value"], left: node, right: right["node"] });
    }
    return { node: node, pos: pos };
}

fun parseUnary(tokens, pos) {
    if (pCheckValue(tokens, pos, "OP", "-")) {
        let opTok = pPeek(tokens, pos);
        let a = pAdvance(tokens, pos); pos = a["pos"];
        let operand = parseUnary(tokens, pos);
        if (isLangError(operand["node"])) { return operand; }
        return { node: astNode("Unary", opTok["line"], { op: "-", operand: operand["node"] }), pos: operand["pos"] };
    }
    return parsePrimary(tokens, pos);
}

fun parsePrimary(tokens, pos) {
    let tok = pPeek(tokens, pos);
    if (tokIs(tok, "NUMBER")) {
        let a = pAdvance(tokens, pos);
        return { node: astNode("NumberLit", tok["line"], { value: toNumber(tok["value"]) }), pos: a["pos"] };
    }
    if (tokIs(tok, "STRING")) {
        let a = pAdvance(tokens, pos);
        return { node: astNode("StringLit", tok["line"], { value: tok["value"] }), pos: a["pos"] };
    }
    if (tokIs(tok, "IDENT")) {
        let a = pAdvance(tokens, pos);
        // نداء دالة كتعبير: foo(1, 2)
        if (pCheckValue(tokens, a["pos"], "OP", "(")) {
            return parseCallArgs(tokens, a["pos"], tok["value"], tok["line"]);
        }
        return { node: astNode("Ident", tok["line"], { name: tok["value"] }), pos: a["pos"] };
    }
    if (tokIsValue(tok, "OP", "(")) {
        let a = pAdvance(tokens, pos);
        let inner = parseExpr(tokens, a["pos"]);
        if (isLangError(inner["node"])) { return inner; }
        let a2 = pAdvance(tokens, inner["pos"]); // ')'
        return { node: inner["node"], pos: a2["pos"] };
    }
    if (tokIsValue(tok, "OP", "[")) {
        let a = pAdvance(tokens, pos);
        pos = a["pos"];
        let items = [];
        while (pCheckValue(tokens, pos, "OP", "]") == false) {
            let it = parseExpr(tokens, pos);
            if (isLangError(it["node"])) { return it; }
            pos = it["pos"];
            push(items, it["node"]);
            if (pCheckValue(tokens, pos, "OP", ",")) {
                let c = pAdvance(tokens, pos); pos = c["pos"];
            }
        }
        let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // ']'
        return { node: astNode("ArrayLit", tok["line"], { items: items }), pos: pos };
    }
    return { node: langError("Parser", "تعبير غير متوقَّع عند '" + toString(tok["value"]) + "'", tok["line"]), pos: pos };
}

// يستهلك "(" arg ("," arg)* ")" لِنداء دالة اسمها fname، بدءاً من الموضع الواقف عند "("
fun parseCallArgs(tokens, pos, fname, line) {
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // '('
    let args = [];
    while (pCheckValue(tokens, pos, "OP", ")") == false) {
        let e = parseExpr(tokens, pos);
        if (isLangError(e["node"])) { return e; }
        pos = e["pos"];
        push(args, e["node"]);
        if (pCheckValue(tokens, pos, "OP", ",")) {
            let c = pAdvance(tokens, pos); pos = c["pos"];
        }
    }
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // ')'
    return { node: astNode("Call", line, { name: fname, args: args }), pos: pos };
}
"""

    val INTERPRETER_RIN: String = """
// ============================================================================
//  Interpreter.rin — المرحلة الثالثة من "Illust" (v0.2 موسّعة): تنفيذ AST مباشرة
//  وتوليد SVG حقيقي.
//
//  قاعدة تصميم عامة (كما في langkit): أي دالة قد تفشل تُعيد ok(value)/err(errObj).
//  execStatement/execBlock تُعيدان تحديداً ok({ ctrl: "normal" }) أو
//  ok({ ctrl: "return", value: v }) — تسمح لجملة return بالانتشار للأعلى عبر
//  if/while/group/rotate حتى تصل لنداء الدالة التي بدأت التنفيذ (انظر callFunction).
//
//  ctx خريطة حالة عامة واحدة تُمرَّر بالمرجع: { width, height, elements: [...],
//  fillNone, fillColor, strokeNone, strokeColor, strokeWidth, functions: {...} }.
//  group()/rotate() لم تعد تُعدِّل إحداثيات كل شكل يدوياً؛ بدل ذلك تصدر عنصر
//  SVG حقيقي <g transform="..."> يلف عناصر الأبناء — أبسط وأصح ويدعم الدوران فعلياً.
// ============================================================================

@import "./Parser.rin";

fun newCtx() {
    return {
        width: 400,
        height: 300,
        elements: [],
        fillNone: false,
        fillColor: "#000000",
        strokeNone: true,
        strokeColor: "#000000",
        strokeWidth: 1,
        functions: {}
    };
}

fun normal() { return ok({ ctrl: "normal" }); }

// يُنفّذ برنامجاً كاملاً ويُعيد نص SVG، أو نص خطأ منسَّق عبر formatLangError
fun interpret(program) {
    if (isLangError(program)) { return formatLangError(program); }
    let ctx = newCtx();
    let env = {};
    let log = [];
    let r = execBlock(program["body"], env, ctx, log);
    if (isOk(r) == false) { return formatLangError(resultError(r)); }
    return renderSVG(ctx);
}

// ينفّذ قائمة جمل بالترتيب؛ يتوقف فوراً وينشر النتيجة للأعلى عند أول خطأ أو return
fun execBlock(statements, env, ctx, log) {
    let i = 0;
    while (i < len(statements)) {
        let r = execStatement(statements[i], env, ctx, log);
        if (isOk(r) == false) { return r; }
        if (r["value"]["ctrl"] == "return") { return r; }
        i = i + 1;
    }
    return normal();
}

fun execStatement(node, env, ctx, log) {
    if (nodeIs(node, "Let")) {
        let v = evalExpr(node["value"], env, ctx, log);
        if (isOk(v) == false) { return v; }
        env[node["name"]] = v["value"];
        return normal();
    }
    if (nodeIs(node, "Assign")) {
        if (has(env, node["name"]) == false) {
            return err(langError("Interpreter", "متغيّر غير معرَّف: " + node["name"], node["line"]));
        }
        let v = evalExpr(node["value"], env, ctx, log);
        if (isOk(v) == false) { return v; }
        env[node["name"]] = v["value"];
        return normal();
    }
    if (nodeIs(node, "ExprStmt")) {
        let v = evalExpr(node["expr"], env, ctx, log);
        if (isOk(v) == false) { return v; }
        return normal();
    }
    if (nodeIs(node, "Canvas")) {
        let w = evalExpr(node["w"], env, ctx, log); if (isOk(w) == false) { return w; }
        let h = evalExpr(node["h"], env, ctx, log); if (isOk(h) == false) { return h; }
        ctx["width"] = w["value"];
        ctx["height"] = h["value"];
        return normal();
    }
    if (nodeIs(node, "Fill")) {
        if (node["none"]) {
            ctx["fillNone"] = true;
        } else {
            let c = evalExpr(node["color"], env, ctx, log); if (isOk(c) == false) { return c; }
            ctx["fillNone"] = false;
            ctx["fillColor"] = toString(c["value"]);
        }
        return normal();
    }
    if (nodeIs(node, "Stroke")) {
        if (node["none"]) {
            ctx["strokeNone"] = true;
        } else {
            let c = evalExpr(node["color"], env, ctx, log); if (isOk(c) == false) { return c; }
            let w = evalExpr(node["width"], env, ctx, log); if (isOk(w) == false) { return w; }
            ctx["strokeNone"] = false;
            ctx["strokeColor"] = toString(c["value"]);
            ctx["strokeWidth"] = w["value"];
        }
        return normal();
    }
    if (nodeIs(node, "Rect")) {
        let x = evalExpr(node["x"], env, ctx, log); if (isOk(x) == false) { return x; }
        let y = evalExpr(node["y"], env, ctx, log); if (isOk(y) == false) { return y; }
        let w = evalExpr(node["w"], env, ctx, log); if (isOk(w) == false) { return w; }
        let h = evalExpr(node["h"], env, ctx, log); if (isOk(h) == false) { return h; }
        push(ctx["elements"], "  <rect x=\"" + toString(x["value"]) + "\" y=\"" + toString(y["value"])
            + "\" width=\"" + toString(w["value"]) + "\" height=\"" + toString(h["value"]) + "\"" + styleAttrs(ctx) + " />");
        return normal();
    }
    if (nodeIs(node, "Circle")) {
        let cx = evalExpr(node["cx"], env, ctx, log); if (isOk(cx) == false) { return cx; }
        let cy = evalExpr(node["cy"], env, ctx, log); if (isOk(cy) == false) { return cy; }
        let r = evalExpr(node["r"], env, ctx, log); if (isOk(r) == false) { return r; }
        push(ctx["elements"], "  <circle cx=\"" + toString(cx["value"]) + "\" cy=\"" + toString(cy["value"])
            + "\" r=\"" + toString(r["value"]) + "\"" + styleAttrs(ctx) + " />");
        return normal();
    }
    if (nodeIs(node, "Ellipse")) {
        let cx = evalExpr(node["cx"], env, ctx, log); if (isOk(cx) == false) { return cx; }
        let cy = evalExpr(node["cy"], env, ctx, log); if (isOk(cy) == false) { return cy; }
        let rx = evalExpr(node["rx"], env, ctx, log); if (isOk(rx) == false) { return rx; }
        let ry = evalExpr(node["ry"], env, ctx, log); if (isOk(ry) == false) { return ry; }
        push(ctx["elements"], "  <ellipse cx=\"" + toString(cx["value"]) + "\" cy=\"" + toString(cy["value"])
            + "\" rx=\"" + toString(rx["value"]) + "\" ry=\"" + toString(ry["value"]) + "\"" + styleAttrs(ctx) + " />");
        return normal();
    }
    if (nodeIs(node, "Polygon")) {
        let pts = evalExpr(node["points"], env, ctx, log); if (isOk(pts) == false) { return pts; }
        let arr = pts["value"];
        let strs = [];
        let i = 0;
        while (i < len(arr)) { push(strs, toString(arr[i])); i = i + 1; }
        push(ctx["elements"], "  <polygon points=\"" + join(strs, ",") + "\"" + styleAttrs(ctx) + " />");
        return normal();
    }
    if (nodeIs(node, "PathEl")) {
        let d = evalExpr(node["d"], env, ctx, log); if (isOk(d) == false) { return d; }
        push(ctx["elements"], "  <path d=\"" + escapeXml(toString(d["value"])) + "\"" + styleAttrs(ctx) + " />");
        return normal();
    }
    if (nodeIs(node, "Line")) {
        let x1 = evalExpr(node["x1"], env, ctx, log); if (isOk(x1) == false) { return x1; }
        let y1 = evalExpr(node["y1"], env, ctx, log); if (isOk(y1) == false) { return y1; }
        let x2 = evalExpr(node["x2"], env, ctx, log); if (isOk(x2) == false) { return x2; }
        let y2 = evalExpr(node["y2"], env, ctx, log); if (isOk(y2) == false) { return y2; }
        let strokeBit = " stroke=\"" + ctx["strokeColor"] + "\" stroke-width=\"" + toString(ctx["strokeWidth"]) + "\"";
        if (ctx["strokeNone"]) { strokeBit = " stroke=\"" + ctx["fillColor"] + "\" stroke-width=\"1\""; }
        push(ctx["elements"], "  <line x1=\"" + toString(x1["value"]) + "\" y1=\"" + toString(y1["value"])
            + "\" x2=\"" + toString(x2["value"]) + "\" y2=\"" + toString(y2["value"]) + "\"" + strokeBit + " />");
        return normal();
    }
    if (nodeIs(node, "Text")) {
        let x = evalExpr(node["x"], env, ctx, log); if (isOk(x) == false) { return x; }
        let y = evalExpr(node["y"], env, ctx, log); if (isOk(y) == false) { return y; }
        let s = evalExpr(node["str"], env, ctx, log); if (isOk(s) == false) { return s; }
        push(ctx["elements"], "  <text x=\"" + toString(x["value"]) + "\" y=\"" + toString(y["value"]) + "\""
            + styleAttrs(ctx) + ">" + escapeXml(toString(s["value"])) + "</text>");
        return normal();
    }
    if (nodeIs(node, "PrintDebug")) {
        let v = evalExpr(node["value"], env, ctx, log); if (isOk(v) == false) { return v; }
        push(log, toString(v["value"]));
        return normal();
    }
    if (nodeIs(node, "If")) {
        let c = evalExpr(node["cond"], env, ctx, log); if (isOk(c) == false) { return c; }
        if (toBool(c["value"])) { return execBlock(node["thenBody"], env, ctx, log); }
        return execBlock(node["elseBody"], env, ctx, log);
    }
    if (nodeIs(node, "While")) {
        let c = evalExpr(node["cond"], env, ctx, log); if (isOk(c) == false) { return c; }
        while (toBool(c["value"])) {
            let r = execBlock(node["body"], env, ctx, log);
            if (isOk(r) == false) { return r; }
            if (r["value"]["ctrl"] == "return") { return r; }
            c = evalExpr(node["cond"], env, ctx, log); if (isOk(c) == false) { return c; }
        }
        return normal();
    }
    if (nodeIs(node, "Group")) {
        let dx = evalExpr(node["dx"], env, ctx, log); if (isOk(dx) == false) { return dx; }
        let dy = evalExpr(node["dy"], env, ctx, log); if (isOk(dy) == false) { return dy; }
        push(ctx["elements"], "  <g transform=\"translate(" + toString(dx["value"]) + "," + toString(dy["value"]) + ")\">");
        let r = execBlock(node["body"], env, ctx, log);
        push(ctx["elements"], "  </g>");
        return r;
    }
    if (nodeIs(node, "Rotate")) {
        let a = evalExpr(node["angle"], env, ctx, log); if (isOk(a) == false) { return a; }
        push(ctx["elements"], "  <g transform=\"rotate(" + toString(a["value"]) + ")\">");
        let r = execBlock(node["body"], env, ctx, log);
        push(ctx["elements"], "  </g>");
        return r;
    }
    if (nodeIs(node, "FunDecl")) {
        ctx["functions"][node["name"]] = { params: node["params"], body: node["body"] };
        return normal();
    }
    if (nodeIs(node, "Return")) {
        let v = evalExpr(node["value"], env, ctx, log); if (isOk(v) == false) { return v; }
        return ok({ ctrl: "return", value: v["value"] });
    }
    return err(langError("Interpreter", "نوع جملة غير مدعوم: " + node["kind"], node["line"]));
}

fun evalExpr(node, env, ctx, log) {
    if (nodeIs(node, "NumberLit")) { return ok(node["value"]); }
    if (nodeIs(node, "StringLit")) { return ok(node["value"]); }
    if (nodeIs(node, "ArrayLit")) {
        let items = node["items"];
        let out = [];
        let i = 0;
        while (i < len(items)) {
            let v = evalExpr(items[i], env, ctx, log);
            if (isOk(v) == false) { return v; }
            push(out, v["value"]);
            i = i + 1;
        }
        return ok(out);
    }
    if (nodeIs(node, "Ident")) {
        if (has(env, node["name"]) == false) {
            return err(langError("Interpreter", "متغيّر غير معرَّف: " + node["name"], node["line"]));
        }
        return ok(env[node["name"]]);
    }
    if (nodeIs(node, "Call")) { return callFunction(node["name"], node["args"], env, ctx, log, node["line"]); }
    if (nodeIs(node, "Unary")) {
        let v = evalExpr(node["operand"], env, ctx, log);
        if (isOk(v) == false) { return v; }
        return ok(0 - v["value"]);
    }
    if (nodeIs(node, "Binary")) {
        let l = evalExpr(node["left"], env, ctx, log);
        if (isOk(l) == false) { return l; }
        let r = evalExpr(node["right"], env, ctx, log);
        if (isOk(r) == false) { return r; }
        let lv = l["value"];
        let rv = r["value"];
        let op = node["op"];
        if (op == "+") { return ok(lv + rv); }
        if (op == "-") { return ok(lv - rv); }
        if (op == "*") { return ok(lv * rv); }
        if (op == "/") { return ok(lv / rv); }
        if (op == "==") { return ok(lv == rv); }
        if (op == "!=") { return ok(lv != rv); }
        if (op == "<") { return ok(lv < rv); }
        if (op == "<=") { return ok(lv <= rv); }
        if (op == ">") { return ok(lv > rv); }
        if (op == ">=") { return ok(lv >= rv); }
        return err(langError("Interpreter", "عامل غير معروف: " + op, node["line"]));
    }
    return err(langError("Interpreter", "نوع عقدة غير مدعوم: " + node["kind"], node["line"]));
}

// نداء دالة معرَّفة بواسطة المستخدم عبر fun: بيئة جديدة (المعاملات فقط، بلا إغلاق على
// متغيرات المستدعي)، لكن ctx (لوحة الرسم) مشتركة عالمياً فتقدر الدالة ترسم مباشرة.
fun callFunction(name, argNodes, env, ctx, log, line) {
    if (has(ctx["functions"], name) == false) {
        return err(langError("Interpreter", "دالة غير معرَّفة: " + name, line));
    }
    let def = ctx["functions"][name];
    if (len(argNodes) != len(def["params"])) {
        return err(langError("Interpreter", "عدد معطيات غير مطابق لِـ" + name, line));
    }
    let newEnv = {};
    let i = 0;
    while (i < len(argNodes)) {
        let v = evalExpr(argNodes[i], env, ctx, log);
        if (isOk(v) == false) { return v; }
        newEnv[def["params"][i]] = v["value"];
        i = i + 1;
    }
    let r = execBlock(def["body"], newEnv, ctx, log);
    if (isOk(r) == false) { return r; }
    if (r["value"]["ctrl"] == "return") { return ok(r["value"]["value"]); }
    return ok(nil);
}

// ---- بناء نص SVG النهائي من ctx ------------------------------------------

fun styleAttrs(ctx) {
    let s = "";
    if (ctx["fillNone"]) { s = s + " fill=\"none\""; } else { s = s + " fill=\"" + ctx["fillColor"] + "\""; }
    if (ctx["strokeNone"] == false) {
        s = s + " stroke=\"" + ctx["strokeColor"] + "\" stroke-width=\"" + toString(ctx["strokeWidth"]) + "\"";
    }
    return s;
}

fun escapeXml(s) {
    let out = "";
    let i = 0;
    while (i < len(s)) {
        let c = charAt(s, i);
        if (c == "&") { out = out + "&amp;"; }
        else if (c == "<") { out = out + "&lt;"; }
        else if (c == ">") { out = out + "&gt;"; }
        else if (c == "\"") { out = out + "&quot;"; }
        else { out = out + c; }
        i = i + 1;
    }
    return out;
}

fun renderSVG(ctx) {
    let lines = [];
    push(lines, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + toString(ctx["width"]) + " " + toString(ctx["height"]) + "\" width=\"" + toString(ctx["width"]) + "\" height=\"" + toString(ctx["height"]) + "\">");
    let i = 0;
    while (i < len(ctx["elements"])) {
        push(lines, ctx["elements"][i]);
        i = i + 1;
    }
    push(lines, "</svg>");
    return join(lines, "\n");
}
"""

    val CODEGEN_RIN: String = """
// ============================================================================
//  CodeGen.rin — مسار بديل لـ Interpreter.rin (v0.2 موسّعة): يولّد كود Rin حقيقي
//  مستقل بذاته يبني نفس مخرجات SVG. دوال illust المعرَّفة بـ fun تُترجَم مباشرة
//  إلى دوال Rin حقيقية (fun/return أصليان في اللغة) — بلا أي محاكاة استدعاء.
// ============================================================================

@import "./Parser.rin";

fun preambleLines() {
    return [
        "fun __newCtx() {",
        "    return { width: 400, height: 300, elements: [], fillNone: false,",
        "             fillColor: \"#000000\", strokeNone: true, strokeColor: \"#000000\",",
        "             strokeWidth: 1 };",
        "}",
        "fun __styleAttrs(ctx) {",
        "    let s = \"\";",
        "    if (ctx[\"fillNone\"]) { s = s + \" fill=\\\"none\\\"\"; } else { s = s + \" fill=\\\"\" + ctx[\"fillColor\"] + \"\\\"\"; }",
        "    if (ctx[\"strokeNone\"] == false) {",
        "        s = s + \" stroke=\\\"\" + ctx[\"strokeColor\"] + \"\\\" stroke-width=\\\"\" + toString(ctx[\"strokeWidth\"]) + \"\\\"\";",
        "    }",
        "    return s;",
        "}",
        "fun __escapeXml(s) {",
        "    let out = \"\"; let i = 0;",
        "    while (i < len(s)) {",
        "        let c = charAt(s, i);",
        "        if (c == \"&\") { out = out + \"&amp;\"; }",
        "        else if (c == \"<\") { out = out + \"&lt;\"; }",
        "        else if (c == \">\") { out = out + \"&gt;\"; }",
        "        else if (c == \"\\\"\") { out = out + \"&quot;\"; }",
        "        else { out = out + c; }",
        "        i = i + 1;",
        "    }",
        "    return out;",
        "}",
        "fun __rect(ctx, x, y, w, h) {",
        "    push(ctx[\"elements\"], \"  <rect x=\\\"\" + toString(x) + \"\\\" y=\\\"\" + toString(y) + \"\\\" width=\\\"\" + toString(w) + \"\\\" height=\\\"\" + toString(h) + \"\\\"\" + __styleAttrs(ctx) + \" />\");",
        "}",
        "fun __circle(ctx, cx, cy, r) {",
        "    push(ctx[\"elements\"], \"  <circle cx=\\\"\" + toString(cx) + \"\\\" cy=\\\"\" + toString(cy) + \"\\\" r=\\\"\" + toString(r) + \"\\\"\" + __styleAttrs(ctx) + \" />\");",
        "}",
        "fun __ellipse(ctx, cx, cy, rx, ry) {",
        "    push(ctx[\"elements\"], \"  <ellipse cx=\\\"\" + toString(cx) + \"\\\" cy=\\\"\" + toString(cy) + \"\\\" rx=\\\"\" + toString(rx) + \"\\\" ry=\\\"\" + toString(ry) + \"\\\"\" + __styleAttrs(ctx) + \" />\");",
        "}",
        "fun __polygon(ctx, points) {",
        "    let strs = []; let i = 0;",
        "    while (i < len(points)) { push(strs, toString(points[i])); i = i + 1; }",
        "    push(ctx[\"elements\"], \"  <polygon points=\\\"\" + join(strs, \",\") + \"\\\"\" + __styleAttrs(ctx) + \" />\");",
        "}",
        "fun __path(ctx, d) {",
        "    push(ctx[\"elements\"], \"  <path d=\\\"\" + __escapeXml(toString(d)) + \"\\\"\" + __styleAttrs(ctx) + \" />\");",
        "}",
        "fun __line(ctx, x1, y1, x2, y2) {",
        "    let sb = \" stroke=\\\"\" + ctx[\"strokeColor\"] + \"\\\" stroke-width=\\\"\" + toString(ctx[\"strokeWidth\"]) + \"\\\"\";",
        "    if (ctx[\"strokeNone\"]) { sb = \" stroke=\\\"\" + ctx[\"fillColor\"] + \"\\\" stroke-width=\\\"1\\\"\"; }",
        "    push(ctx[\"elements\"], \"  <line x1=\\\"\" + toString(x1) + \"\\\" y1=\\\"\" + toString(y1) + \"\\\" x2=\\\"\" + toString(x2) + \"\\\" y2=\\\"\" + toString(y2) + \"\\\"\" + sb + \" />\");",
        "}",
        "fun __text(ctx, x, y, s) {",
        "    push(ctx[\"elements\"], \"  <text x=\\\"\" + toString(x) + \"\\\" y=\\\"\" + toString(y) + \"\\\"\" + __styleAttrs(ctx) + \">\" + __escapeXml(toString(s)) + \"</text>\");",
        "}",
        "fun __gOpen(ctx, transform) { push(ctx[\"elements\"], \"  <g transform=\\\"\" + transform + \"\\\">\"); }",
        "fun __gClose(ctx) { push(ctx[\"elements\"], \"  </g>\"); }",
        "fun __renderSVG(ctx) {",
        "    let lines = [];",
        "    push(lines, \"<svg xmlns=\\\"http://www.w3.org/2000/svg\\\" viewBox=\\\"0 0 \" + toString(ctx[\"width\"]) + \" \" + toString(ctx[\"height\"]) + \"\\\" width=\\\"\" + toString(ctx[\"width\"]) + \"\\\" height=\\\"\" + toString(ctx[\"height\"]) + \"\\\">\");",
        "    let i = 0;",
        "    while (i < len(ctx[\"elements\"])) { push(lines, ctx[\"elements\"][i]); i = i + 1; }",
        "    push(lines, \"</svg>\");",
        "    return join(lines, \"\\n\");",
        "}"
    ];
}

fun generate(program) {
    if (isLangError(program)) { return formatLangError(program); }
    let lines = [];
    push(lines, "// ---- مُولَّد تلقائياً من Illust عبر CodeGen.rin ----");
    push(lines, "// كود Rin مستقل بذاته: لا يعتمد وقت التشغيل على مفسّر Illust.");
    let pre = preambleLines();
    let i = 0;
    while (i < len(pre)) { push(lines, pre[i]); i = i + 1; }
    push(lines, "let __ctx = __newCtx();");

    // تمريرة أولى: عرِّف دوال المستخدم (fun) بصيغة Rin حقيقية قبل بقية البرنامج،
    // لأن Rin (مثل أي لغة C-style) لا يشترط ترتيباً لكن هذا أوضح للقارئ.
    let topBody = program["body"];
    let mainBody = [];
    let j = 0;
    while (j < len(topBody)) {
        let stmt = topBody[j];
        if (nodeIs(stmt, "FunDecl")) {
            let r = genFunDecl(stmt, lines);
            if (isOk(r) == false) { return formatLangError(resultError(r)); }
        } else {
            push(mainBody, stmt);
        }
        j = j + 1;
    }

    let r2 = genBlock(mainBody, lines, "");
    if (isOk(r2) == false) { return formatLangError(resultError(r2)); }
    push(lines, "print __renderSVG(__ctx);");
    return join(lines, "\n");
}

fun genFunDecl(node, lines) {
    push(lines, "fun " + node["name"] + "(" + join(node["params"], ", ") + ") {");
    let r = genBlock(node["body"], lines, "    ");
    if (isOk(r) == false) { return r; }
    push(lines, "}");
    return ok(true);
}

fun genBlock(statements, lines, indent) {
    let i = 0;
    while (i < len(statements)) {
        let r = genStatement(statements[i], lines, indent);
        if (isOk(r) == false) { return r; }
        i = i + 1;
    }
    return ok(true);
}

fun genStatement(node, lines, indent) {
    if (nodeIs(node, "FunDecl")) {
        // دالة معرَّفة داخل كتلة متداخلة: نفس المعالجة، Rin يسمح بذلك في أعلى المستوى غالباً
        return genFunDecl(node, lines);
    }
    if (nodeIs(node, "Let")) {
        let e = genExpr(node["value"]); if (isOk(e) == false) { return e; }
        push(lines, indent + "let " + node["name"] + " = " + e["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "Assign")) {
        let e = genExpr(node["value"]); if (isOk(e) == false) { return e; }
        push(lines, indent + node["name"] + " = " + e["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "ExprStmt")) {
        let e = genExpr(node["expr"]); if (isOk(e) == false) { return e; }
        push(lines, indent + e["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "Return")) {
        let e = genExpr(node["value"]); if (isOk(e) == false) { return e; }
        push(lines, indent + "return " + e["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "Canvas")) {
        let w = genExpr(node["w"]); if (isOk(w) == false) { return w; }
        let h = genExpr(node["h"]); if (isOk(h) == false) { return h; }
        push(lines, indent + "__ctx[\"width\"] = " + w["value"] + "; __ctx[\"height\"] = " + h["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "Fill")) {
        if (node["none"]) {
            push(lines, indent + "__ctx[\"fillNone\"] = true;");
        } else {
            let c = genExpr(node["color"]); if (isOk(c) == false) { return c; }
            push(lines, indent + "__ctx[\"fillNone\"] = false; __ctx[\"fillColor\"] = toString(" + c["value"] + ");");
        }
        return ok(true);
    }
    if (nodeIs(node, "Stroke")) {
        if (node["none"]) {
            push(lines, indent + "__ctx[\"strokeNone\"] = true;");
        } else {
            let c = genExpr(node["color"]); if (isOk(c) == false) { return c; }
            let w = genExpr(node["width"]); if (isOk(w) == false) { return w; }
            push(lines, indent + "__ctx[\"strokeNone\"] = false; __ctx[\"strokeColor\"] = toString(" + c["value"] + "); __ctx[\"strokeWidth\"] = " + w["value"] + ";");
        }
        return ok(true);
    }
    if (nodeIs(node, "Rect")) {
        let x = genExpr(node["x"]); if (isOk(x) == false) { return x; }
        let y = genExpr(node["y"]); if (isOk(y) == false) { return y; }
        let w = genExpr(node["w"]); if (isOk(w) == false) { return w; }
        let h = genExpr(node["h"]); if (isOk(h) == false) { return h; }
        push(lines, indent + "__rect(__ctx, " + x["value"] + ", " + y["value"] + ", " + w["value"] + ", " + h["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "Circle")) {
        let cx = genExpr(node["cx"]); if (isOk(cx) == false) { return cx; }
        let cy = genExpr(node["cy"]); if (isOk(cy) == false) { return cy; }
        let r = genExpr(node["r"]); if (isOk(r) == false) { return r; }
        push(lines, indent + "__circle(__ctx, " + cx["value"] + ", " + cy["value"] + ", " + r["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "Ellipse")) {
        let cx = genExpr(node["cx"]); if (isOk(cx) == false) { return cx; }
        let cy = genExpr(node["cy"]); if (isOk(cy) == false) { return cy; }
        let rx = genExpr(node["rx"]); if (isOk(rx) == false) { return rx; }
        let ry = genExpr(node["ry"]); if (isOk(ry) == false) { return ry; }
        push(lines, indent + "__ellipse(__ctx, " + cx["value"] + ", " + cy["value"] + ", " + rx["value"] + ", " + ry["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "Polygon")) {
        let pts = genExpr(node["points"]); if (isOk(pts) == false) { return pts; }
        push(lines, indent + "__polygon(__ctx, " + pts["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "PathEl")) {
        let d = genExpr(node["d"]); if (isOk(d) == false) { return d; }
        push(lines, indent + "__path(__ctx, " + d["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "Line")) {
        let x1 = genExpr(node["x1"]); if (isOk(x1) == false) { return x1; }
        let y1 = genExpr(node["y1"]); if (isOk(y1) == false) { return y1; }
        let x2 = genExpr(node["x2"]); if (isOk(x2) == false) { return x2; }
        let y2 = genExpr(node["y2"]); if (isOk(y2) == false) { return y2; }
        push(lines, indent + "__line(__ctx, " + x1["value"] + ", " + y1["value"] + ", " + x2["value"] + ", " + y2["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "Text")) {
        let x = genExpr(node["x"]); if (isOk(x) == false) { return x; }
        let y = genExpr(node["y"]); if (isOk(y) == false) { return y; }
        let s = genExpr(node["str"]); if (isOk(s) == false) { return s; }
        push(lines, indent + "__text(__ctx, " + x["value"] + ", " + y["value"] + ", " + s["value"] + ");");
        return ok(true);
    }
    if (nodeIs(node, "PrintDebug")) {
        let v = genExpr(node["value"]); if (isOk(v) == false) { return v; }
        push(lines, indent + "print " + v["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "If")) {
        let c = genExpr(node["cond"]); if (isOk(c) == false) { return c; }
        push(lines, indent + "if (" + c["value"] + ") {");
        let r1 = genBlock(node["thenBody"], lines, indent + "    ");
        if (isOk(r1) == false) { return r1; }
        push(lines, indent + "} else {");
        let r2 = genBlock(node["elseBody"], lines, indent + "    ");
        if (isOk(r2) == false) { return r2; }
        push(lines, indent + "}");
        return ok(true);
    }
    if (nodeIs(node, "While")) {
        let c = genExpr(node["cond"]); if (isOk(c) == false) { return c; }
        push(lines, indent + "while (" + c["value"] + ") {");
        let r1 = genBlock(node["body"], lines, indent + "    ");
        if (isOk(r1) == false) { return r1; }
        push(lines, indent + "}");
        return ok(true);
    }
    if (nodeIs(node, "Group")) {
        let dx = genExpr(node["dx"]); if (isOk(dx) == false) { return dx; }
        let dy = genExpr(node["dy"]); if (isOk(dy) == false) { return dy; }
        push(lines, indent + "__gOpen(__ctx, \"translate(\" + toString(" + dx["value"] + ") + \",\" + toString(" + dy["value"] + ") + \")\");");
        let r1 = genBlock(node["body"], lines, indent);
        if (isOk(r1) == false) { return r1; }
        push(lines, indent + "__gClose(__ctx);");
        return ok(true);
    }
    if (nodeIs(node, "Rotate")) {
        let a = genExpr(node["angle"]); if (isOk(a) == false) { return a; }
        push(lines, indent + "__gOpen(__ctx, \"rotate(\" + toString(" + a["value"] + ") + \")\");");
        let r1 = genBlock(node["body"], lines, indent);
        if (isOk(r1) == false) { return r1; }
        push(lines, indent + "__gClose(__ctx);");
        return ok(true);
    }
    return err(langError("CodeGen", "لا يمكن توليد كود لجملة: " + node["kind"], node["line"]));
}

fun genExpr(node) {
    if (nodeIs(node, "NumberLit")) { return ok(toString(node["value"])); }
    if (nodeIs(node, "StringLit")) { return ok("\"" + node["value"] + "\""); }
    if (nodeIs(node, "Ident")) { return ok(node["name"]); }
    if (nodeIs(node, "ArrayLit")) {
        let parts = [];
        let i = 0;
        while (i < len(node["items"])) {
            let e = genExpr(node["items"][i]); if (isOk(e) == false) { return e; }
            push(parts, e["value"]);
            i = i + 1;
        }
        return ok("[" + join(parts, ", ") + "]");
    }
    if (nodeIs(node, "Call")) {
        let parts = [];
        let i = 0;
        while (i < len(node["args"])) {
            let e = genExpr(node["args"][i]); if (isOk(e) == false) { return e; }
            push(parts, e["value"]);
            i = i + 1;
        }
        return ok(node["name"] + "(" + join(parts, ", ") + ")");
    }
    if (nodeIs(node, "Unary")) {
        let v = genExpr(node["operand"]); if (isOk(v) == false) { return v; }
        return ok("(-" + v["value"] + ")");
    }
    if (nodeIs(node, "Binary")) {
        let l = genExpr(node["left"]); if (isOk(l) == false) { return l; }
        let r = genExpr(node["right"]); if (isOk(r) == false) { return r; }
        return ok("(" + l["value"] + " " + node["op"] + " " + r["value"] + ")");
    }
    return err(langError("CodeGen", "لا يمكن توليد كود لتعبير: " + node["kind"], node["line"]));
}
"""

    val RUN_RIN: String = """
// ============================================================================
//  run.rin — نقطة الدخول الكاملة للغة "Illust": مصدر .illust -> tokens -> AST
//  -> تنفيذ فوري (SVG عبر Interpreter) + توليد كود Rin مستقل (CodeGen).
//
//  الاستخدام من سطر أوامر Rin:
//    rin run run.rin --  path/to/drawing.illust
//  أو عدّل SOURCE_PATH أدناه مباشرة أثناء التطوير داخل المحرر/IDE.
// ============================================================================

@import "./Interpreter.rin";
@import "./CodeGen.rin";

let SOURCE_PATH = "examples/hello.illust";

fun runFile(path) {
    let source = readFile(path);
    let tokens = lex(source);

    let i = 0;
    while (i < len(tokens)) {
        if (isLangError(tokens[i])) {
            print formatLangError(tokens[i]);
            return nil;
        }
        i = i + 1;
    }

    let ast = parse(tokens);
    if (isLangError(ast)) {
        print formatLangError(ast);
        return nil;
    }

    print "── SVG (Interpreter) ──";
    let svg = interpret(ast);
    print svg;

    print "── كود Rin مُولَّد (CodeGen) ──";
    let generated = generate(ast);
    print generated;
    // اختياري: احفظ الناتج وشغّله لاحقاً بأي مفسّر Rin عادي بلا الحاجة لمفسّر Illust
    // writeFile("build/output.rin", generated);
    // اختياري: احفظ SVG مباشرة كملف قابل للعرض في أي متصفح
    // writeFile("build/output.svg", svg);
    return nil;
}

runFile(SOURCE_PATH);
"""

    val SYNTAX_JSON: String = """
{
  "language": "Illust",
  "fileExtension": "illust",
  "lineComment": "//",
  "blockComment": { "start": "/*", "end": "*/" },
  "keywords": ["let", "canvas", "fill", "stroke", "none", "rect", "circle", "line", "text", "if", "else", "while", "group", "print"],
  "operators": ["+", "-", "*", "/", "=", "==", "!=", "<", "<=", ">", ">=", "(", ")", "{", "}", ";", ","],
  "rules": [
    { "name": "comment", "pattern": "//[^\\n]*", "color": "#6A9955" },
    { "name": "string", "pattern": "\"(?:[^\"\\\\]|\\\\.)*\"", "color": "#CE9178" },
    { "name": "number", "pattern": "\\b[0-9]+(?:\\.[0-9]+)?\\b", "color": "#B5CEA8" },
    { "name": "keyword", "pattern": "\\b(let|canvas|fill|stroke|none|rect|circle|line|text|if|else|while|group|print)\\b", "color": "#C586C0" },
    { "name": "identifier", "pattern": "\\b[A-Za-z_][A-Za-z0-9_]*\\b", "color": "#9CDCFE" },
    { "name": "operator", "pattern": "==|!=|<=|>=|[+\\-*/=<>(){};,]", "color": "#D4D4D4" }
  ],
  "note": "هذا الملف يقرأه محرر Rin IDE (CustomLanguageSyntaxLoader.kt) لتلوين ملفات .illust تلقائياً. rules تُطبَّق بالترتيب كتعبيرات نمطية (regex) قياسية."
}
"""

    val README_MD: String = """
# Illust — لغة رسم صغيرة فوق Rin

`illust.rin` لغة مكمّلة لـ RinLang: أوامر رسم نصية بسيطة (canvas/rect/circle/line/text/fill/stroke)
مع متغيرات وشروط وحلقات، مبنية بالكامل فوق `lib/langkit.og.rin` بنفس بنية أي مشروع
"لغة مخصّصة" في المستودع (راجع `docs/custom-languages.md` و`examples/customlang/calc/`).

مسارها المزدوج مطابق لمثال calc:
- **Interpreter.rin** — يُنفّذ AST مباشرة ويُخرج **SVG حقيقي** جاهز للعرض في أي متصفح.
- **CodeGen.rin** — يترجم نفس AST إلى **كود Rin مستقل بذاته** (لا يعتمد وقت التشغيل على
  مفسّر Illust ولا حتى على `langkit`)، يمكن حفظه بـ `writeFile()` وتشغيله لاحقاً بأي
  مفسّر Rin عادي وينتج نفس مخرجات SVG تماماً.

كِلا المسارين اختُبرا فعلياً ببناء مفسّر Rin من مصدر هذا المستودع (`cli/linux`) وتشغيل
`run.rin` عليه — وليس مجرد كود مكتوب نظرياً.

## البنية

```
illust/
├── manifest.json
├── Lexer.rin
├── Parser.rin
├── Interpreter.rin
├── CodeGen.rin
├── run.rin
├── syntax.rinsyntax.json
└── examples/
    └── hello.illust
```

للتثبيت داخل مستودع rinlang: انسخ هذا المجلد إلى `examples/customlang/illust/` تماماً
مثل `examples/customlang/calc/`، أو استخدمه كنقطة بداية عبر IDE أندرويد
(مشروع جديد ← لغة مخصصة جديدة) بنفس آلية `templates/customlang/`.

## الصياغة (Syntax)

```illust
canvas(300, 200);        // تحديد أبعاد اللوحة (اختياري، افتراضياً 400x300)

fill("#eaf6ff");         // لون التعبئة، أو fill(none);
stroke(none);             // بلا حدّ، أو stroke("#8a5a00", 3);  (لون، سُمك)
rect(0, 0, 300, 200);     // x, y, width, height

fill("#ffcc66");
stroke("#8a5a00", 3);
circle(150, 100, 60);     // cx, cy, r

let i = 0;
while (i < 2) {
    let ex = 0 - 20 + i * 40;
    circle(ex, -10, 6);   // إحداثيات نسبية داخل group()
    i = i + 1;
}

group(150, 100) {         // كل الرسم داخل الكتلة يُزاح بمقدار (dx, dy)
    line(-15, 25, 15, 25);
}

if (1 == 1) {
    text(90, 190, "Illust says hi");
}
print("تم بناء الرسمة");  // يُطبع في سجلّ التنفيذ، لا يظهر في SVG
```

### الكلمات المفتاحية
`let`, `canvas`, `fill`, `stroke`, `none`, `rect`, `circle`, `line`, `text`,
`if`, `else`, `while`, `group`, `print`

### التعابير
أرقام، نصوص `"..."`، متغيرات، `+ - * /`، مقارنات `== != < <= > >=`، سالب أحادي `-x`، أقواس.

## التشغيل

```
rin run run.rin --  examples/hello.illust
```

أو عدّل `SOURCE_PATH` داخل `run.rin` مباشرة. يطبع البرنامج:
1. `SVG` الناتج من `Interpreter.rin` (جاهز للحفظ كـ `.svg` وفتحه في أي متصفح).
2. كود Rin كامل مُولَّد من `CodeGen.rin` (جاهز للحفظ كـ `.rin` وتشغيله مستقلاً).

## أفكار للتوسعة لاحقاً
- أشكال إضافية: `polygon(...)`, `ellipse(...)`, مسارات `path("M ... L ...")`.
- دوال معرّفة من المستخدم (`fun`) لإعادة استخدام رسومات مركّبة.
- تدوير/تحجيم داخل `group()` (transform كامل، وليس إزاحة فقط).
- تصدير مباشر عبر `writeFile()` إلى ملف `.svg`/`.rin` من `run.rin`.
"""

    val EXAMPLE_HELLO_ILLUST: String = """
// hello.illust — يعرض كل ميزات v0.2: ellipse/polygon/path/group/rotate/fun+return

canvas(320, 240);

fill("#eaf6ff");
stroke(none);
rect(0, 0, 320, 240);

// وجه بيضاوي بدل الدائرة السابقة
fill("#ffcc66");
stroke("#8a5a00", 3);
ellipse(160, 120, 70, 55);

group(160, 120) {
    fill("#222222");
    stroke(none);

    let i = 0;
    while (i < 2) {
        let ex = 0 - 22 + i * 44;
        circle(ex, -10, 6);
        i = i + 1;
    }

    stroke("#222222", 4);
    fill(none);
    path("M -18 25 Q 0 40 18 25");
}

// دالة قابلة لإعادة الاستخدام: نجمة بسيطة عبر polygon، تُستدعى وتُدار بـ rotate
fun star(cx, cy, r) {
    fill("#ff5566");
    stroke(none);
    let pts = [
        cx,      cy - r,
        cx + r/4, cy - r/4,
        cx + r,  cy,
        cx + r/4, cy + r/4,
        cx,      cy + r,
        cx - r/4, cy + r/4,
        cx - r,  cy,
        cx - r/4, cy - r/4
    ];
    polygon(pts);
    return 0;
}

group(60, 60) {
    rotate(15) {
        star(0, 0, 24);
    }
}

group(260, 60) {
    rotate(-15) {
        star(0, 0, 24);
    }
}

text(95, 225, "Illust v0.2");
print("تم بناء الرسمة الموسّعة");
"""

}
