#pragma once
// Rin Language - common token & value definitions
#include <string>
#include <vector>
#include <optional>
#include "diagnostics/diagnostic.h"

namespace rin {

enum class TokenType {
    // literals
    NUMBER, STRING, IDENT,
    // core keywords
    LET, PRINT, IF, ELSE, WHILE, FUN, RETURN, TRUE, FALSE, NIL, AND, OR,
    BREAK, CONTINUE, RINOPEN, // rinopen = الاسم الموحّد للحلقة
    FOR,             // for (init; condition; increment) { ... } -> حلقة for على طراز C (إضافة جديدة، additive بحتة)
    // data-container language keywords (لغة الحاويات/البيانات)
    TEXT,            // text  -> إعلان قيمة نصية
    CONTAINER,       // container
    CONTAINERS,      // Containers (جزء من Containers.Group)
    GROUP,           // Group (جزء من Containers.Group)
    VOLUME,          // Volume
    SECTION,         // Section
    TRANSLATIONS,    // Translations (الكتلة)
    TRANSLATION,     // translation (السطر الواحد)
    LINK,            // link
    TYING,           // tying
    MERGE,           // merge
    INSTALLATION,    // installation
    SIMPLIFIED,      // simplified
    SAVE,            // save
    FILE_KW,         // file
    END,             // end (تُستخدم داخل .end/...)
    PIPE_KW,         // pipe    (جزء من container.pipe -> خط أنابيب بيانات/إحصاء؛ كلمة محجوزة تاريخياً)
    // ملاحظة: "data" / "api" / "import" / "route" ليست أنواع Token مستقلة عمداً؛ تُقرأ كمعرّفات
    // (IDENT) عادية ويُتعرَّف عليها سياقياً في المحلل النحوي فقط (بعد 'container.' أو كبداية عبارة)
    // حتى لا تتحوّل إلى كلمات محجوزة عالمياً تتعارض مع أسماء متغيرات شائعة مثل 'data'.
    // single/double char tokens
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQUAL, EQUAL_EQUAL, BANG, BANG_EQUAL,
    LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    LPAREN, RPAREN, LBRACE, RBRACE,
    LBRACKET, RBRACKET, // [ ]  (مصفوفات وفهرسة arr[i])
    COLON,              // :    (فواصل key:value في القواميس maps)
    COMMA, SEMICOLON,
    AT,              // @  (بداية كتلة container/container.pipe/Containers.Group/Volume)
    DOT,             // .  (تُستخدم في وسم الإغلاق .end/... وفي container.pipe)
    PIPE,            // |>   (مُشغّل الأنابيب: يمرر القيمة اليسرى كأول وسيط للنداء اليمين)
    END_OF_FILE, ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;
    double number = 0.0;
    int line = 0;
    int col = 0;     // 1-indexed، عمود أول حرف من الرمز (نظام Diagnostics — انظر diagnostics/)
    int endCol = 0;  // 1-indexed، حصري النهاية (عمود آخر حرف + 1)
};

// Thrown by the lexer/parser/interpreter on any language error.
//
// .message و.line يبقيان كما كانا (توافقية خلفية كاملة مع كل مستهلكي RinError
// الحاليين: jni_bridge.cpp، rin_c_api.cpp، web/rin_wasm_bridge.cpp، أدوات CLI...).
// أي RinError جديد يُبنى عبر throwDiagnostic(...) يحمل أيضاً Diagnostic كامل
// (كود + موقع دقيق + reason/expected/found/help/suggestions) يستطيع أي مستهلك
// جديد الاستفادة منه دون كسر أي كود قديم لا يعرف عنه شيئاً.
struct RinError {
    std::string message;
    int line;
    std::optional<diag::Diagnostic> diagnostic; // موجود فقط للأخطاء المُصدَرة عبر نظام Diagnostics الجديد

    RinError(std::string msg, int ln) : message(std::move(msg)), line(ln) {}

    // يبني RinError من Diagnostic كامل؛ .message/.line يُشتقّان تلقائياً من الحقول
    // الأساسية (code + message + location.startLine) حتى يبقى كل الكود القديم يعمل كما هو.
    explicit RinError(diag::Diagnostic d)
        : message("[" + diag::codeString(d.code) + "] " + d.message +
                   (d.reason ? (" (" + *d.reason + ")") : "")),
          line(d.location.startLine),
          diagnostic(std::move(d)) {}
};

} // namespace rin
