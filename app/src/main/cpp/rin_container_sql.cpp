#include "rin_container_sql.h"
#include <cctype>
#include <cstdlib>

namespace rin::sql {

namespace {

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

    bool consumeSymbol(char c) {
        skipSpace();
        if (i < s.size() && s[i] == c) { i++; return true; }
        return false;
    }

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
    static const char* ops[] = {
        "eq", "ne", "ieq", "gt", "gte", "lt", "lte",
        "has", "like", "starts", "ends", "exists", "missing",
        "asc", "desc" // أسماء عمليات المُعدِّل order:asc/order:desc (انظر applyModifierIfReserved)
    };
    for (const char* o : ops) if (op == o) return true;
    return false;
}

std::vector<std::string> readPathFrom(Lexer& lex, std::string first, const std::string& what) {
    std::vector<std::string> parts;
    parts.push_back(std::move(first));
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

std::vector<std::string> readPath(Lexer& lex, const std::string& what) {
    return readPathFrom(lex, lex.readIdent(what), what);
}

Predicate readPredicateLeaf(Lexer& lex, std::string firstIdent) {
    auto fieldParts = readPathFrom(lex, std::move(firstIdent), "اسم حقل");
    std::string field = fieldParts[0];
    for (size_t k = 1; k < fieldParts.size(); k++) field += "/" + fieldParts[k];

    Predicate p;
    p.field = field;
    if (!lex.consumeSymbol(':')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ ':' بعد اسم الحقل '" + field + "'");
    }
    std::string op = lex.readIdent("اسم عملية (eq/ne/ieq/gt/gte/lt/lte/has/like/starts/ends/exists/missing)");
    if (!isSupportedOp(op)) {
        throw SqlSyntaxError("RIN CONTAINER SQL: عملية غير معروفة '" + op +
                              "' -- العمليات المدعومة: eq, ne, ieq, gt, gte, lt, lte, has, like, "
                              "starts, ends, exists, missing");
    }
    p.op = op;
    if (!lex.consumeSymbol('(')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ '(' بعد العملية '" + op + "'");
    }
    if (lex.peekChar() != ')') {
        p.arg = lex.readIdent("وسيط العملية '" + op + "'");
    }
    if (!lex.consumeSymbol(')')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ ')' بعد وسيط العملية '" + op + "'");
    }
    return p;
}

Predicate readPredicate(Lexer& lex); // fwd -- readPredicate واستدعاءات المجموعات متبادلة الاستدعاء

// يقرأ محتوى مجموعة or(...)/and(...) بعد استهلاك "(": شرط/مجموعة واحدة على الأقل، مفصولة بـ '&'،
// ثم ')' إغلاق.
std::vector<Predicate> readGroupBody(Lexer& lex, const std::string& groupName) {
    std::vector<Predicate> subs;
    subs.push_back(readPredicate(lex));
    while (lex.consumeSymbol('&')) subs.push_back(readPredicate(lex));
    if (!lex.consumeSymbol(')')) {
        throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ ')' لإغلاق مجموعة " + groupName + "(...)");
    }
    return subs;
}

// يقرأ predicate/group واحداً: إن كان IDENT المقروء "or"/"and"/"not" ويليه '(' مباشرة فهي مجموعة
// منطقية؛ وإلا فهو بداية اسم حقل عادي (وقد تكون "or"/"and"/"not" أسماء حقول شرعية أيضاً طالما لم
// يتبعها '(' مباشرة -- مثال: or:eq(x) شرط عادي على حقل اسمه "or").
Predicate readPredicate(Lexer& lex) {
    std::string first = lex.readIdent("اسم حقل أو or(...)/and(...)/not(...)");
    if ((first == "or" || first == "and" || first == "not") && lex.peekChar() == '(') {
        lex.consumeSymbol('(');
        Predicate group;
        group.isGroup = true;
        group.groupOp = first;
        if (first == "not") {
            group.subs.push_back(readPredicate(lex));
            if (!lex.consumeSymbol(')')) {
                throw SqlSyntaxError("RIN CONTAINER SQL: توقّعتُ ')' لإغلاق مجموعة not(...) -- "
                                      "not(...) تأخذ شرطاً/مجموعة واحدة فقط، بلا '&' بداخلها");
            }
        } else {
            group.subs = readGroupBody(lex, first);
        }
        return group;
    }
    return readPredicateLeaf(lex, first);
}

// يحاول تفسير predicate عادي (غير مجموعة) كأحد المُعدِّلات المحجوزة (order/limit/offset/select/
// distinct)؛ إن كان كذلك يُطبَّق أثره على q ويعاد true (فلا يُضاف إلى q.predicates كشرط فلترة
// حقيقي). يرمي SqlSyntaxError إن كان اسم الحقل محجوزاً لكن الشكل (العملية/الوسيط) غير صالح.
bool applyModifierIfReserved(Query& q, const Predicate& p) {
    if (p.isGroup) return false;
    if (p.field == "order") {
        if (p.op != "asc" && p.op != "desc") {
            throw SqlSyntaxError("RIN CONTAINER SQL: 'order' يتوقّع asc(field) أو desc(field)، وليس '" + p.op + "'");
        }
        if (p.arg.empty()) {
            throw SqlSyntaxError("RIN CONTAINER SQL: 'order' يحتاج اسم حقل كوسيط: order:asc(field)");
        }
        q.orderBy.push_back(SortKey{p.arg, p.op == "desc"});
        return true;
    }
    if (p.field == "limit" || p.field == "offset") {
        if (p.op != "eq") {
            throw SqlSyntaxError("RIN CONTAINER SQL: '" + p.field + "' يتوقّع eq(N)، وليس '" + p.op + "'");
        }
        if (p.arg.empty()) {
            throw SqlSyntaxError("RIN CONTAINER SQL: '" + p.field + "' يحتاج رقماً صحيحاً كوسيط");
        }
        char* end = nullptr;
        long n = std::strtol(p.arg.c_str(), &end, 10);
        if (!end || *end != '\0' || n < 0) {
            throw SqlSyntaxError("RIN CONTAINER SQL: '" + p.field + "' يحتاج رقماً صحيحاً غير سالب، وُجد '" + p.arg + "'");
        }
        if (p.field == "limit") q.limit = n; else q.offset = n;
        return true;
    }
    if (p.field == "select") {
        if (p.op != "has") {
            throw SqlSyntaxError("RIN CONTAINER SQL: 'select' يتوقّع has(field)، وليس '" + p.op + "'");
        }
        if (p.arg.empty()) {
            throw SqlSyntaxError("RIN CONTAINER SQL: 'select' يحتاج اسم حقل كوسيط: select:has(field)");
        }
        q.selectFields.push_back(p.arg);
        return true;
    }
    if (p.field == "distinct") {
        if (p.op != "eq") {
            throw SqlSyntaxError("RIN CONTAINER SQL: 'distinct' يتوقّع eq(field)، وليس '" + p.op + "'");
        }
        if (p.arg.empty()) {
            throw SqlSyntaxError("RIN CONTAINER SQL: 'distinct' يحتاج اسم حقل كوسيط: distinct:eq(field)");
        }
        // تُكرَّر لبناء مفتاح تفريد مركّب: distinct:eq(city) & distinct:eq(role) يعني تفريداً حسب
        // (city, role) معاً، لا كل حقل منفرداً.
        q.distinctFields.push_back(p.arg);
        return true;
    }
    return false;
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
        Predicate p = readPredicate(lex);
        if (!applyModifierIfReserved(q, p)) {
            q.predicates.push_back(std::move(p));
        }
    }
    if (!lex.eof()) {
        throw SqlSyntaxError("RIN CONTAINER SQL: رموز زائدة غير متوقَّعة بعد نهاية الاستعلام عند الموضع " +
                              std::to_string(lex.i));
    }
    return q;
}

} // namespace rin::sql
