#include "rin_container_sql.h"
#include <cctype>

namespace rin::sql {

namespace {

// حرف صالح ضمن IDENT: أحرف/أرقام لاتينية، '_' '.' '-'، أو أي بايت UTF-8 غير-ASCII (>= 0x80) —
// هذا الأخير يسمح بمعرّفات/قيم عربية كاملة (مثال: role:eq(مدير)) دون فكّ ترميز UTF-8 فعلياً، لأن
// كل امتداد متعدد البايتات لحرف عربي واحد يقع بالكامل ضمن النطاق >= 0x80 فيُقبَل كوحدة متتالية.
inline bool isIdentByte(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '.' || c == '-' || c >= 0x80;
}

struct Lexer {
    const std::string& s;
    size_t i = 0;
    explicit Lexer(const std::string& src) : s(src) {}

    void skipSpace() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; }
    bool eof() { skipSpace(); return i >= s.size(); }
    char peekChar() { skipSpace(); return i < s.size() ? s[i] : '\0'; }

    // يستهلك رمزاً تركيبياً واحداً محدداً (من المسموح بها فقط) إن كان هو التالي، ويعيد true.
    bool consumeSymbol(char c) {
        skipSpace();
        if (i < s.size() && s[i] == c) { i++; return true; }
        return false;
    }

    // يقرأ IDENT واحداً (تسلسل غير فارغ من isIdentByte). يرمي SqlSyntaxError إن لم يبدأ بحرف صالح.
    std::string readIdent(const std::string& what) {
        skipSpace();
        size_t start = i;
        while (i < s.size() && isIdentByte(static_cast<unsigned char>(s[i]))) i++;
        if (i == start) {
            throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ " + what + " عند الموضع " +
                                  std::to_string(i) + " من الاستعلام");
        }
        return s.substr(start, i - start);
    }

    // فحص أمان مبكر: يرفض أي رمز خارج القائمة المسموحة برسالة واضحة، قبل أي محاولة تحليل نحوي —
    // بدل أن يُبتلَع بصمت داخل IDENT مجاور (غير ممكن أصلاً هنا لأن isIdentByte لا يشملها) أو يُنتج
    // خطأ تحليل مُضلِّلاً لاحقاً بعيداً عن موضعه الحقيقي.
    void validateCharset() const {
        for (size_t k = 0; k < s.size(); k++) {
            unsigned char c = static_cast<unsigned char>(s[k]);
            if (std::isspace(c)) continue;
            if (isIdentByte(c)) continue;
            if (c == '/' || c == ':' || c == '&' || c == '(' || c == ')' || c == '#') continue;
            throw SqlSyntaxError(std::string("RIN CONTAINER SQL: رمز غير مسموح به '") +
                                  std::string(1, static_cast<char>(c)) +
                                  "' عند الموضع " + std::to_string(k) +
                                  " -- المسموح به حصراً: / : & ( ) # بالإضافة لحروف/أرقام المعرّفات");
        }
    }
};

bool isSupportedOp(const std::string& op) {
    static const char* ops[] = {"eq", "ne", "gt", "gte", "lt", "lte", "has", "like"};
    for (const char* o : ops) if (op == o) return true;
    return false;
}

// IDENT ("/" IDENT)* -> أجزاء مسار (اسم حاوية متداخل أو حقل متداخل)
std::vector<std::string> readPath(Lexer& lex, const std::string& what) {
    std::vector<std::string> parts;
    parts.push_back(lex.readIdent(what));
    while (true) {
        size_t save = lex.i;
        lex.skipSpace();
        if (lex.i < lex.s.size() && lex.s[lex.i] == '/') {
            lex.i++;
            parts.push_back(lex.readIdent(what + " بعد '/'"));
        } else {
            lex.i = save;
            break;
        }
    }
    return parts;
}

Predicate readPredicate(Lexer& lex) {
    Predicate p;
    auto fieldParts = readPath(lex, "اسم حقل");
    std::string field = fieldParts[0];
    for (size_t k = 1; k < fieldParts.size(); k++) field += "/" + fieldParts[k];
    p.field = field;

    if (!lex.consumeSymbol(':')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ ':' بعد اسم الحقل '" + field + "'");
    }
    std::string op = lex.readIdent("اسم عملية (eq/ne/gt/gte/lt/lte/has/like)");
    if (!isSupportedOp(op)) {
        throw SqlSyntaxError("RIN CONTAINER SQL: عملية غير معروفة '" + op +
                              "' -- العمليات المدعومة: eq, ne, gt, gte, lt, lte, has, like");
    }
    p.op = op;
    if (!lex.consumeSymbol('(')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ '(' بعد العملية '" + op + "'");
    }
    // وسيط واحد اختياري (بلا فاصلة -- الفاصلة ',' ليست من الرموز المسموحة) ثم ')'
    if (lex.peekChar() != ')') {
        p.arg = lex.readIdent("وسيط العملية '" + op + "'");
    }
    if (!lex.consumeSymbol(')')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ ')' بعد وسيط العملية '" + op + "'");
    }
    return p;
}

} // namespace

Query parse(const std::string& text) {
    Lexer lex(text);
    lex.validateCharset();

    Query q;
    if (lex.eof()) {
        throw SqlSyntaxError("RIN CONTAINER SQL: استعلام فارغ");
    }
    if (lex.consumeSymbol('#')) {
        q.targetIsMask = true;
        q.targetMask = lex.readIdent("اسم قناع بعد '#'");
    } else {
        q.targetPath = readPath(lex, "اسم حاوية");
    }
    while (lex.consumeSymbol('&')) {
        q.predicates.push_back(readPredicate(lex));
    }
    if (!lex.eof()) {
        throw SqlSyntaxError("RIN CONTAINER SQL: رموز زائدة غير متوقَّعة بعد نهاية الاستعلام عند الموضع " +
                              std::to_string(lex.i));
    }
    return q;
}

} // namespace rin::sql
