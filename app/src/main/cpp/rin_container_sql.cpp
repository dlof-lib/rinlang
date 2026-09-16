#include "rin_container_sql.h"
#include <cctype>
#include <algorithm>

namespace rin::sql {
namespace {

inline bool isIdentByte(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

struct Lexer {
    const std::string& s;
    size_t i = 0;
    explicit Lexer(const std::string& src) : s(src) {}

    void skipSpace() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eof() { skipSpace(); return i >= s.size(); }
    char peek() { skipSpace(); return i < s.size() ? s[i] : '\0'; }

    bool consume(char c) {
        skipSpace();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }

    std::string ident(const std::string& what) {
        skipSpace();
        const size_t start = i;
        while (i < s.size() && isIdentByte(static_cast<unsigned char>(s[i]))) ++i;
        if (i == start) {
            throw SqlSyntaxError("RIN CONTAINER SQL: توقعت " + what + " عند الموضع " + std::to_string(i));
        }
        return s.substr(start, i - start);
    }

    void validateCharset() const {
        for (size_t k = 0; k < s.size(); ++k) {
            const unsigned char c = static_cast<unsigned char>(s[k]);
            if (std::isspace(c) || isIdentByte(c)) continue;
            if (c == '/' || c == ':' || c == '&' || c == '(' || c == ')' || c == '#') continue;
            throw SqlSyntaxError(
                "E0042_InvalidSql: رمز غير مسموح به '" + std::string(1, static_cast<char>(c)) +
                "' عند الموضع " + std::to_string(k) +
                " — المسموح: / : & ( ) # والحروف/الأرقام/underscore فقط");
        }
    }
};

std::vector<std::string> readPath(Lexer& lex, const std::string& what) {
    std::vector<std::string> parts;
    parts.push_back(lex.ident(what));
    while (true) {
        const size_t save = lex.i;
        lex.skipSpace();
        if (lex.i < lex.s.size() && lex.s[lex.i] == '/') {
            ++lex.i;
            parts.push_back(lex.ident(what + " بعد '/'") );
        } else {
            lex.i = save;
            break;
        }
    }
    return parts;
}

std::string joinPath(const std::vector<std::string>& p) {
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        if (i) out += '/';
        out += p[i];
    }
    return out;
}

} // namespace

bool isSupportedOperator(const std::string& op) {
    static const char* ops[] = {
        "eq", "ne", "gt", "gte", "lt", "lte", "has", "like",
        "starts", "ends", "contains", "exists", "missing",
        "empty", "notempty", "isnull", "notnull"
    };
    for (const char* item : ops) if (op == item) return true;
    return false;
}

bool operatorNeedsArgument(const std::string& op) {
    return !(op == "exists" || op == "missing" || op == "empty" ||
             op == "notempty" || op == "isnull" || op == "notnull");
}

Query parse(const std::string& text) {
    Lexer lex(text);
    lex.validateCharset();
    Query q;

    if (lex.eof()) throw SqlSyntaxError("E0042_InvalidSql: استعلام RCSQL فارغ");

    if (lex.consume('#')) {
        q.targetIsMask = true;
        q.targetMask = lex.ident("اسم القناع بعد '#'");
    } else {
        q.targetPath = readPath(lex, "اسم الحاوية");
    }

    while (lex.consume('&')) {
        Predicate p;
        const auto parts = readPath(lex, "اسم الحقل");
        p.field = joinPath(parts);

        if (!lex.consume(':')) {
            throw SqlSyntaxError("E0042_InvalidSql: توقعت ':' بعد الحقل '" + p.field + "'");
        }
        p.op = lex.ident("اسم العملية");
        if (!isSupportedOperator(p.op)) {
            throw SqlSyntaxError("E0042_InvalidSql: عملية غير معروفة '" + p.op + "'");
        }
        if (!lex.consume('(')) {
            throw SqlSyntaxError("E0042_InvalidSql: توقعت '(' بعد العملية '" + p.op + "'");
        }

        if (lex.peek() != ')') p.arg = lex.ident("وسيط العملية '" + p.op + "'");

        if (!lex.consume(')')) {
            throw SqlSyntaxError("E0042_InvalidSql: توقعت ')' بعد العملية '" + p.op + "'");
        }
        const bool needs = operatorNeedsArgument(p.op);
        if (needs && p.arg.empty()) {
            throw SqlSyntaxError("E0042_InvalidSql: العملية '" + p.op + "' تحتاج وسيطاً");
        }
        if (!needs && !p.arg.empty()) {
            throw SqlSyntaxError("E0042_InvalidSql: العملية '" + p.op + "' لا تقبل وسيطاً");
        }
        q.predicates.push_back(std::move(p));
    }

    if (!lex.eof()) {
        throw SqlSyntaxError("E0042_InvalidSql: رموز زائدة عند الموضع " + std::to_string(lex.i));
    }
    return q;
}

} // namespace rin::sql
