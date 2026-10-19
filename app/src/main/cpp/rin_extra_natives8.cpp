// ============================================================================
//  rin_extra_natives8.cpp — مفهوم `token`: الرمز اللغوي كقيمة من الدرجة الأولى في Rin
// ----------------------------------------------------------------------------
//  دفعة ثامنة فوق rin_extra_natives*.cpp دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra8() وتُضمَّن (#include) في نهاية rin_interpreter.cpp
//  (كالدفعة السابعة)، فتدخل كل أهداف البناء (APK / CLI / WASM / CI) بلا أي تعديل في ملفات البناء.
//
//  الموجود قبلها (يبقى كما هو حرفياً، لا تغيير في ناتجه):
//    tokens(src)      -> [{type, lexeme, line}]      |  tokenType(src) -> نوع أول رمز
//
//  الجديد (الشرح الكامل والأمثلة: docs/TOKEN.md) — كلها تعمل على rin::Lexer الحقيقي نفسه الذي
//  يراه المحلل النحوي، فلا قائمة موازية قد تنحرف عن اللغة:
//    tokenList(src[, opts])      رموز كاملة التفاصيل: {index,type,category,lexeme,line,col,endCol[,value]}
//                                opts: {eof:true, only:"keyword"|["IDENT","literal"], skip:"punctuation"}
//    tokenCount(src[, only])     عدد الرموز (بلا EOF) — اختيارياً لنوع/فئة/مصفوفة منها
//    tokenStats(src)             {total, lines, identifiers, byType:{}, byCategory:{}}
//    tokenAt(src, line, col)     الرمز الذي يغطّي الموضع (أو nil) — للمحرّرات وrefactoring
//    tokenLines(src)             الرموز مجمّعة بالسطر: [{line, count, tokens:[...]}]
//    tokenCheck(src)             فحص بلا استثناء: {ok:true,count} | {ok:false,error,line,col}
//    tokenDump(src)              جدول نصي جاهز للطباعة (للتعلّم والتصحيح)
//    tokenCategory(type)         فئة نوع الرمز: keyword|literal|identifier|operator|punctuation|special (أو nil)
//    tokenKinds([category])      كل أسماء أنواع الرموز (اختيارياً ضمن فئة واحدة)
//    tokenKeywords()             كل الكلمات المحجوزة الفعلية (مرتّبة)
//    tokenIsKeyword(word)        هل الكلمة محجوزة؟
//
//  القواعد المشتركة:
//    * كل الدوال نقية: لا تُعدِّل مدخلاتها ولا تُنفّذ الشيفرة المُحلَّلة أبداً (تحليل لغوي فقط).
//    * خطأ لغوي في المصدر (نص غير مُغلَق...) يرمي خطأً عادياً بنفس أسلوب tokens()؛ أما tokenCheck
//      فتُرجع الخطأ كقيمة، لتفحص مدخل المستخدم بلا try.
//    * الأسماء مسطّحة عمداً (tokenList لا token.list): `token` اسم متغيّر شائع جداً في شيفرة
//      المستخدم (كلمات مرور/رموز دخول)، فلا نُدخل مساحة أسماء تتقاطع معه.
// ============================================================================
#include "rin_interpreter.h"
#include "rin_lexer.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace rin {
namespace extra8 {

using Args = std::vector<Value>;

// ------------------------------------------------------------------ الفئات
// كل أنواع الرموز بترتيب التعريف في TokenType (rin_common.h). أي نوع يُضاف مستقبلاً هناك ولا
// يُضاف هنا يظهر في tokenKinds كـ "UNKNOWN" بدل أن يختفي بصمت — انظر tokenTypeName.
static const std::vector<TokenType>& allTypes8() {
    static const std::vector<TokenType> v = {
        TokenType::NUMBER, TokenType::STRING, TokenType::IDENT,
        TokenType::LET, TokenType::PRINT, TokenType::IF, TokenType::ELSE, TokenType::WHILE, TokenType::FUN,
        TokenType::RETURN, TokenType::TRUE, TokenType::FALSE, TokenType::NIL, TokenType::AND, TokenType::OR,
        TokenType::BREAK, TokenType::CONTINUE, TokenType::RINOPEN, TokenType::FOR,
        TokenType::TEXT, TokenType::CONTAINER, TokenType::CONTAINERS, TokenType::GROUP, TokenType::VOLUME,
        TokenType::SECTION, TokenType::TRANSLATIONS, TokenType::TRANSLATION, TokenType::LINK, TokenType::TYING,
        TokenType::MERGE, TokenType::INSTALLATION, TokenType::SIMPLIFIED, TokenType::SAVE, TokenType::FILE_KW,
        TokenType::END, TokenType::PIPE_KW,
        TokenType::PLUS, TokenType::MINUS, TokenType::STAR, TokenType::SLASH, TokenType::PERCENT,
        TokenType::EQUAL, TokenType::EQUAL_EQUAL, TokenType::BANG, TokenType::BANG_EQUAL,
        TokenType::NOT_SAME, TokenType::COPY_OF,
        TokenType::LESS, TokenType::LESS_EQUAL, TokenType::GREATER, TokenType::GREATER_EQUAL,
        TokenType::LPAREN, TokenType::RPAREN, TokenType::LBRACE, TokenType::RBRACE,
        TokenType::LBRACKET, TokenType::RBRACKET, TokenType::COLON, TokenType::COMMA, TokenType::SEMICOLON,
        TokenType::AT, TokenType::DOT, TokenType::PIPE, TokenType::QUESTION,
        TokenType::END_OF_FILE, TokenType::ERROR};
    return v;
}

static const char* categoryOf8(TokenType t) {
    switch (t) {
        case TokenType::NUMBER: case TokenType::STRING:
        case TokenType::TRUE: case TokenType::FALSE: case TokenType::NIL:
            return "literal";
        case TokenType::IDENT:
            return "identifier";
        case TokenType::PLUS: case TokenType::MINUS: case TokenType::STAR: case TokenType::SLASH:
        case TokenType::PERCENT: case TokenType::EQUAL: case TokenType::EQUAL_EQUAL: case TokenType::BANG:
        case TokenType::BANG_EQUAL: case TokenType::NOT_SAME: case TokenType::COPY_OF: case TokenType::LESS:
        case TokenType::LESS_EQUAL: case TokenType::GREATER: case TokenType::GREATER_EQUAL:
        case TokenType::PIPE: case TokenType::QUESTION:
            return "operator";
        case TokenType::LPAREN: case TokenType::RPAREN: case TokenType::LBRACE: case TokenType::RBRACE:
        case TokenType::LBRACKET: case TokenType::RBRACKET: case TokenType::COLON: case TokenType::COMMA:
        case TokenType::SEMICOLON: case TokenType::AT: case TokenType::DOT:
            return "punctuation";
        case TokenType::END_OF_FILE: case TokenType::ERROR:
            return "special";
        default:
            return "keyword";
    }
}

static std::string upper8(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
static std::string lower8(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ------------------------------------------------------------------ تشغيل Lexer
// رسالة خطأ Lexer قد تحمل بايتاً يتيماً من محرف متعدد البايتات (مثلاً أول بايت من حرف عربي خارج نص)،
// وهو UTF-8 غير صالح يفسد الطباعة/JSON لدى المستهلك. نُحوّل أي بايت غير صالح إلى \xHH مقروء.
static std::string safeUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        size_t n = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        bool ok = n > 0 && i + n <= in.size();
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        if (ok) { out.append(in, i, n); i += n; }
        else { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", c); out += b; ++i; }
    }
    return out;
}

struct LexFail8 { std::string message; int line = 0; int col = 0; };

// يشغّل rin::Lexer؛ عند خطأ لغوي يملأ fail ويُرجع false (الاستثناء لا يخرج من هنا).
static bool runLexer8(const std::string& src, std::vector<Token>& out, LexFail8& fail) {
    try {
        Lexer lexer(src);
        out = lexer.scanTokens();
        return true;
    } catch (RinError& e) {
        fail.message = safeUtf8(e.message);
        fail.line = e.line;
        fail.col = e.diagnostic ? e.diagnostic->location.startCol : 0;
        return false;
    }
}

// نسخة ترمي بنفس أسلوب natives["tokens"] (E0035 + اسم الدالة).
static std::vector<Token> lexOrThrow8(const std::string& src, const char* fn, int line) {
    std::vector<Token> toks;
    LexFail8 f;
    if (!runLexer8(src, toks, f))
        throw diagErr(diag::Code::E0035_RuntimeError, line, std::string("'") + fn + "': " + f.message);
    return toks;
}

// ------------------------------------------------------------------ المرشّحات (only / skip)
// كل عنصر يطابق اسم نوع (IDENT, LET, PLUS...) أو اسم فئة (keyword, operator...) دون حساسية لحالة الأحرف.
struct Filter8 {
    std::set<std::string> names; // مُوحَّدة: أنواع بحروف كبيرة، فئات بحروف صغيرة
    bool empty() const { return names.empty(); }
    bool matches(TokenType t) const {
        return names.count(tokenTypeName(t)) > 0 || names.count(categoryOf8(t)) > 0;
    }
};

static Filter8 toFilter8(const Value& v, const char* fn, const char* what, int line) {
    Filter8 f;
    auto add = [&](const Value& e) {
        if (e.type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line,
                          std::string("'") + fn + "': '" + what + "' expects a type/category name or an array of them but got " + e.typeName());
        const std::string up = upper8(e.str);
        const std::string lo = lower8(e.str);
        bool known = false;
        for (TokenType t : allTypes8()) {
            if (tokenTypeName(t) == up || std::string(categoryOf8(t)) == lo) { known = true; break; }
        }
        if (!known)
            throw diagErr(diag::Code::E0007_InvalidArguments, line,
                          std::string("'") + fn + "': unknown token type/category '" + e.str +
                              "' (see tokenKinds() and tokenCategory())");
        f.names.insert(up);
        f.names.insert(lo);
    };
    if (v.type == Value::Type::NIL) return f;
    if (v.type == Value::Type::ARRAY) { for (const auto& e : *v.array) add(e); }
    else add(v);
    return f;
}

// ------------------------------------------------------------------ بناء القيم
static Value S8(const std::string& s) { return Value::string(s); }
static Value N8(double n) { return Value::num(n); }

static Value tokenMap8(const Token& t, size_t index) {
    auto m = std::make_shared<MapData>();
    m->push_back({S8("index"), N8(static_cast<double>(index))});
    m->push_back({S8("type"), S8(tokenTypeName(t.type))});
    m->push_back({S8("category"), S8(categoryOf8(t.type))});
    m->push_back({S8("lexeme"), S8(t.lexeme)});
    m->push_back({S8("line"), N8(static_cast<double>(t.line))});
    m->push_back({S8("col"), N8(static_cast<double>(t.col))});
    m->push_back({S8("endCol"), N8(static_cast<double>(t.endCol))});
    if (t.type == TokenType::NUMBER) m->push_back({S8("value"), N8(t.number)});
    return Value::makeMap(m);
}

static const Value* mapGet8(const Value& m, const char* key) {
    if (m.type != Value::Type::MAP || !m.map) return nullptr;
    return m.map->findValue(Value::string(key));
}

static std::string asStr8(const Value& v, const char* fn, int line) {
    if (v.type != Value::Type::STRING)
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      std::string("'") + fn + "' expects a string but got " + v.typeName());
    return v.str;
}

static int asInt8(const Value& v, const char* fn, const char* what, int line) {
    if (v.type != Value::Type::NUMBER)
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      std::string("'") + fn + "': '" + what + "' expects a number but got " + v.typeName());
    return static_cast<int>(v.number);
}

static void bump8(std::map<std::string, int>& m, const std::string& k) { ++m[k]; }

static Value countsToMap8(const std::vector<std::pair<std::string, int>>& v) {
    auto m = std::make_shared<MapData>();
    for (const auto& kv : v) m->push_back({S8(kv.first), N8(kv.second)});
    return Value::makeMap(m);
}

// الرموز بعد استبعاد EOF (ما لم يُطلب) وتطبيق only/skip. تُرجع أزواج (الفهرس الأصلي، الرمز).
static std::vector<std::pair<size_t, const Token*>> selectTokens8(const std::vector<Token>& toks, bool withEof,
                                                                 const Filter8& only, const Filter8& skip) {
    std::vector<std::pair<size_t, const Token*>> out;
    for (size_t i = 0; i < toks.size(); ++i) {
        const Token& t = toks[i];
        if (!withEof && t.type == TokenType::END_OF_FILE) continue;
        if (!only.empty() && !only.matches(t.type)) continue;
        if (!skip.empty() && skip.matches(t.type)) continue;
        out.push_back({i, &t});
    }
    return out;
}

} // namespace extra8

void Interpreter::registerNativesExtra8() {
    using namespace extra8;

    // tokenList(src[, opts]) -> [{index,type,category,lexeme,line,col,endCol[,value]}]
    natives["tokenList"] = [](Args& a, int line) -> Value {
        expectArgsRange("tokenList", a, 1, 2, line);
        const std::string src = asStr8(a[0], "tokenList", line);
        bool withEof = false;
        Filter8 only, skip;
        if (a.size() == 2 && a[1].type != Value::Type::NIL) {
            if (a[1].type != Value::Type::MAP)
                throw diagErr(diag::Code::E0004_InvalidType, line,
                              "'tokenList' expects options as a map like {eof:true, only:\"keyword\", skip:\"punctuation\"} but got " + a[1].typeName());
            if (const Value* e = mapGet8(a[1], "eof")) withEof = e->isTruthy();
            if (const Value* o = mapGet8(a[1], "only")) only = toFilter8(*o, "tokenList", "only", line);
            if (const Value* s = mapGet8(a[1], "skip")) skip = toFilter8(*s, "tokenList", "skip", line);
        }
        const auto toks = lexOrThrow8(src, "tokenList", line);
        auto arr = std::make_shared<ArrayData>();
        for (const auto& p : selectTokens8(toks, withEof, only, skip)) arr->push_back(tokenMap8(*p.second, p.first));
        return Value::makeArray(arr);
    };

    // tokenCount(src[, only]) -> عدد الرموز (بلا EOF)
    natives["tokenCount"] = [](Args& a, int line) -> Value {
        expectArgsRange("tokenCount", a, 1, 2, line);
        const std::string src = asStr8(a[0], "tokenCount", line);
        Filter8 only;
        if (a.size() == 2) only = toFilter8(a[1], "tokenCount", "only", line);
        const auto toks = lexOrThrow8(src, "tokenCount", line);
        return N8(static_cast<double>(selectTokens8(toks, false, only, Filter8{}).size()));
    };

    // tokenStats(src) -> {total, lines, identifiers, byType, byCategory}
    natives["tokenStats"] = [](Args& a, int line) -> Value {
        expectArgs("tokenStats", a, 1, line);
        const std::string src = asStr8(a[0], "tokenStats", line);
        const auto toks = lexOrThrow8(src, "tokenStats", line);
        std::map<std::string, int> byType, byCat;
        std::set<std::string> idents;
        std::set<int> lines;
        int total = 0;
        for (const auto& t : toks) {
            if (t.type == TokenType::END_OF_FILE) continue;
            ++total;
            bump8(byType, tokenTypeName(t.type));
            bump8(byCat, categoryOf8(t.type));
            lines.insert(t.line);
            if (t.type == TokenType::IDENT) idents.insert(t.lexeme);
        }
        // ترتيب الإخراج حتمي: أنواع بترتيب TokenType، فئات بترتيب ثابت.
        std::vector<std::pair<std::string, int>> typeOut, catOut;
        for (TokenType t : allTypes8()) {
            auto it = byType.find(tokenTypeName(t));
            if (it != byType.end()) typeOut.push_back({it->first, it->second});
        }
        for (const char* c : {"keyword", "literal", "identifier", "operator", "punctuation", "special"}) {
            auto it = byCat.find(c);
            if (it != byCat.end()) catOut.push_back({it->first, it->second});
        }
        auto m = std::make_shared<MapData>();
        m->push_back({S8("total"), N8(total)});
        m->push_back({S8("lines"), N8(static_cast<double>(lines.size()))});
        m->push_back({S8("identifiers"), N8(static_cast<double>(idents.size()))});
        m->push_back({S8("byType"), countsToMap8(typeOut)});
        m->push_back({S8("byCategory"), countsToMap8(catOut)});
        return Value::makeMap(m);
    };

    // tokenAt(src, line, col) -> الرمز الذي يغطّي الموضع (1-based) أو nil
    natives["tokenAt"] = [](Args& a, int line) -> Value {
        expectArgs("tokenAt", a, 3, line);
        const std::string src = asStr8(a[0], "tokenAt", line);
        const int ln = asInt8(a[1], "tokenAt", "line", line);
        const int col = asInt8(a[2], "tokenAt", "col", line);
        const auto toks = lexOrThrow8(src, "tokenAt", line);
        for (size_t i = 0; i < toks.size(); ++i) {
            const Token& t = toks[i];
            if (t.type == TokenType::END_OF_FILE) continue;
            if (t.line == ln && col >= t.col && col < t.endCol) return tokenMap8(t, i);
        }
        return Value::nil();
    };

    // tokenLines(src) -> [{line, count, tokens:[...]}] مجمّعة بالسطر (الأسطر الفارغة من الرموز لا تظهر)
    natives["tokenLines"] = [](Args& a, int line) -> Value {
        expectArgs("tokenLines", a, 1, line);
        const std::string src = asStr8(a[0], "tokenLines", line);
        const auto toks = lexOrThrow8(src, "tokenLines", line);
        auto out = std::make_shared<ArrayData>();
        std::shared_ptr<ArrayData> cur;
        int curLine = -1;
        auto flush = [&]() {
            if (!cur) return;
            auto m = std::make_shared<MapData>();
            m->push_back({S8("line"), N8(curLine)});
            m->push_back({S8("count"), N8(static_cast<double>(cur->size()))});
            m->push_back({S8("tokens"), Value::makeArray(cur)});
            out->push_back(Value::makeMap(m));
            cur.reset();
        };
        for (size_t i = 0; i < toks.size(); ++i) {
            const Token& t = toks[i];
            if (t.type == TokenType::END_OF_FILE) continue;
            if (!cur || t.line != curLine) { flush(); cur = std::make_shared<ArrayData>(); curLine = t.line; }
            cur->push_back(tokenMap8(t, i));
        }
        flush();
        return Value::makeArray(out);
    };

    // tokenCheck(src) -> {ok:true,count} | {ok:false,error,line,col} — بلا استثناء
    natives["tokenCheck"] = [](Args& a, int line) -> Value {
        expectArgs("tokenCheck", a, 1, line);
        const std::string src = asStr8(a[0], "tokenCheck", line);
        std::vector<Token> toks;
        LexFail8 f;
        auto m = std::make_shared<MapData>();
        if (runLexer8(src, toks, f)) {
            size_t n = 0;
            for (const auto& t : toks) if (t.type != TokenType::END_OF_FILE) ++n;
            m->push_back({S8("ok"), Value::boolean_(true)});
            m->push_back({S8("count"), N8(static_cast<double>(n))});
        } else {
            m->push_back({S8("ok"), Value::boolean_(false)});
            m->push_back({S8("error"), S8(f.message)});
            m->push_back({S8("line"), N8(f.line)});
            m->push_back({S8("col"), N8(f.col)});
        }
        return Value::makeMap(m);
    };

    // tokenDump(src) -> جدول نصي: LINE:COL  TYPE  CATEGORY  lexeme
    natives["tokenDump"] = [](Args& a, int line) -> Value {
        expectArgs("tokenDump", a, 1, line);
        const std::string src = asStr8(a[0], "tokenDump", line);
        const auto toks = lexOrThrow8(src, "tokenDump", line);
        std::string out;
        for (const auto& t : toks) {
            if (t.type == TokenType::END_OF_FILE) continue;
            std::string lx;
            for (char c : t.lexeme) {
                if (c == '\n') lx += "\\n";
                else if (c == '\t') lx += "\\t";
                else if (c == '\r') lx += "\\r";
                else lx += c;
            }
            char head[96];
            std::snprintf(head, sizeof head, "%4d:%-3d  %-13s %-12s ", t.line, t.col,
                          tokenTypeName(t.type).c_str(), categoryOf8(t.type));
            if (!out.empty()) out += "\n";
            out += head;
            out += lx;
        }
        return S8(out);
    };

    // tokenCategory(type) -> فئة نوع الرمز أو nil إن لم يُعرف النوع
    natives["tokenCategory"] = [](Args& a, int line) -> Value {
        expectArgs("tokenCategory", a, 1, line);
        const std::string up = upper8(asStr8(a[0], "tokenCategory", line));
        for (TokenType t : allTypes8())
            if (tokenTypeName(t) == up) return S8(categoryOf8(t));
        return Value::nil();
    };

    // tokenKinds([category]) -> كل أسماء أنواع الرموز، اختيارياً ضمن فئة
    natives["tokenKinds"] = [](Args& a, int line) -> Value {
        expectArgsRange("tokenKinds", a, 0, 1, line);
        std::string cat;
        if (a.size() == 1) {
            cat = lower8(asStr8(a[0], "tokenKinds", line));
            bool known = false;
            for (TokenType t : allTypes8()) if (cat == categoryOf8(t)) { known = true; break; }
            if (!known)
                throw diagErr(diag::Code::E0007_InvalidArguments, line,
                              "'tokenKinds': unknown category '" + a[0].str +
                                  "' (keyword, literal, identifier, operator, punctuation, special)");
        }
        auto arr = std::make_shared<ArrayData>();
        for (TokenType t : allTypes8())
            if (cat.empty() || cat == categoryOf8(t)) arr->push_back(S8(tokenTypeName(t)));
        return Value::makeArray(arr);
    };

    // tokenKeywords() -> كل الكلمات المحجوزة الفعلية في Lexer (مرتّبة أبجدياً)
    natives["tokenKeywords"] = [](Args& a, int line) -> Value {
        expectArgs("tokenKeywords", a, 0, line);
        auto kw = keywordList();
        std::sort(kw.begin(), kw.end());
        auto arr = std::make_shared<ArrayData>();
        for (const auto& k : kw) arr->push_back(S8(k));
        return Value::makeArray(arr);
    };

    // tokenIsKeyword(word) -> true إن كانت الكلمة محجوزة في Lexer
    natives["tokenIsKeyword"] = [](Args& a, int line) -> Value {
        expectArgs("tokenIsKeyword", a, 1, line);
        const std::string w = asStr8(a[0], "tokenIsKeyword", line);
        const auto kw = keywordList();
        return Value::boolean_(std::find(kw.begin(), kw.end(), w) != kw.end());
    };
}

} // namespace rin
