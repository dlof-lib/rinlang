#pragma once
// Rin Diagnostics - diagnostic.h
//
// تعريف Diagnostic نفسه: كائن موحّد يمثّل أي خطأ/تحذير/ملاحظة يصدر عن أي
// مرحلة من مراحل معالجة Rin (Lexer, Parser, Interpreter/TypeChecker،
// وطبقة قاعدة البيانات المدمجة: schema/index/relation/transaction/...).
//
// انظر docs/ERROR_SYSTEM.md لجدول كامل بكل الأكواد ومتى تُستخدم.
#include <string>
#include <vector>
#include <optional>
#include "source_location.h"

namespace rin::diag {

enum class Severity { Error, Warning, Note, Help };

inline const char* severityName(Severity s) {
    switch (s) {
        case Severity::Error:   return "error";
        case Severity::Warning: return "warning";
        case Severity::Note:    return "note";
        case Severity::Help:    return "help";
    }
    return "error";
}

// ------------------------------------------------------------------------
// جدول أكواد الأخطاء (E-codes) والتحذيرات (W-codes). أضف هنا أولاً أي كود
// جديد قبل استخدامه في أي مرحلة، حتى يبقى مصدراً وحيداً للحقيقة (single
// source of truth) ويظهر تلقائياً في --format=json وdocs.
// ------------------------------------------------------------------------
enum class Code {
    E0001_UndefinedVariable,
    E0002_DuplicateVariable,
    E0003_InvalidAssignment,
    E0004_InvalidType,
    E0005_TypeMismatch,
    E0006_UnknownFunction,
    E0007_InvalidArguments,
    E0008_InvalidReturn,
    E0009_MissingReturn,
    E0010_ParserError,
    E0011_UnexpectedToken,
    E0012_MissingToken,
    E0013_InvalidExpression,
    E0014_InvalidContainer,
    E0015_UnknownContainer,
    E0016_InvalidProperty,
    E0017_UndefinedProperty,
    E0018_InvalidSchema,
    E0019_SchemaViolation,
    E0020_InvalidDocument,
    E0021_InvalidIndex,
    E0022_InvalidRelation,
    E0023_TransactionError,
    E0024_MigrationError,
    E0025_CacheError,
    E0026_AsyncError,
    E0027_AwaitOutsideAsync,
    E0028_ImportError,
    E0029_ModuleNotFound,
    E0030_CircularDependency,
    E0031_GenericError,
    E0032_InvalidGenericArgument,
    E0033_OwnershipError,
    E0034_BorrowError,
    E0035_RuntimeError,
    E0036_IOFailure,
    E0037_NetworkError,
    E0038_PackageError,
    E0039_InternalCompilerError,
    E0040_UnsupportedFeature,
    E0041_MissingDependency, // RCS-1.0 §3.14 Dependency: 'requires X;' حيث X غير موجودة في الشجرة وقت الفحص

    W0001_UnusedVariable,
    W0002_UnusedImport,
    W0003_UnreachableCode,
    W0004_DeprecatedFeature,
    W0005_ShadowedVariable,
    W0006_UnnecessaryConversion,
    W0007_UnusedFunction,
    W0008_SuspiciousComparison,
};

// "E0001" / "W0001" ...
std::string codeString(Code c);
// اسم الكود المختصر بالإنجليزية (UndefinedVariable ...)، يُستخدم في JSON/LSP كـ source/name.
std::string codeName(Code c);
Severity defaultSeverity(Code c); // W-codes -> Warning افتراضياً، E-codes -> Error

// ------------------------------------------------------------------------
// Diagnostic: التمثيل الموحّد لأي خطأ/تحذير عبر كل مراحل Rin.
// ------------------------------------------------------------------------
struct Diagnostic {
    Severity severity = Severity::Error;
    Code code;
    std::string message;                 // العنوان المختصر: "type mismatch", "undefined variable `x`"
    std::optional<std::string> reason;    // "لماذا حدث؟" — سطر reason:
    std::optional<std::string> expected;  // لأخطاء النوع/التوكن: القيمة المتوقعة
    std::optional<std::string> found;     // القيمة الفعلية الموجودة
    SourceLocation location;

    std::vector<std::string> notes;       // أسطر note: إضافية
    std::vector<std::string> hints;       // أسطر help: (قد تحوي أمثلة كود على أسطر متعددة)
    std::vector<std::string> suggestions; // "did you mean `x`?" / "possible matches"
    std::vector<std::string> causedBy;    // سلسلة Caused by: (الأقدم أولاً أو الأحدث أولاً حسب الاستدعاء)

    Diagnostic() : code(Code::E0031_GenericError) {}
    Diagnostic(Code c, std::string msg, SourceLocation loc)
        : severity(defaultSeverity(c)), code(c), message(std::move(msg)), location(std::move(loc)) {}

    Diagnostic& withReason(std::string r) { reason = std::move(r); return *this; }
    Diagnostic& withExpectedFound(std::string exp, std::string fnd) {
        expected = std::move(exp); found = std::move(fnd); return *this;
    }
    Diagnostic& withHint(std::string h) { hints.push_back(std::move(h)); return *this; }
    Diagnostic& withNote(std::string n) { notes.push_back(std::move(n)); return *this; }
    Diagnostic& withSuggestion(std::string s) { suggestions.push_back(std::move(s)); return *this; }
    Diagnostic& withCause(std::string c) { causedBy.push_back(std::move(c)); return *this; }
    Diagnostic& asWarning() { severity = Severity::Warning; return *this; }

    bool isError() const { return severity == Severity::Error; }
};

} // namespace rin::diag
