package com.dlof.rinlang.store.languages

/**
 * نصوص قالب "مشروع لغة مخصصة" مُضمَّنة مباشرة كسلاسل Kotlin خام (نفس نسخة templates/customlang/
 * بالضبط) — بلا حاجة لملفات assets أو تعديل build.gradle. [CustomLanguageProjectScaffolder]
 * يستبدل العناصر النائبة (__LANG_ID__/__LANG_NAME__/__LANG_EXT__/__DEVELOPER__/__LANG_DESCRIPTION__)
 * بقيم المستخدم الفعلية عند إنشاء مشروع جديد. عدِّل هذه الثوابت مع أي تعديل مقابل على القالب
 * الأصلي في templates/customlang/ ليبقى الاثنان متطابقين.
 */
object CustomLanguageTemplates {
    const val LEXER_TEMPLATE = """// ============================================================================
//  Lexer.rin — المرحلة الأولى من لغة "__LANG_NAME__": تحويل النص المصدر إلى tokens
//  المدخل: نص مصدر (string) بلغتك __LANG_NAME__
//  المخرج: مصفوفة tokens، كل واحد { type, value, line } (راجع lib/langkit.og.rin::makeToken)
// ============================================================================

@import "langkit";

// عدّل هذه القائمة لتضيف الكلمات المفتاحية الخاصة بلغتك
fun isKeyword(word) {
    let keywords = ["let", "print", "if", "else"];
    return contains(keywords, word);
}

// نقطة الدخول الرئيسية: تُستدعى من Parser.rin
fun lex(source) {
    let tokens = [];
    let i = 0;
    let line = 1;
    let n = len(source);

    while (i < n) {
        let ch = charAt(source, i);

        // تجاهل الفراغات
        if (isSpaceChar(ch)) {
            i = i + 1;
        } else if (isNewlineChar(ch)) {
            line = line + 1;
            i = i + 1;

        // الأرقام: 123 أو 12.5
        } else if (isDigitChar(ch)) {
            let start = i;
            while (i < n and (isDigitChar(charAt(source, i)) or charAt(source, i) == ".")) {
                i = i + 1;
            }
            push(tokens, makeToken("NUMBER", substr(source, start, i - start), line));

        // المعرّفات والكلمات المفتاحية: name أو let أو print
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

        // النصوص المحاطة بعلامتي اقتباس "..."
        } else if (ch == "\"") {
            let start = i + 1;
            i = i + 1;
            while (i < n and charAt(source, i) != "\"") {
                i = i + 1;
            }
            push(tokens, makeToken("STRING", substr(source, start, i - start), line));
            i = i + 1; // تجاوز علامة الاقتباس الختامية

        // عوامل ورموز مفردة/مزدوجة: + - * / = ( ) ; == إلخ.
        } else if (ch == "=" and i + 1 < n and charAt(source, i + 1) == "=") {
            push(tokens, makeToken("OP", "==", line));
            i = i + 2;
        } else if (contains(["+", "-", "*", "/", "=", "(", ")", "{", "}", ";", "<", ">"], ch)) {
            push(tokens, makeToken("OP", ch, line));
            i = i + 1;

        } else {
            // محرف غير معروف: نسجّله كخطأ لغوي بدل تجاهله بصمت
            push(tokens, langError("Lexer", "محرف غير متوقَّع: '" + ch + "'", line));
            i = i + 1;
        }
    }

    push(tokens, eofToken(line));
    return tokens;
}
"""
    const val PARSER_TEMPLATE = """// ============================================================================
//  Parser.rin — المرحلة الثانية من لغة "__LANG_NAME__": tokens -> شجرة AST
//  محلّل نازل بالتكرار (recursive descent) بسيط. يبني عقد عبر astNode() من langkit.
//  قواعد النحو المضمّنة هنا كمثال (عدّلها لتناسب لغتك):
//    program    -> statement* EOF
//    statement  -> "let" IDENT "=" expr ";"
//                | "print" expr ";"
//                | "if" "(" expr ")" "{" statement* "}" ("else" "{" statement* "}")?
//                | expr ";"
//    expr       -> comparison
//    comparison -> term (("==" | "<" | ">") term)*
//    term       -> factor (("+" | "-") factor)*
//    factor     -> unary (("*" | "/") unary)*
//    unary      -> "-" unary | primary
//    primary    -> NUMBER | STRING | IDENT | "(" expr ")"
// ============================================================================

@import "./Lexer.rin";

// نقطة الدخول: مصفوفة tokens -> { program: [statements...] } أو langError واحد
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

fun parseStatement(tokens, pos) {
    if (pCheckValue(tokens, pos, "KEYWORD", "let")) { return parseLet(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "print")) { return parsePrint(tokens, pos); }
    if (pCheckValue(tokens, pos, "KEYWORD", "if")) { return parseIf(tokens, pos); }
    return parseExprStatement(tokens, pos);
}

fun parseLet(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // استهلاك 'let'
    let nameTok = pPeek(tokens, pos);
    let a2 = pExpect(tokens, pos, "IDENT", "Parser");
    if (isLangError(a2["tok"])) { return { node: a2["tok"], pos: pos }; }
    pos = a2["pos"];
    let a3 = pExpect(tokens, pos, "OP", "Parser"); // '='
    if (isLangError(a3["tok"])) { return { node: a3["tok"], pos: pos }; }
    pos = a3["pos"];
    let r = parseExpr(tokens, pos);
    if (isLangError(r["node"])) { return r; }
    pos = r["pos"];
    let a4 = pExpect(tokens, pos, "OP", "Parser"); // ';'
    pos = a4["pos"];
    return { node: astNode("Let", line, { name: nameTok["value"], value: r["node"] }), pos: pos };
}

fun parsePrint(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // استهلاك 'print'
    let r = parseExpr(tokens, pos);
    if (isLangError(r["node"])) { return r; }
    pos = r["pos"];
    let a2 = pExpect(tokens, pos, "OP", "Parser"); // ';'
    pos = a2["pos"];
    return { node: astNode("Print", line, { value: r["node"] }), pos: pos };
}

fun parseIf(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    let a1 = pAdvance(tokens, pos); pos = a1["pos"]; // 'if'
    let a2 = pAdvance(tokens, pos); pos = a2["pos"]; // '('
    let cond = parseExpr(tokens, pos);
    if (isLangError(cond["node"])) { return cond; }
    pos = cond["pos"];
    let a3 = pAdvance(tokens, pos); pos = a3["pos"]; // ')'
    let a4 = pAdvance(tokens, pos); pos = a4["pos"]; // '{'
    let thenBody = [];
    while (pCheckValue(tokens, pos, "OP", "}") == false and pAtEnd(tokens, pos) == false) {
        let s = parseStatement(tokens, pos);
        if (isLangError(s["node"])) { return s; }
        push(thenBody, s["node"]);
        pos = s["pos"];
    }
    let a5 = pAdvance(tokens, pos); pos = a5["pos"]; // '}'

    let elseBody = [];
    if (pCheckValue(tokens, pos, "KEYWORD", "else")) {
        let a6 = pAdvance(tokens, pos); pos = a6["pos"]; // 'else'
        let a7 = pAdvance(tokens, pos); pos = a7["pos"]; // '{'
        while (pCheckValue(tokens, pos, "OP", "}") == false and pAtEnd(tokens, pos) == false) {
            let s = parseStatement(tokens, pos);
            if (isLangError(s["node"])) { return s; }
            push(elseBody, s["node"]);
            pos = s["pos"];
        }
        let a8 = pAdvance(tokens, pos); pos = a8["pos"]; // '}'
    }

    return { node: astNode("If", line, { cond: cond["node"], thenBody: thenBody, elseBody: elseBody }), pos: pos };
}

fun parseExprStatement(tokens, pos) {
    let r = parseExpr(tokens, pos);
    if (isLangError(r["node"])) { return r; }
    pos = r["pos"];
    let a = pExpect(tokens, pos, "OP", "Parser"); // ';'
    pos = a["pos"];
    return { node: r["node"], pos: pos };
}

fun parseExpr(tokens, pos) { return parseComparison(tokens, pos); }

fun parseComparison(tokens, pos) {
    let left = parseTerm(tokens, pos);
    if (isLangError(left["node"])) { return left; }
    pos = left["pos"];
    let node = left["node"];
    while (pCheckValue(tokens, pos, "OP", "==") or pCheckValue(tokens, pos, "OP", "<") or pCheckValue(tokens, pos, "OP", ">")) {
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
        return { node: astNode("Ident", tok["line"], { name: tok["value"] }), pos: a["pos"] };
    }
    if (tokIsValue(tok, "OP", "(")) {
        let a = pAdvance(tokens, pos);
        let inner = parseExpr(tokens, a["pos"]);
        if (isLangError(inner["node"])) { return inner; }
        let a2 = pAdvance(tokens, inner["pos"]); // ')'
        return { node: inner["node"], pos: a2["pos"] };
    }
    return { node: langError("Parser", "تعبير غير متوقَّع عند '" + toString(tok["value"]) + "'", tok["line"]), pos: pos };
}
"""
    const val INTERPRETER_TEMPLATE = """// ============================================================================
//  Interpreter.rin — المرحلة الثالثة من لغة "__LANG_NAME__": تنفيذ شجرة AST مباشرة
//  (بديل CodeGen.rin الذي يولّد كوداً بدل تنفيذه فوراً — يمكن استخدام الاثنين معاً)
//
//  ملاحظة تصميم مهمة: has()/keys() في Rin يفشلان إن مُرِّرت لهما قيمة ليست خريطة، وقيمة
//  تعبير ناجح (evalExpr) غالباً رقم أو نص خام — لذا evalExpr/execStatement/execBlock كلها
//  تُعيد دوماً Result عبر ok(value)/err(langErrorObj) من langkit.og.rin (خريطة دوماً، آمنة
//  لفحص isOk() دون أي احتمال فشل)، لا القيمة الخام مباشرة ولا isLangError على نتيجتها.
// ============================================================================

@import "./Parser.rin";

// ينفّذ برنامجاً كاملاً (نتيجة parse())، ويُعيد نص المخرجات المُجمَّع من كل "print"
fun interpret(program) {
    if (isLangError(program)) { return formatLangError(program); }
    let env = {};
    let output = [];
    let r = execBlock(program["body"], env, output);
    if (isOk(r) == false) { return formatLangError(resultError(r)); }
    return join(output, "\n");
}

// ينفّذ قائمة statements بالترتيب فوق env، ويضيف نواتج "print" إلى output
fun execBlock(statements, env, output) {
    let i = 0;
    while (i < len(statements)) {
        let r = execStatement(statements[i], env, output);
        if (isOk(r) == false) { return r; }
        i = i + 1;
    }
    return ok(env);
}

fun execStatement(node, env, output) {
    if (nodeIs(node, "Let")) {
        let v = evalExpr(node["value"], env);
        if (isOk(v) == false) { return v; }
        env[node["name"]] = v["value"];
        return ok(env);
    }
    if (nodeIs(node, "Print")) {
        let v = evalExpr(node["value"], env);
        if (isOk(v) == false) { return v; }
        push(output, toString(v["value"]));
        return ok(env);
    }
    if (nodeIs(node, "If")) {
        let c = evalExpr(node["cond"], env);
        if (isOk(c) == false) { return c; }
        if (toBool(c["value"])) {
            return execBlock(node["thenBody"], env, output);
        } else {
            return execBlock(node["elseBody"], env, output);
        }
    }
    // تعبير بمفرده كـstatement (مثال: نداء مستقبلي بلا قيمة راجعة تُستخدم)
    let v = evalExpr(node, env);
    if (isOk(v) == false) { return v; }
    return ok(env);
}

// يُعيد دوماً ok(قيمة) أو err(langError) — راجع ملاحظة التصميم أعلى الملف
fun evalExpr(node, env) {
    if (nodeIs(node, "NumberLit")) { return ok(node["value"]); }
    if (nodeIs(node, "StringLit")) { return ok(node["value"]); }
    if (nodeIs(node, "Ident")) {
        if (has(env, node["name"]) == false) {
            return err(langError("Interpreter", "متغيّر غير معرَّف: " + node["name"], node["line"]));
        }
        return ok(env[node["name"]]);
    }
    if (nodeIs(node, "Unary")) {
        let v = evalExpr(node["operand"], env);
        if (isOk(v) == false) { return v; }
        return ok(0 - v["value"]);
    }
    if (nodeIs(node, "Binary")) {
        let l = evalExpr(node["left"], env);
        if (isOk(l) == false) { return l; }
        let r = evalExpr(node["right"], env);
        if (isOk(r) == false) { return r; }
        let lv = l["value"];
        let rv = r["value"];
        let op = node["op"];
        if (op == "+") { return ok(lv + rv); }
        if (op == "-") { return ok(lv - rv); }
        if (op == "*") { return ok(lv * rv); }
        if (op == "/") { return ok(lv / rv); }
        if (op == "==") { return ok(lv == rv); }
        if (op == "<") { return ok(lv < rv); }
        if (op == ">") { return ok(lv > rv); }
        return err(langError("Interpreter", "عامل غير معروف: " + op, node["line"]));
    }
    return err(langError("Interpreter", "نوع عقدة غير مدعوم: " + node["kind"], node["line"]));
}
"""
    const val CODEGEN_TEMPLATE = """// ============================================================================
//  CodeGen.rin — مسار بديل لـ Interpreter.rin: بدل تنفيذ AST مباشرة، يولّد نص كود
//  Rin حقيقي مكافئ، يمكن حفظه بـ writeFile() ثم تشغيله بأي مفسّر Rin عادي.
//  هذا يجعل لغتك "__LANG_NAME__" لغة تُترجَم (compiled) وليس فقط تُفسَّر (interpreted).
//
//  نفس ملاحظة التصميم في Interpreter.rin: genExpr يُعيد دوماً ok(نص)/err(langError)،
//  لأن toString(node["value"]) قد يكون نصاً خاماً غير خريطة (has() يفشل على غير الخرائط).
// ============================================================================

@import "./Parser.rin";

// يولّد نص برنامج Rin كامل من AST؛ يُعيد نصاً جاهزاً للحفظ/التشغيل، أو نص خطأ منسَّق
fun generate(program) {
    if (isLangError(program)) { return formatLangError(program); }
    let lines = [];
    push(lines, "// ---- مُولَّد تلقائياً من __LANG_NAME__ عبر CodeGen.rin ----");
    let r = genBlock(program["body"], lines, "");
    if (isOk(r) == false) { return formatLangError(resultError(r)); }
    return join(lines, "\n");
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
    if (nodeIs(node, "Let")) {
        let e = genExpr(node["value"]);
        if (isOk(e) == false) { return e; }
        push(lines, indent + "let " + node["name"] + " = " + e["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "Print")) {
        let e = genExpr(node["value"]);
        if (isOk(e) == false) { return e; }
        push(lines, indent + "print " + e["value"] + ";");
        return ok(true);
    }
    if (nodeIs(node, "If")) {
        let c = genExpr(node["cond"]);
        if (isOk(c) == false) { return c; }
        push(lines, indent + "if (" + c["value"] + ") {");
        let r1 = genBlock(node["thenBody"], lines, indent + "    ");
        if (isOk(r1) == false) { return r1; }
        push(lines, indent + "} else {");
        let r2 = genBlock(node["elseBody"], lines, indent + "    ");
        if (isOk(r2) == false) { return r2; }
        push(lines, indent + "}");
        return ok(true);
    }
    let e = genExpr(node);
    if (isOk(e) == false) { return e; }
    push(lines, indent + e["value"] + ";");
    return ok(true);
}

// يبني تمثيل نص Rin لتعبير واحد (بدون ; ختامية — يُضيفها المستدعي). يُعيد ok(نص)/err(...)
fun genExpr(node) {
    if (nodeIs(node, "NumberLit")) { return ok(toString(node["value"])); }
    if (nodeIs(node, "StringLit")) { return ok("\"" + node["value"] + "\""); }
    if (nodeIs(node, "Ident")) { return ok(node["name"]); }
    if (nodeIs(node, "Unary")) {
        let v = genExpr(node["operand"]);
        if (isOk(v) == false) { return v; }
        return ok("(-" + v["value"] + ")");
    }
    if (nodeIs(node, "Binary")) {
        let l = genExpr(node["left"]);
        if (isOk(l) == false) { return l; }
        let r = genExpr(node["right"]);
        if (isOk(r) == false) { return r; }
        return ok("(" + l["value"] + " " + node["op"] + " " + r["value"] + ")");
    }
    return err(langError("CodeGen", "لا يمكن توليد كود لعقدة: " + node["kind"], node["line"]));
}
"""
    const val RUN_TEMPLATE = """// ============================================================================
//  run.rin — نقطة الدخول الكاملة للغة "__LANG_NAME__": هذا هو "المترجم/المفسّر" الفعلي
//  الذي يشغّله المستخدم. يوصل السلسلة الكاملة: مصدر .{{__LANG_EXT__}} -> tokens -> AST
//  -> تنفيذ فوري (Interpreter) و/أو توليد كود Rin (CodeGen).
//
//  الاستخدام من سطر أوامر Rin (rinc/rin_run) أو من داخل IDE أندرويد:
//    rin run.rin --  path/to/program.__LANG_EXT__
//  أو عدّل SOURCE_PATH أدناه مباشرة أثناء التطوير داخل المحرر.
// ============================================================================

@import "./Interpreter.rin";
@import "./CodeGen.rin";

// عدّل هذا المسار عند التطوير المباشر داخل IDE (أو مرّره من واجهة "تشغيل اللغة المخصصة")
let SOURCE_PATH = "examples/hello.__LANG_EXT__";

fun runFile(path) {
    let source = readFile(path);
    let tokens = lex(source);

    // إن احتوت tokens على خطأ لفظي، أوقف قبل حتى محاولة التحليل التركيبي
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

    print "── تنفيذ مباشر (Interpreter) ──";
    print interpret(ast);

    print "── كود Rin مُولَّد (CodeGen) ──";
    let generated = generate(ast);
    print generated;
    // اختياري: احفظ الكود المُولَّد وشغّله لاحقاً بأي مفسّر Rin عادي
    // writeFile("build/output.rin", generated);
    return nil;
}

runFile(SOURCE_PATH);
"""
    const val SYNTAX_TEMPLATE = """{
  "language": "__LANG_NAME__",
  "fileExtension": "__LANG_EXT__",
  "lineComment": "//",
  "blockComment": { "start": "/*", "end": "*/" },
  "keywords": ["let", "print", "if", "else"],
  "operators": ["+", "-", "*", "/", "=", "==", "<", ">", "(", ")", "{", "}", ";"],
  "rules": [
    { "name": "comment", "pattern": "//[^\\n]*", "color": "#6A9955" },
    { "name": "string", "pattern": "\"(?:[^\"\\\\]|\\\\.)*\"", "color": "#CE9178" },
    { "name": "number", "pattern": "\\b[0-9]+(?:\\.[0-9]+)?\\b", "color": "#B5CEA8" },
    { "name": "keyword", "pattern": "\\b(let|print|if|else)\\b", "color": "#C586C0" },
    { "name": "identifier", "pattern": "\\b[A-Za-z_][A-Za-z0-9_]*\\b", "color": "#9CDCFE" },
    { "name": "operator", "pattern": "==|[+\\-*/=<>(){};]", "color": "#D4D4D4" }
  ],
  "note": "هذا الملف يقرأه محرر Rin IDE (CustomLanguageSyntaxLoader.kt) لتلوين ملفات .__LANG_EXT__ تلقائياً. rules تُطبَّق بالترتيب كتعبيرات نمطية (regex) قياسية."
}
"""
    const val README_TEMPLATE = """# قالب مشروع لغة مخصصة (Custom Language Project) فوق RinLang

هذا القالب يُنشئ **لغة برمجة حقيقية** خاصة بك، بنفس بنية أي لغة برمجة احترافية:
محلّل لفظي (Lexer) ← محلّل تركيبي (Parser) ← مفسّر (Interpreter) و/أو مولّد كود (CodeGen)،
كل مرحلة في ملفها الخاص — وليس كل شيء في ملف واحد.

## البنية

```
اسم_لغتك/
├── manifest.json          # هوية اللغة: id/name/version/developer/fileExtension...
├── Lexer.rin              # نص المصدر -> tokens
├── Parser.rin              # tokens -> شجرة AST
├── Interpreter.rin         # ينفّذ AST مباشرة ويطبع النتائج
├── CodeGen.rin              # (اختياري) يترجم AST إلى كود Rin حقيقي قابل للحفظ والتشغيل
├── run.rin                 # نقطة التشغيل: يربط كل ما سبق ويشغّل ملف مصدر فعلي
├── syntax.rinsyntax.json   # قواعد تلوين الصياغة في المحرر (Syntax Highlighting)
└── examples/
    └── hello.__LANG_EXT__  # برنامج تجريبي بلغتك الجديدة
```

## كيف تبني لغتك الخاصة

1. أنشئ مشروعاً جديداً من داخل IDE أندرويد: **مشروع جديد ← لغة مخصصة جديدة** (يستخدم
   `CustomLanguageProjectScaffolder.kt` الذي ينسخ هذا القالب ويستبدل `__LANG_ID__` /
   `__LANG_NAME__` / `__LANG_EXT__` / `__DEVELOPER__` بقيمك.
2. عدّل **Lexer.rin**: أضف/غيّر الكلمات المفتاحية والرموز التي تفهمها لغتك.
3. عدّل **Parser.rin**: أضف قواعد نحوية جديدة (جمل/تعبيرات) حسب تصميم لغتك.
4. عدّل **Interpreter.rin** (و/أو **CodeGen.rin**): أضف معنى تنفيذياً لكل عقدة AST جديدة.
5. عدّل **syntax.rinsyntax.json** لتلوين الكلمات المفتاحية والرموز الجديدة في المحرر.
6. شغّل **run.rin** لتجربة لغتك على ملف من `examples/`.

## من مكتبة لأداة لغة رسمية

- `lib/langkit.og.rin` يوفّر اللبنات المشتركة (تصنيف محارف، Token/AST، مؤشّر Parser، أخطاء
  موحّدة) التي يعتمد عليها هذا القالب — استوردها في أي ملف من ملفات لغتك.
- بمجرد أن تعمل لغتك، احزمها ونشرها عبر **متجر إضافات Rin** بنوع "لغة" (`language`) —
  بالضبط بنفس مسار نشر أي إضافة (`PublishExtensionActivity`) — فتصبح متاحة لكل مستخدمي
  Rin للتثبيت والاستخدام مباشرة. راجع `docs/custom-languages.md` لشرح كيف تُعتمَد لغتك
  "رسمية" (badge "official") بعد المراجعة.
"""
}
