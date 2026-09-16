#pragma once
#include <string>
#include <vector>
#include <stdexcept>

// ============================================================================================
// RIN CONTAINER SQL (RCSQL) 1.0
// --------------------------------------------------------------------------------------------
// استعلام خفيف خاص بـ Rin للبحث داخل @container.doc / @doc الموجودة مسبقاً.
// RCSQL ليس محرك تخزين جديداً ولا يضيف قاعدة بيانات ثانية.
//
// الرموز التركيبية الوحيدة داخل نص الاستعلام:
//     /  :  &  (  )  #
// بالإضافة إلى المعرّفات (حروف/أرقام/underscore، مع دعم UTF-8)، والمسافات.
// أي رمز آخر مرفوض مباشرة.
//
// grammar:
//   query      := target ("&" predicate)*
//   target     := "#" IDENT | IDENT ("/" IDENT)*
//   predicate := field ":" OP "(" ARG? ")"
//   field      := IDENT ("/" IDENT)*
//   OP         := eq | ne | gt | gte | lt | lte | has | like
//              | starts | ends | contains | exists | missing
//              | empty | notempty | isnull | notnull
//   ARG        := IDENT
//
// كل predicates مرتبطة بـ AND بواسطة '&'. لا توجد صيغة OR في RCSQL 1.0.
// ============================================================================================

namespace rin::sql {

struct Predicate {
    std::string field;
    std::string op;
    std::string arg;
};

struct Query {
    bool targetIsMask = false;
    std::string targetMask;
    std::vector<std::string> targetPath;
    std::vector<Predicate> predicates;
};

struct SqlSyntaxError : std::runtime_error {
    explicit SqlSyntaxError(const std::string& msg) : std::runtime_error(msg) {}
};

Query parse(const std::string& text);
bool isSupportedOperator(const std::string& op);
bool operatorNeedsArgument(const std::string& op);

} // namespace rin::sql
