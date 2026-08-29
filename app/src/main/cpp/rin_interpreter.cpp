#include "rin_interpreter.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_stdlib_libs.h"
#include "rin_http.h"
#include "rin_json.h"
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/source_manager.h"
#include <cmath>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <cstdint>
#include <unordered_map>
#include <zlib.h> // zlibDeflateRaw/zlibInflateRaw (ضغط DEFLATE حقيقي متوافق مع صيغة ZIP method=8)

// ---- توافق ويندوز/POSIX لـ stat()/mkdir() ----------------------------------
// على أندرويد NDK/لينكس/macOS: stat()/mkdir(path, mode) القياسيتان بتوقيعهما
// المعروف. على ويندوز (MSVC أو MinGW): الاسمان المتاحان بلا بادئة تحته سفلي هما
// _stat()/_mkdir(path) (بلا وسيط mode)، ولا يمكن استدعاء mkdir بوسيطين هناك.
// هذا القسم فقط يوحّد الاسم المُستخدَم أدناه؛ لا يغيّر أي سلوك على المنصات الأخرى.
#ifdef _WIN32
    #include <direct.h>
    using rin_stat_t = struct _stat;
    #define RIN_STAT(pathCStr, stPtr) ::_stat((pathCStr), (stPtr))
    #define RIN_MKDIR(pathCStr) ::_mkdir(pathCStr)
#else
    using rin_stat_t = struct stat;
    #define RIN_STAT(pathCStr, stPtr) ::stat((pathCStr), (stPtr))
    #define RIN_MKDIR(pathCStr) ::mkdir((pathCStr), 0755)
#endif

namespace rin {

// ---- Diagnostics: نظام موحّد (src/diagnostics) ----
// اسم الملف الحالي المستخدَم في كل Diagnostic صادر عن دوال حرة (utilities) أو lambdas لا تصل إلى
// Interpreter::sourceFile (asNumber/asString/expectArgs/requireNumbers/معظم natives...).
// يُضبَط من Interpreter::run()/setSourceFile() قبل أي تنفيذ، ومن قِبل معالجة container.import/@import
// أثناء الدخول إلى ملف/مكتبة مستوردة، بحيث تشير أخطاء تلك الدوال الحرة إلى الملف الصحيح فعلياً.
std::string g_diagFile = "<input>";

// يبني RinError غنياً (Diagnostic كامل: كود + موقع من رقم السطر + رسالة) بلا حاجة لِـ `this` —
// يُستخدم من كل الدوال الحرة و lambdas التي لا تلتقط Interpreter (natives أغلبها [this] فعلاً، لكن
// استخدام دالة حرة واحدة موحّدة لكل المواقع يبقي الكود بسيطاً ويتجنّب تعارض التقاط this في lambdas).
RinError diagErr(diag::Code code, int line, std::string message) {
    diag::Diagnostic d(code, message, diag::SourceLocation::point(g_diagFile, line, 1));
    return RinError(std::move(d));
}


// تصريحات أمامية (forward declarations): تُستخدَم هذه الدوال داخل registerNatives() أدناه
// (natives["httpRequest"]... إلخ) قبل تعريفها الفعلي في نهاية الملف (قسم "أدوات HTTP الحقيقي")،
// وبلا هذه التصريحات لن يُبنى الملف (استخدام قبل التعريف). عرّفها هنا فقط، اترك التعريف الفعلي
// في مكانه الأصلي بالأسفل.
static http::HeaderList headersFromValue(const Value& v, const std::string& fn, int line);
static std::string bodyToString(const Value& v, http::HeaderList& headers);
static Value httpResultToValue(const http::HttpResult& r);

// تمثيل "متداخل" لقيمة (يُستخدم داخل عناصر المصفوفات/القواميس عند الطباعة): النصوص توضع بين علامتي تنصيص.
static std::string reprValue(const Value& v) {
    if (v.type == Value::Type::STRING) return "\"" + v.str + "\"";
    return v.toDisplayString();
}

// تهيئة متعددة الأسطر (multi-line, indented) لمصفوفة/قاموس — تُستخدم حصرياً عبر print pretty=true
// (انظر PrintStmt في rin_ast.h). كل مستوى تعشيش يُزاح بمسافتين إضافيتين؛ العناصر/القيم غير
// المُركَّبة (رقم/نص/منطقي/nil/دالة) تُطبع عبر reprValue تماماً كالتمثيل المضغوط المعتاد، فقط
// الحاويات (array/map) نفسها هي ما يتمدد على عدة أسطر. مصفوفة/قاموس فارغان يبقيان "[]"/"{}"
// على سطر واحد بلا تمدد (لا فائدة من تعدد أسطر لعنصر فارغ).
static std::string prettyPrintValue(const Value& v, int indent) {
    std::string ind(static_cast<size_t>(indent) * 2, ' ');
    std::string indInner(static_cast<size_t>(indent + 1) * 2, ' ');
    if (v.type == Value::Type::ARRAY) {
        if (v.array->empty()) return "[]";
        std::ostringstream ss;
        ss << "[\n";
        for (size_t i = 0; i < v.array->size(); i++) {
            const Value& item = (*v.array)[i];
            ss << indInner;
            if (item.type == Value::Type::ARRAY || item.type == Value::Type::MAP) {
                ss << prettyPrintValue(item, indent + 1);
            } else {
                ss << reprValue(item);
            }
            if (i + 1 < v.array->size()) ss << ",";
            ss << "\n";
        }
        ss << ind << "]";
        return ss.str();
    }
    if (v.type == Value::Type::MAP) {
        if (v.map->empty()) return "{}";
        std::ostringstream ss;
        ss << "{\n";
        for (size_t i = 0; i < v.map->size(); i++) {
            const Value& key = (*v.map)[i].first;
            const Value& val = (*v.map)[i].second;
            ss << indInner << reprValue(key) << ": ";
            if (val.type == Value::Type::ARRAY || val.type == Value::Type::MAP) {
                ss << prettyPrintValue(val, indent + 1);
            } else {
                ss << reprValue(val);
            }
            if (i + 1 < v.map->size()) ss << ",";
            ss << "\n";
        }
        ss << ind << "}";
        return ss.str();
    }
    return reprValue(v);
}

bool valuesEqual(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case Value::Type::NIL: return true;
        case Value::Type::NUMBER: return a.number == b.number;
        case Value::Type::STRING: return a.str == b.str;
        case Value::Type::BOOL: return a.boolean == b.boolean;
        case Value::Type::FUNCTION: return a.function == b.function;
        case Value::Type::ARRAY: {
            if (a.array == b.array) return true;
            if (!a.array || !b.array) return false;
            if (a.array->size() != b.array->size()) return false;
            for (size_t i = 0; i < a.array->size(); i++) {
                if (!valuesEqual((*a.array)[i], (*b.array)[i])) return false;
            }
            return true;
        }
        case Value::Type::MAP: {
            if (a.map == b.map) return true;
            if (!a.map || !b.map) return false;
            if (a.map->size() != b.map->size()) return false;
            for (auto& kv : *a.map) {
                bool found = false;
                for (auto& kv2 : *b.map) {
                    if (valuesEqual(kv.first, kv2.first) && valuesEqual(kv.second, kv2.second)) {
                        found = true; break;
                    }
                }
                if (!found) return false;
            }
            return true;
        }
    }
    return false;
}

std::string Value::typeName() const {
    switch (type) {
        case Type::NIL: return "nil";
        case Type::NUMBER: return "number";
        case Type::STRING: return "string";
        case Type::BOOL: return "bool";
        case Type::FUNCTION: return "function";
        case Type::ARRAY: return "array";
        case Type::MAP: return "map";
    }
    return "nil";
}

std::string Value::toDisplayString() const {
    switch (type) {
        case Type::NIL: return "nil";
        case Type::BOOL: return boolean ? "true" : "false";
        case Type::STRING: return str;
        case Type::FUNCTION: return "<function>";
        case Type::NUMBER: {
            std::ostringstream ss;
            if (number == static_cast<long long>(number)) {
                ss << static_cast<long long>(number);
            } else {
                ss << number;
            }
            return ss.str();
        }
        case Type::ARRAY: {
            std::ostringstream ss;
            ss << "[";
            for (size_t i = 0; i < array->size(); i++) {
                if (i) ss << ", ";
                ss << reprValue((*array)[i]);
            }
            ss << "]";
            return ss.str();
        }
        case Type::MAP: {
            std::ostringstream ss;
            ss << "{";
            for (size_t i = 0; i < map->size(); i++) {
                if (i) ss << ", ";
                ss << reprValue((*map)[i].first) << ": " << reprValue((*map)[i].second);
            }
            ss << "}";
            return ss.str();
        }
    }
    return "nil";
}

// ---- Diagnostics helpers (src/diagnostics) — انظر تعليقات الإعلان في rin_interpreter.h ----

RinError Interpreter::err(diag::Code code, int line, std::string message) const {
    diag::Diagnostic d(code, message, diag::SourceLocation::point(sourceFile, line, 1));
    return RinError(std::move(d));
}

RinError Interpreter::errWithReason(diag::Code code, int line, std::string message, std::string reason) const {
    diag::Diagnostic d(code, message, diag::SourceLocation::point(sourceFile, line, 1));
    d.withReason(std::move(reason));
    return RinError(std::move(d));
}

RinError Interpreter::undefinedVariableErr(const std::string& name, int line, const EnvPtr& env) const {
    diag::Diagnostic d(diag::Code::E0001_UndefinedVariable, "undefined variable `" + name + "`",
                        diag::SourceLocation::point(sourceFile, line, 1));
    d.withReason("no variable named `" + name + "` exists in this scope");
    if (env) {
        std::unordered_set<std::string> visible;
        env->collectVisibleNames(visible);
        std::vector<std::string> candidates(visible.begin(), visible.end());
        auto matches = diag::nearestMatches(name, candidates, 2, 3);
        for (auto& m : matches) d.withSuggestion(m);
        if (!matches.empty()) d.withHint("did you mean `" + matches.front() + "`?");
        else d.withHint("define the variable before using it: `let " + name + " = ...;`");
    }
    return RinError(std::move(d));
}

RinError Interpreter::unknownFunctionErr(const std::string& name, int line) const {
    diag::Diagnostic d(diag::Code::E0006_UnknownFunction, "`" + name + "` is not a function",
                        diag::SourceLocation::point(sourceFile, line, 1));
    d.withReason("no function (built-in or user-defined) named `" + name + "` is callable here");
    std::vector<std::string> candidates;
    candidates.reserve(natives.size());
    for (auto& kv : natives) candidates.push_back(kv.first);
    auto matches = diag::nearestMatches(name, candidates, 2, 3);
    for (auto& m : matches) d.withSuggestion(m);
    if (!matches.empty()) d.withHint("did you mean `" + matches.front() + "()`?");
    return RinError(std::move(d));
}

Interpreter::Interpreter() {
    globals = std::make_shared<Environment>();
    globals->define("PI", Value::num(3.14159265358979323846));
    globals->define("E", Value::num(2.71828182845904523536));
    registerNatives();
}

static double asNumber(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::NUMBER) {
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a number but got " + v.typeName());
    }
    return v.number;
}

// ---- مفهوم "token": يحوّل TokenType إلى اسمه النصي (للاستخدام في native "tokens" أدناه، التي
// تكشف تدفّق الرموز اللغوية الفعلي الذي ينتجه محلّل Rin الحقيقي (rin::Lexer) للمستخدم/المبرمج،
// بدل أن يبقى تفصيلاً داخلياً غير مرئي. مفيد لتعلّم اللغة، تصحيح أخطاء بناء الجملة، أو بناء أدوات
// تحليل نصوص (مثل lib/lexkit.og.rin) تعتمد على معرفة كيف يُقسَّم مصدر Rin فعلياً إلى tokens.
static std::string tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::STRING: return "STRING";
        case TokenType::IDENT: return "IDENT";
        case TokenType::LET: return "LET";
        case TokenType::PRINT: return "PRINT";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::WHILE: return "WHILE";
        case TokenType::FUN: return "FUN";
        case TokenType::RETURN: return "RETURN";
        case TokenType::TRUE: return "TRUE";
        case TokenType::FALSE: return "FALSE";
        case TokenType::NIL: return "NIL";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::BREAK: return "BREAK";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::RINOPEN: return "RINOPEN";
        case TokenType::FOR: return "FOR";
        case TokenType::TEXT: return "TEXT";
        case TokenType::CONTAINER: return "CONTAINER";
        case TokenType::CONTAINERS: return "CONTAINERS";
        case TokenType::GROUP: return "GROUP";
        case TokenType::VOLUME: return "VOLUME";
        case TokenType::SECTION: return "SECTION";
        case TokenType::TRANSLATIONS: return "TRANSLATIONS";
        case TokenType::TRANSLATION: return "TRANSLATION";
        case TokenType::LINK: return "LINK";
        case TokenType::TYING: return "TYING";
        case TokenType::MERGE: return "MERGE";
        case TokenType::INSTALLATION: return "INSTALLATION";
        case TokenType::SIMPLIFIED: return "SIMPLIFIED";
        case TokenType::SAVE: return "SAVE";
        case TokenType::FILE_KW: return "FILE";
        case TokenType::END: return "END";
        case TokenType::PIPE_KW: return "PIPE_KW";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TokenType::BANG: return "BANG";
        case TokenType::BANG_EQUAL: return "BANG_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::COLON: return "COLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::AT: return "AT";
        case TokenType::DOT: return "DOT";
        case TokenType::PIPE: return "PIPE";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

static std::string asString(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::STRING) {
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a string but got " + v.typeName());
    }
    return v.str;
}

static void expectArgs(const std::string& fn, std::vector<Value>& args, size_t count, int line) {
    if (args.size() != count) {
        throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "' expects " + std::to_string(count) +
                        " argument(s) but got " + std::to_string(args.size()));
    }
}

// يتحقّق أن عدد الوسائط بين min و max (شاملَين) -- لدوال بوسيط أخير اختياري مثل cacheSet(key,
// value[, ttlSeconds]) أو defineMigration(name, up[, down]).
static void expectArgsRange(const std::string& fn, std::vector<Value>& args, size_t minCount, size_t maxCount, int line) {
    if (args.size() < minCount || args.size() > maxCount) {
        throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "' expects " + std::to_string(minCount) + " to " + std::to_string(maxCount) +
                        " argument(s) but got " + std::to_string(args.size()));
    }
}

// ---- schema: يتحقّق أن قيمة تطابق اسم نوع مخطط نصي (انظر Interpreter::schemaErrors) ----
static bool valueMatchesSchemaType(const Value& v, const std::string& type) {
    if (type == "any") return true;
    if (type == "string") return v.type == Value::Type::STRING;
    if (type == "number") return v.type == Value::Type::NUMBER;
    if (type == "bool" || type == "boolean") return v.type == Value::Type::BOOL;
    if (type == "array") return v.type == Value::Type::ARRAY;
    if (type == "map" || type == "object") return v.type == Value::Type::MAP;
    return true; // اسم نوع غير معروف: يُتجاهَل التحقق منه بدل رفض الإدراج بلا سبب واضح للمستخدم
}

// يدمج رسائل أخطاء schemaErrors في سطر واحد لرسالة RinError واضحة.
static std::string joinErrors(const std::vector<std::string>& errs) {
    std::string out;
    for (size_t i = 0; i < errs.size(); i++) {
        if (i) out += "; ";
        out += errs[i];
    }
    return out;
}

// ---- transaction: نسخ عميق (deep clone) لقيمة Rin -- ضروري للقطات beginTransaction/rollback:
// نسخ Value وحدها ضحل (ARRAY/MAP يشاركان نفس shared_ptr)، وupdateDoc قد يُعدِّل MapData مكانه
// (دمج جزئي)، فبلا نسخ عميق حقيقي كانت أي لقطة ستتأثر بتعديلات لاحقة على نفس المستند.
static Value deepCloneValue(const Value& v) {
    switch (v.type) {
        case Value::Type::ARRAY: {
            auto arr = std::make_shared<ArrayData>();
            if (v.array) {
                arr->reserve(v.array->size());
                for (auto& item : *v.array) arr->push_back(deepCloneValue(item));
            }
            return Value::makeArray(arr);
        }
        case Value::Type::MAP: {
            auto m = std::make_shared<MapData>();
            if (v.map) {
                m->reserve(v.map->size());
                for (auto& kv : *v.map) m->push_back({deepCloneValue(kv.first), deepCloneValue(kv.second)});
            }
            return Value::makeMap(m);
        }
        default:
            return v; // NUMBER/STRING/BOOL/NIL/FUNCTION: لا حالة مشتركة قابلة للتغيير، النسخ بالقيمة كافٍ
    }
}

// يتحقّق أن طرفَي عملية حسابية/مقارنة (غير + التي تدعم النصوص أيضاً) هما رقمان فعلاً، بدلاً من الاعتماد
// الصامت على Value::number الذي يساوي 0.0 افتراضياً لأي نوع آخر (نص/مصفوفة/قاموس/nil) — وهو ما كان
// يجعل عبارة مثل "abc" - 5 تُحسَب بصمت كـ 0 - 5 بدل رمي خطأ واضح.
static void requireNumbers(const Value& left, const Value& right, const std::string& op, int line) {
    if (left.type != Value::Type::NUMBER || right.type != Value::Type::NUMBER) {
        const Value& bad = (left.type != Value::Type::NUMBER) ? left : right;
        throw diagErr(diag::Code::E0004_InvalidType, line, "operator `" + op + "` requires two numbers, but found a `" + bad.typeName() + "`");
    }
}

// يحوّل قيمة من نوع array إلى مصفوفة أرقام C++ لاستخدامها في الدوال الإحصائية.
static std::vector<double> asNumberArray(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::ARRAY) {
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an array of numbers but got " + v.typeName());
    }
    std::vector<double> out;
    out.reserve(v.array->size());
    for (auto& item : *v.array) {
        if (item.type != Value::Type::NUMBER) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an array of numbers, found a " + item.typeName() + " element");
        }
        out.push_back(item.number);
    }
    if (out.empty()) {
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' لا يقبل مصفوفة فارغة (empty array)");
    }
    return out;
}

// يجمع أسماء الحاويات (containers) الفعلية داخل مجموعة (Containers.Group)، متفرّعاً بشكل متكرر
// عبر أي مجموعات فرعية متداخلة بداخلها، ومحافظاً على ترتيب الإدخال.
static void collectGroupContainerNames(const std::unordered_map<std::string, std::vector<std::string>>& groupMembers,
                                        const std::string& groupKey,
                                        std::vector<std::string>& out) {
    auto it = groupMembers.find(groupKey);
    if (it == groupMembers.end()) return;
    for (auto& memberName : it->second) {
        if (groupMembers.count(memberName)) {
            collectGroupContainerNames(groupMembers, memberName, out); // عضو هو مجموعة فرعية -> تفرّع
        } else {
            out.push_back(memberName); // عضو هو حاوية فعلية
        }
    }
}

static std::string toUpperAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::string containerTagName(ContainerKind k) {
    switch (k) {
        case ContainerKind::PIPE: return "container.pipe";
        case ContainerKind::DATA: return "container.data";
        case ContainerKind::API: return "container.api";
        case ContainerKind::IMPORT: return "container.import";
        // ملاحظة: سواء فُتحت بصيغة "@container.table=" (مدمجة) أو "@table=" (مستقلة)، تُحفَظ
        // دائماً بنفس الوسم الموحَّد "container.table" لضمان إعادة قراءة واحدة لا لبس فيها.
        case ContainerKind::TABLE: return "container.table";
        // نفس المبدأ لـ "@container.doc=" / "@doc=" -> دائماً "container.doc" موحَّدة.
        case ContainerKind::DOC: return "container.doc";
        // نفس المبدأ لمفاهيم التنسيق/الستايل الجديدة: "@Object="/"@portal="/"@block=" توحَّد دائماً
        // إلى "container.object"/"container.portal"/"container.block" عند الحفظ وإعادة القراءة.
        case ContainerKind::OBJECT: return "container.object";
        case ContainerKind::PORTAL: return "container.portal";
        case ContainerKind::BLOCK: return "container.block";
        // "@sticker="/"@container.sticker=" توحَّد دائماً إلى "container.sticker" عند الحفظ وإعادة القراءة.
        case ContainerKind::STICKER: return "container.sticker";
        default: return "container";
    }
}

static std::string containerIcon(ContainerKind k) {
    switch (k) {
        case ContainerKind::PIPE: return "🧵";
        case ContainerKind::DATA: return "🗂️";
        case ContainerKind::API: return "🌐";
        case ContainerKind::IMPORT: return "📦⬅️";
        case ContainerKind::TABLE: return "📊";
        case ContainerKind::DOC: return "🧾";
        case ContainerKind::OBJECT: return "🧩";
        case ContainerKind::PORTAL: return "🎨";
        case ContainerKind::BLOCK: return "🧱";
        case ContainerKind::STICKER: return "🏷️";
        default: return "📦";
    }
}

// ================= تسلسل القيم (serialization) لأجل save/installation الحقيقيَّين =================
// يحوّل رقماً إلى نص Rin قابل لإعادة التحليل مباشرة (بدون ترميز علمي e/E غير مدعوم في scanNumber).
static std::string numberLiteral(double n) {
    if (std::isnan(n) || std::isinf(n)) return "0";
    bool neg = n < 0.0;
    double absN = std::fabs(n);
    std::ostringstream oss;
    if (absN == std::floor(absN) && absN < 1e15) {
        oss << static_cast<long long>(absN);
    } else {
        oss.setf(std::ios::fixed);
        oss.precision(10);
        oss << absN;
        std::string s = oss.str();
        size_t dot = s.find('.');
        if (dot != std::string::npos) {
            size_t last = s.find_last_not_of('0');
            if (last == dot) last++; // خانة صفر واحدة بعد الفاصلة على الأقل
            s = s.substr(0, last + 1);
        }
        return (neg ? "-" : "") + s;
    }
    return (neg ? "-" : "") + oss.str();
}

// يهرّب نصاً ليكون قابلاً لإعادة القراءة داخل علامتي تنصيص Rin (يعتمد على دعم \" \\ \n \t في scanString).
static std::string escapeStringLiteral(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

static bool looksLikeIdentifier(const std::string& s) {
    if (s.empty() || (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_')) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

// يحوّل Value إلى نص حرفي (literal) صالح لإعادة التحليل: أرقام/نصوص/منطقية/مصفوفات/قواميس متداخلة.
// الدوال (FUNCTION) لا يمكن تمثيلها كقيمة حرفية فتُستبدَل بـ nil مع تعليق توضيحي من المستدعي.
static std::string serializeValueLiteral(const Value& v) {
    switch (v.type) {
        case Value::Type::NIL: return "nil";
        case Value::Type::NUMBER: return numberLiteral(v.number);
        case Value::Type::BOOL: return v.boolean ? "true" : "false";
        case Value::Type::STRING: return "\"" + escapeStringLiteral(v.str) + "\"";
        case Value::Type::FUNCTION: return "nil";
        case Value::Type::ARRAY: {
            std::string out = "[";
            if (v.array) {
                for (size_t i = 0; i < v.array->size(); i++) {
                    if (i) out += ", ";
                    out += serializeValueLiteral((*v.array)[i]);
                }
            }
            out += "]";
            return out;
        }
        case Value::Type::MAP: {
            std::string out = "{";
            if (v.map) {
                for (size_t i = 0; i < v.map->size(); i++) {
                    if (i) out += ", ";
                    const auto& kv = (*v.map)[i];
                    std::string keyStr = (kv.first.type == Value::Type::STRING && looksLikeIdentifier(kv.first.str))
                                          ? kv.first.str
                                          : serializeValueLiteral(kv.first);
                    out += keyStr + ": " + serializeValueLiteral(kv.second);
                }
            }
            out += "}";
            return out;
        }
    }
    return "nil";
}

// يبني جسم الحاوية (كل متغيراتها المباشرة: text للنصوص، let لغير ذلك) كنص Rin. الترتيب أبجدي
// لضمان مخرجات ثابتة (deterministic) عند كل حفظ بغضّ النظر عن ترتيب unordered_map الداخلي.
static std::string serializeEnvBody(const EnvPtr& env, bool simplified) {
    std::vector<std::string> names;
    names.reserve(env->values.size());
    for (auto& kv : env->values) names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    std::ostringstream out;
    int skippedFunctions = 0;
    for (auto& name : names) {
        const Value& v = env->values[name];
        if (v.type == Value::Type::FUNCTION) { skippedFunctions++; continue; }
        std::string kw = (v.type == Value::Type::STRING) ? "text" : "let";
        if (simplified) {
            out << kw << " " << name << "=" << serializeValueLiteral(v) << ";";
        } else {
            out << "    " << kw << " " << name << " = " << serializeValueLiteral(v) << ";\n";
        }
    }
    if (!simplified && skippedFunctions > 0) {
        out << "    // ملاحظة: تم تجاهل " << skippedFunctions
            << " دالة/دوال عند الحفظ (الدوال لا يمكن تمثيلها كقيمة محفوظة حالياً)\n";
    }
    return out.str();
}

void Interpreter::registerNatives() {
    // ---- رياضيات (math) ----
    natives["abs"] = [](std::vector<Value>& a, int line) {
        expectArgs("abs", a, 1, line);
        return Value::num(std::fabs(asNumber(a[0], "abs", line)));
    };
    natives["sqrt"] = [](std::vector<Value>& a, int line) {
        expectArgs("sqrt", a, 1, line);
        double n = asNumber(a[0], "sqrt", line);
        if (n < 0) throw diagErr(diag::Code::E0004_InvalidType, line, "'sqrt' لا يقبل عدداً سالباً");
        return Value::num(std::sqrt(n));
    };
    natives["pow"] = [](std::vector<Value>& a, int line) {
        expectArgs("pow", a, 2, line);
        return Value::num(std::pow(asNumber(a[0], "pow", line), asNumber(a[1], "pow", line)));
    };
    natives["floor"] = [](std::vector<Value>& a, int line) {
        expectArgs("floor", a, 1, line);
        return Value::num(std::floor(asNumber(a[0], "floor", line)));
    };
    natives["ceil"] = [](std::vector<Value>& a, int line) {
        expectArgs("ceil", a, 1, line);
        return Value::num(std::ceil(asNumber(a[0], "ceil", line)));
    };
    natives["round"] = [](std::vector<Value>& a, int line) {
        expectArgs("round", a, 1, line);
        return Value::num(std::round(asNumber(a[0], "round", line)));
    };
    natives["min"] = [](std::vector<Value>& a, int line) {
        expectArgs("min", a, 2, line);
        return Value::num(std::min(asNumber(a[0], "min", line), asNumber(a[1], "min", line)));
    };
    natives["max"] = [](std::vector<Value>& a, int line) {
        expectArgs("max", a, 2, line);
        return Value::num(std::max(asNumber(a[0], "max", line), asNumber(a[1], "max", line)));
    };
    natives["random"] = [](std::vector<Value>& a, int line) {
        expectArgs("random", a, 0, line);
        return Value::num(static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0));
    };

    // ---- معالجة نصوص (strings) ----
    natives["len"] = [](std::vector<Value>& a, int line) {
        expectArgs("len", a, 1, line);
        if (a[0].type == Value::Type::STRING) return Value::num(static_cast<double>(a[0].str.size()));
        if (a[0].type == Value::Type::ARRAY) return Value::num(static_cast<double>(a[0].array->size()));
        if (a[0].type == Value::Type::MAP) return Value::num(static_cast<double>(a[0].map->size()));
        throw diagErr(diag::Code::E0004_InvalidType, line, "'len' expects a string, array or map but got " + a[0].typeName());
    };
    natives["upper"] = [](std::vector<Value>& a, int line) {
        expectArgs("upper", a, 1, line);
        std::string s = asString(a[0], "upper", line);
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value::string(s);
    };
    natives["lower"] = [](std::vector<Value>& a, int line) {
        expectArgs("lower", a, 1, line);
        std::string s = asString(a[0], "lower", line);
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Value::string(s);
    };
    natives["trim"] = [](std::vector<Value>& a, int line) {
        expectArgs("trim", a, 1, line);
        std::string s = asString(a[0], "trim", line);
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return Value::string("");
        size_t e = s.find_last_not_of(" \t\r\n");
        return Value::string(s.substr(b, e - b + 1));
    };
    natives["substr"] = [](std::vector<Value>& a, int line) -> Value {
        if (a.size() != 2 && a.size() != 3) {
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'substr' expects 2 or 3 argument(s) but got " + std::to_string(a.size()));
        }
        std::string s = asString(a[0], "substr", line);
        long start = static_cast<long>(asNumber(a[1], "substr", line));
        if (start < 0) start = 0;
        if (static_cast<size_t>(start) > s.size()) start = static_cast<long>(s.size());
        long len = a.size() == 3 ? static_cast<long>(asNumber(a[2], "substr", line))
                                  : static_cast<long>(s.size()) - start;
        if (len < 0) len = 0;
        return Value::string(s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
    };
    natives["split"] = [](std::vector<Value>& a, int line) {
        expectArgs("split", a, 2, line);
        std::string s = asString(a[0], "split", line);
        std::string sep = asString(a[1], "split", line);
        auto result = std::make_shared<ArrayData>();
        if (sep.empty()) {
            for (char c : s) result->push_back(Value::string(std::string(1, c)));
        } else {
            size_t pos = 0;
            while (true) {
                size_t next = s.find(sep, pos);
                if (next == std::string::npos) {
                    result->push_back(Value::string(s.substr(pos)));
                    break;
                }
                result->push_back(Value::string(s.substr(pos, next - pos)));
                pos = next + sep.size();
            }
        }
        return Value::makeArray(result);
    };
    natives["join"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("join", a, 2, line);
        if (a[0].type != Value::Type::ARRAY) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'join' expects an array as the first argument");
        }
        std::string sep = asString(a[1], "join", line);
        std::ostringstream ss;
        for (size_t i = 0; i < a[0].array->size(); i++) {
            if (i) ss << sep;
            ss << (*a[0].array)[i].toDisplayString();
        }
        return Value::string(ss.str());
    };
    natives["indexOf"] = [](std::vector<Value>& a, int line) {
        expectArgs("indexOf", a, 2, line);
        std::string s = asString(a[0], "indexOf", line);
        std::string sub = asString(a[1], "indexOf", line);
        auto pos = s.find(sub);
        return Value::num(pos == std::string::npos ? -1.0 : static_cast<double>(pos));
    };
    natives["replace"] = [](std::vector<Value>& a, int line) {
        expectArgs("replace", a, 3, line);
        std::string s = asString(a[0], "replace", line);
        std::string from = asString(a[1], "replace", line);
        std::string to = asString(a[2], "replace", line);
        if (from.empty()) return Value::string(s);
        std::string out;
        size_t pos = 0;
        while (true) {
            size_t next = s.find(from, pos);
            if (next == std::string::npos) { out += s.substr(pos); break; }
            out += s.substr(pos, next - pos);
            out += to;
            pos = next + from.size();
        }
        return Value::string(out);
    };
    natives["contains"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("contains", a, 2, line);
        if (a[0].type == Value::Type::STRING) {
            return Value::boolean_(a[0].str.find(asString(a[1], "contains", line)) != std::string::npos);
        }
        if (a[0].type == Value::Type::ARRAY) {
            for (auto& item : *a[0].array) if (valuesEqual(item, a[1])) return Value::boolean_(true);
            return Value::boolean_(false);
        }
        throw diagErr(diag::Code::E0004_InvalidType, line, "'contains' expects a string or array as the first argument");
    };
    natives["charAt"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("charAt", a, 2, line);
        std::string s = asString(a[0], "charAt", line);
        long i = static_cast<long>(asNumber(a[1], "charAt", line));
        if (i < 0 || static_cast<size_t>(i) >= s.size()) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "'charAt': index out of range");
        }
        return Value::string(std::string(1, s[static_cast<size_t>(i)]));
    };
    // chr(n) -> يحوّل رقماً صحيحاً (0..255) إلى نص من بايت واحد (عكس ord).
    // يسمح ببناء بيانات ثنائية خام (مثل ملفات PNG) داخل سكربتات Rin نفسها.
    natives["chr"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("chr", a, 1, line);
        double n = asNumber(a[0], "chr", line);
        long i = static_cast<long>(n);
        if (i < 0 || i > 255) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'chr': القيمة يجب أن تكون بين 0 و255 (تلقّت " + std::to_string(i) + ")");
        }
        return Value::string(std::string(1, static_cast<char>(static_cast<unsigned char>(i))));
    };
    // ord(s) -> يحوّل أول بايت من نص إلى رقمه (0..255)، عكس chr.
    natives["ord"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("ord", a, 1, line);
        std::string s = asString(a[0], "ord", line);
        if (s.empty()) throw diagErr(diag::Code::E0004_InvalidType, line, "'ord': النص فارغ");
        return Value::num(static_cast<double>(static_cast<unsigned char>(s[0])));
    };
    // bytesFromArray(arr) -> يحوّل مصفوفة أرقام (كل عنصر 0..255) إلى نص بايتات واحد دفعة
    // واحدة (أسرع من استدعاء chr() داخل حلقة وتجميع النتيجة بـ +).
    natives["bytesFromArray"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("bytesFromArray", a, 1, line);
        if (a[0].type != Value::Type::ARRAY) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'bytesFromArray' expects an array but got " + a[0].typeName());
        }
        std::string out;
        out.reserve(a[0].array->size());
        for (auto& item : *a[0].array) {
            double n = asNumber(item, "bytesFromArray", line);
            long i = static_cast<long>(n);
            if (i < 0 || i > 255) {
                throw diagErr(diag::Code::E0004_InvalidType, line, "'bytesFromArray': كل عنصر يجب أن يكون بين 0 و255 (وُجد " + std::to_string(i) + ")");
            }
            out.push_back(static_cast<char>(static_cast<unsigned char>(i)));
        }
        return Value::string(out);
    };
    // crc32(s) -> يحسب CRC-32 (نفس خوارزمية zlib/PNG/gzip) لنص بايتات خام، ويعيده كرقم غير سالب.
    // لازم لبناء أي chunk في ملف PNG صالح (IHDR, IDAT, IEND...) من داخل Rin بدون C++.
    natives["crc32"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("crc32", a, 1, line);
        std::string s = asString(a[0], "crc32", line);
        static uint32_t table[256];
        static bool tableInit = false;
        if (!tableInit) {
            for (uint32_t n = 0; n < 256; n++) {
                uint32_t c = n;
                for (int k = 0; k < 8; k++) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[n] = c;
            }
            tableInit = true;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char ch : s) {
            crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
        }
        crc ^= 0xFFFFFFFFu;
        return Value::num(static_cast<double>(crc));
    };
    // adler32(s) -> يحسب Adler-32 (المطلوب في نهاية كل تدفّق zlib، بما فيها بيانات IDAT في PNG).
    natives["adler32"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("adler32", a, 1, line);
        std::string s = asString(a[0], "adler32", line);
        const uint32_t MOD_ADLER = 65521u;
        uint32_t a32 = 1, b32 = 0;
        for (unsigned char ch : s) {
            a32 = (a32 + ch) % MOD_ADLER;
            b32 = (b32 + a32) % MOD_ADLER;
        }
        return Value::num(static_cast<double>((b32 << 16) | a32));
    };
    // ---- ضغط/فكّ ضغط DEFLATE حقيقي (zlib) — natives خام (raw، بلا رأس zlib/gzip) ----
    // يُطابق تماماً method=8 في صيغة ZIP الرسمية (PKWARE APPNOTE)، فيُنتج/يقرأ أرشيفات
    // .zip مضغوطة فعلياً ومتوافقة 100% مع أي أداة ZIP قياسية (unzip, 7-Zip...).
    // zlibDeflateRaw(s) -> نص بايتات s الخام مضغوطاً بـ DEFLATE خام (بلا أي رأس/تذييل إضافي).
    natives["zlibDeflateRaw"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("zlibDeflateRaw", a, 1, line);
        std::string input = asString(a[0], "zlibDeflateRaw", line);
        z_stream strm{};
        // windowBits = -15: يطلب من zlib صيغة DEFLATE الخام (RFC 1951) بدل التغليف بصيغة
        // zlib (RFC 1950) أو gzip — وهذا بالضبط ما يتوقّعه حقل بيانات ملف ZIP لكل entry.
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "zlibDeflateRaw: تعذّر تهيئة الضاغط (deflateInit2 فشل)");
        }
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        strm.avail_in = static_cast<uInt>(input.size());
        std::string out;
        out.resize(static_cast<size_t>(compressBound(static_cast<uLong>(input.size()))) + 64);
        strm.next_out = reinterpret_cast<Bytef*>(out.empty() ? nullptr : &out[0]);
        strm.avail_out = static_cast<uInt>(out.size());
        int ret = deflate(&strm, Z_FINISH);
        if (ret != Z_STREAM_END) {
            deflateEnd(&strm);
            throw diagErr(diag::Code::E0035_RuntimeError, line, "zlibDeflateRaw: فشل الضغط (deflate لم يُكمل التدفّق)");
        }
        out.resize(strm.total_out);
        deflateEnd(&strm);
        return Value::string(out);
    };
    // zlibInflateRaw(compressed, expectedSize) -> يفكّ ضغط compressed (ناتج DEFLATE خام، مثل
    // zlibDeflateRaw أو أي entry method=8 داخل ملف ZIP) إلى expectedSize بايت أصلية بالضبط
    // (الحجم الأصلي محفوظ دائماً صراحة في صيغة ZIP نفسها، فلا حاجة لتخمينه). يرمي خطأً صريحاً
    // إن كانت البيانات تالفة أو الحجم المتوقّع غير مطابق للتدفّق الفعلي.
    natives["zlibInflateRaw"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("zlibInflateRaw", a, 2, line);
        std::string input = asString(a[0], "zlibInflateRaw", line);
        double expectedD = asNumber(a[1], "zlibInflateRaw", line);
        if (expectedD < 0) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "zlibInflateRaw: الحجم المتوقّع يجب أن يكون >= 0");
        }
        size_t expected = static_cast<size_t>(expectedD);
        if (expected == 0) { return Value::string(""); }
        z_stream strm{};
        if (inflateInit2(&strm, -15) != Z_OK) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "zlibInflateRaw: تعذّر تهيئة فاكّ الضغط (inflateInit2 فشل)");
        }
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        strm.avail_in = static_cast<uInt>(input.size());
        std::string out;
        out.resize(expected);
        strm.next_out = reinterpret_cast<Bytef*>(&out[0]);
        strm.avail_out = static_cast<uInt>(out.size());
        int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (ret != Z_STREAM_END || strm.total_out != expected) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "zlibInflateRaw: فشل فكّ الضغط (بيانات تالفة أو الحجم المتوقّع خاطئ)");
        }
        return Value::string(out);
    };
    natives["toString"] = [](std::vector<Value>& a, int line) {
        expectArgs("toString", a, 1, line);
        return Value::string(a[0].toDisplayString());
    };
    natives["toNumber"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("toNumber", a, 1, line);
        if (a[0].type == Value::Type::NUMBER) return a[0];
        std::string s = asString(a[0], "toNumber", line);
        try {
            size_t idx = 0;
            double d = std::stod(s, &idx);
            return Value::num(d);
        } catch (...) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "'toNumber': \"" + s + "\" ليست رقماً صالحاً");
        }
    };
    // toBool(value) -> يحوّل صراحةً إلى Boolean:
    //   - BOOL تُعاد كما هي.
    //   - NUMBER: 0 -> false، أي رقم آخر -> true (نفس قاعدة isTruthy لكن بشكل صريح ومقروء بالكود).
    //   - STRING: فقط "true"/"false" (بأي حالة أحرف) مقبولتان؛ أي نص آخر خطأ صريح بدل تخمين صامت.
    //   - NIL: false.
    //   - ARRAY/MAP/FUNCTION: خطأ صريح، لأن "صدق" هذه الأنواع غامض المعنى ولا ينبغي تحويله تلقائياً.
    natives["toBool"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("toBool", a, 1, line);
        const Value& v = a[0];
        switch (v.type) {
            case Value::Type::BOOL:
                return v;
            case Value::Type::NUMBER:
                return Value::boolean_(v.number != 0.0);
            case Value::Type::NIL:
                return Value::boolean_(false);
            case Value::Type::STRING: {
                std::string lower = v.str;
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == "true") return Value::boolean_(true);
                if (lower == "false") return Value::boolean_(false);
                throw diagErr(diag::Code::E0035_RuntimeError, line, "'toBool': \"" + v.str + "\" ليست true/false صالحة");
            }
            default:
                throw diagErr(diag::Code::E0035_RuntimeError, line, "'toBool' لا يدعم تحويل نوع " + v.typeName() + " إلى Boolean");
        }
    };
    // isBool(value) -> true فقط إن كانت القيمة من نوع Boolean فعلياً (وليس أي قيمة "صادقة" عبر isTruthy).
    natives["isBool"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("isBool", a, 1, line);
        return Value::boolean_(a[0].type == Value::Type::BOOL);
    };

    // ---- إحصاء (statistics) - مصمّمة للعمل مع خطوط الأنابيب |> و container.pipe ----
    // مرحلة "تجميع" (Aggregation): تُلخّص مصفوفة أرقام إلى قيمة واحدة.
    natives["sum"] = [](std::vector<Value>& a, int line) {
        expectArgs("sum", a, 1, line);
        auto nums = asNumberArray(a[0], "sum", line);
        double total = 0.0;
        for (double n : nums) total += n;
        return Value::num(total);
    };
    natives["mean"] = [](std::vector<Value>& a, int line) {
        expectArgs("mean", a, 1, line);
        auto nums = asNumberArray(a[0], "mean", line);
        double total = 0.0;
        for (double n : nums) total += n;
        return Value::num(total / static_cast<double>(nums.size()));
    };
    natives["median"] = [](std::vector<Value>& a, int line) {
        expectArgs("median", a, 1, line);
        auto nums = asNumberArray(a[0], "median", line);
        std::sort(nums.begin(), nums.end());
        size_t n = nums.size();
        if (n % 2 == 1) return Value::num(nums[n / 2]);
        return Value::num((nums[n / 2 - 1] + nums[n / 2]) / 2.0);
    };
    natives["variance"] = [](std::vector<Value>& a, int line) {
        expectArgs("variance", a, 1, line);
        auto nums = asNumberArray(a[0], "variance", line);
        double m = 0.0;
        for (double n : nums) m += n;
        m /= static_cast<double>(nums.size());
        double sq = 0.0;
        for (double n : nums) sq += (n - m) * (n - m);
        return Value::num(sq / static_cast<double>(nums.size()));
    };
    natives["stddev"] = [](std::vector<Value>& a, int line) {
        expectArgs("stddev", a, 1, line);
        auto nums = asNumberArray(a[0], "stddev", line);
        double m = 0.0;
        for (double n : nums) m += n;
        m /= static_cast<double>(nums.size());
        double sq = 0.0;
        for (double n : nums) sq += (n - m) * (n - m);
        return Value::num(std::sqrt(sq / static_cast<double>(nums.size())));
    };
    natives["mode"] = [](std::vector<Value>& a, int line) {
        expectArgs("mode", a, 1, line);
        auto nums = asNumberArray(a[0], "mode", line);
        double best = nums[0];
        int bestCount = 0;
        for (double candidate : nums) {
            int count = 0;
            for (double n : nums) if (n == candidate) count++;
            if (count > bestCount) { bestCount = count; best = candidate; }
        }
        return Value::num(best);
    };
    natives["minOf"] = [](std::vector<Value>& a, int line) {
        expectArgs("minOf", a, 1, line);
        auto nums = asNumberArray(a[0], "minOf", line);
        return Value::num(*std::min_element(nums.begin(), nums.end()));
    };
    natives["maxOf"] = [](std::vector<Value>& a, int line) {
        expectArgs("maxOf", a, 1, line);
        auto nums = asNumberArray(a[0], "maxOf", line);
        return Value::num(*std::max_element(nums.begin(), nums.end()));
    };

    // مرحلة "تحويل" (Transformation): تُنتج مصفوفة جديدة بنفس الحجم.
    natives["normalize"] = [](std::vector<Value>& a, int line) {
        expectArgs("normalize", a, 1, line);
        auto nums = asNumberArray(a[0], "normalize", line);
        double lo = *std::min_element(nums.begin(), nums.end());
        double hi = *std::max_element(nums.begin(), nums.end());
        auto result = std::make_shared<ArrayData>();
        result->reserve(nums.size());
        for (double n : nums) {
            double normalized = (hi == lo) ? 0.0 : (n - lo) / (hi - lo);
            result->push_back(Value::num(normalized));
        }
        return Value::makeArray(result);
    };
    natives["scale"] = [](std::vector<Value>& a, int line) {
        expectArgs("scale", a, 2, line);
        auto nums = asNumberArray(a[0], "scale", line);
        double factor = asNumber(a[1], "scale", line);
        auto result = std::make_shared<ArrayData>();
        result->reserve(nums.size());
        for (double n : nums) result->push_back(Value::num(n * factor));
        return Value::makeArray(result);
    };
    natives["shift"] = [](std::vector<Value>& a, int line) {
        expectArgs("shift", a, 2, line);
        auto nums = asNumberArray(a[0], "shift", line);
        double delta = asNumber(a[1], "shift", line);
        auto result = std::make_shared<ArrayData>();
        result->reserve(nums.size());
        for (double n : nums) result->push_back(Value::num(n + delta));
        return Value::makeArray(result);
    };

    // ---- Containers.Group: استعلام عن أعضاء المجموعة ----
    natives["groupContainers"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("groupContainers", a, 1, line);
        std::string name = asString(a[0], "groupContainers", line);
        auto result = std::make_shared<ArrayData>();
        if (groupMembers.count(name)) {
            std::vector<std::string> flat;
            collectGroupContainerNames(groupMembers, name, flat);
            for (auto& n : flat) result->push_back(Value::string(n));
        }
        return Value::makeArray(result);
    };
    natives["groupMembers"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("groupMembers", a, 1, line);
        std::string name = asString(a[0], "groupMembers", line);
        auto result = std::make_shared<ArrayData>();
        auto it = groupMembers.find(name);
        if (it != groupMembers.end()) {
            for (auto& n : it->second) result->push_back(Value::string(n));
        }
        return Value::makeArray(result);
    };

    // ---- Rin Loom: استعلام عن ربط @view/warp/@theme بحاوية بعينها (بنفس روح groupContainers/
    // groupMembers أعلاه) — تجعل هذا الربط قابلاً للاستخدام فعلياً من كود Rin نفسه، لا مجرد تخزين صامت ----
    natives["containerHasView"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("containerHasView", a, 1, line);
        std::string name = asString(a[0], "containerHasView", line);
        return Value::boolean_(containerViews.count(name) > 0);
    };
    natives["containerViewName"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("containerViewName", a, 1, line);
        std::string name = asString(a[0], "containerViewName", line);
        auto it = containerViews.find(name);
        if (it == containerViews.end()) return Value::nil();
        return Value::string(it->second->name.empty() ? it->second->kindTag : it->second->name);
    };
    natives["containerWarpNames"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("containerWarpNames", a, 1, line);
        std::string name = asString(a[0], "containerWarpNames", line);
        auto result = std::make_shared<ArrayData>();
        auto it = containerWarpDecls.find(name);
        if (it != containerWarpDecls.end()) {
            for (auto& w : it->second) result->push_back(Value::string(w->name));
        }
        return Value::makeArray(result);
    };
    natives["containerThemeNames"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("containerThemeNames", a, 1, line);
        std::string name = asString(a[0], "containerThemeNames", line);
        auto result = std::make_shared<ArrayData>();
        auto it = containerThemeDecls.find(name);
        if (it != containerThemeDecls.end()) {
            for (auto& t : it->second) result->push_back(Value::string(t->name));
        }
        return Value::makeArray(result);
    };

    // ---- Section: استعلام عن حالة قسم بعد إغلاقه (يحوّل Section من زخرفية بحتة إلى شيء يمكن
    // قراءته وبناء منطق فوقه، بنفس روح groupContainers/groupMembers أعلاه) ----

    // sectionVars(name) -> map بمتغيرات القسم المباشرة (بالاسم name فقط، إن كان له وُجِد)، أو map
    // فارغ إن لم يُنفَّذ أي قسم بهذا الاسم بعد. المفاتيح مرتّبة أبجدياً لمخرجات ثابتة (نفس مبدأ
    // serializeEnvBody أعلاه)، والدوال (FUNCTION) تُستبعَد لأنها لا تُمثَّل كقيمة Rin عادية.
    natives["sectionVars"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("sectionVars", a, 1, line);
        std::string name = asString(a[0], "sectionVars", line);
        auto result = std::make_shared<MapData>();
        auto it = sectionEnvs.find(name);
        if (it != sectionEnvs.end()) {
            std::vector<std::string> keys;
            keys.reserve(it->second->values.size());
            for (auto& kv : it->second->values) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            for (auto& k : keys) {
                const Value& v = it->second->values[k];
                if (v.type == Value::Type::FUNCTION) continue;
                result->push_back({Value::string(k), v});
            }
        }
        return Value::makeMap(result);
    };
    // sectionNames() -> مصفوفة أسماء كل الأقسام المُسمّاة التي نُفِّذت حتى الآن، بترتيب أول ظهور.
    natives["sectionNames"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("sectionNames", a, 0, line);
        auto result = std::make_shared<ArrayData>();
        for (auto& n : sectionOrder) result->push_back(Value::string(n));
        return Value::makeArray(result);
    };
    // hasSection(name) -> true إن نُفِّذ قسم بهذا الاسم مرة واحدة على الأقل حتى الآن.
    natives["hasSection"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("hasSection", a, 1, line);
        std::string name = asString(a[0], "hasSection", line);
        return Value::boolean_(sectionEnvs.count(name) > 0);
    };

    // ---- container.doc / doc: قاعدة بيانات لاعلاقية (NoSQL) بمستندات ----
    // container = "مجموعة مستندات" (collection)، و Containers.Group التي تضمّها = "قاعدة بيانات"
    // (database) كاملة (استخدم groupContainers(dbName) لسرد مجموعات مستنداتها). الإدراج الوصفي عبر
    // 'document id=... fields=...;' داخل @container.doc، أو الإدراج/التحديث الحيّ عبر insertDoc/updateDoc أدناه.

    // insertDoc(collection, id, fields) -> true إن كان إدراجاً جديداً، false إن استبدل مستنداً موجوداً (upsert كامل)
    natives["insertDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("insertDoc", a, 3, line);
        std::string container = asString(a[0], "insertDoc", line);
        std::string id = asString(a[1], "insertDoc", line);
        if (a[2].type != Value::Type::MAP) {
            throw diagErr(diag::Code::E0020_InvalidDocument, line, "'insertDoc' expects a map as the third argument (document fields)");
        }
        if (!containerKinds.count(container) || containerKinds[container] != ContainerKind::DOC) {
            throw errWithReason(diag::Code::E0020_InvalidDocument, line,
                                 "'" + container + "' is not a NoSQL document collection",
                                 "expected a `container.doc` / `doc` container, defined with `container.doc`");
        }
        auto errors = schemaErrors(container, a[2]);
        if (!errors.empty()) {
            auto d = diagErr(diag::Code::E0019_SchemaViolation, line, "schema violation in `" + container + "`");
            d.diagnostic->withReason(joinErrors(errors));
            throw d;
        }
        auto& docs = docStore[container];
        bool isUpdate = false;
        for (auto& entry : docs) {
            if (entry.first == id) { entry.second = a[2]; isUpdate = true; break; }
        }
        if (!isUpdate) docs.push_back({id, a[2]});
        refreshIndexesForContainer(container);
        notifyWatchers(container, id, a[2], isUpdate ? "update" : "insert", line);
        return Value::boolean_(!isUpdate);
    };

    // updateDoc(collection, id, partialFields) -> true إن وُجد ودُمجت الحقول الجديدة (تحديث جزئي/patch)، false إن لم يوجد
    natives["updateDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("updateDoc", a, 3, line);
        std::string container = asString(a[0], "updateDoc", line);
        std::string id = asString(a[1], "updateDoc", line);
        if (a[2].type != Value::Type::MAP) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'updateDoc' يتوقّع كائناً/قاموساً (map) كوسيط ثالث للحقول الجديدة");
        }
        auto it = docStore.find(container);
        if (it == docStore.end()) return Value::boolean_(false);
        for (auto& entry : it->second) {
            if (entry.first != id) continue;
            // يُبنى الدمج في نسخة منفصلة أولاً ويُتحقَّق من المخطط عليها قبل أي التزام فعلي، حتى لا
            // يُترك المستند بحالة جزئية غير صالحة إن فشل التحقق (schemaErrors) في المنتصف.
            auto merged = std::make_shared<MapData>();
            if (entry.second.type == Value::Type::MAP && entry.second.map) *merged = *entry.second.map;
            for (auto& kv : *a[2].map) {
                bool found = false;
                for (auto& existing : *merged) {
                    if (valuesEqual(existing.first, kv.first)) { existing.second = kv.second; found = true; break; }
                }
                if (!found) merged->push_back(kv);
            }
            Value mergedVal = Value::makeMap(merged);
            auto errors = schemaErrors(container, mergedVal);
            if (!errors.empty()) {
                auto d = diagErr(diag::Code::E0019_SchemaViolation, line, "schema violation in `" + container + "`");
                d.diagnostic->withReason(joinErrors(errors));
                throw d;
            }
            entry.second = mergedVal;
            refreshIndexesForContainer(container);
            notifyWatchers(container, id, mergedVal, "update", line);
            return Value::boolean_(true);
        }
        return Value::boolean_(false);
    };

    // deleteDoc(collection, id) -> true إن حُذف فعلاً، false إن لم يوجد أصلاً
    natives["deleteDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("deleteDoc", a, 2, line);
        std::string container = asString(a[0], "deleteDoc", line);
        std::string id = asString(a[1], "deleteDoc", line);
        auto it = docStore.find(container);
        if (it != docStore.end()) {
            auto& vec = it->second;
            for (size_t i = 0; i < vec.size(); i++) {
                if (vec[i].first == id) {
                    Value removed = vec[i].second;
                    vec.erase(vec.begin() + i);
                    refreshIndexesForContainer(container);
                    notifyWatchers(container, id, removed, "delete", line);
                    return Value::boolean_(true);
                }
            }
        }
        return Value::boolean_(false);
    };

    // findDoc(collection, id) -> حقول المستند (map) أو nil إن لم يوجد
    natives["findDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("findDoc", a, 2, line);
        std::string container = asString(a[0], "findDoc", line);
        std::string id = asString(a[1], "findDoc", line);
        auto it = docStore.find(container);
        if (it != docStore.end()) {
            for (auto& entry : it->second) if (entry.first == id) return entry.second;
        }
        return Value::nil();
    };

    // queryDocs(collection, field, value) -> مصفوفة كل المستندات (map) التي يساوي فيها field القيمة المعطاة
    natives["queryDocs"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("queryDocs", a, 3, line);
        std::string container = asString(a[0], "queryDocs", line);
        std::string field = asString(a[1], "queryDocs", line);
        Value target = a[2];
        auto result = std::make_shared<ArrayData>();
        auto it = docStore.find(container);
        if (it != docStore.end()) {
            for (auto& entry : it->second) {
                const Value& doc = entry.second;
                if (doc.type != Value::Type::MAP || !doc.map) continue;
                for (auto& kv : *doc.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == field && valuesEqual(kv.second, target)) {
                        result->push_back(doc);
                        break;
                    }
                }
            }
        }
        return Value::makeArray(result);
    };

    // queryOneDoc(collection, field, value) -> أول مستند مطابق (map) أو nil
    natives["queryOneDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("queryOneDoc", a, 3, line);
        std::string container = asString(a[0], "queryOneDoc", line);
        std::string field = asString(a[1], "queryOneDoc", line);
        Value target = a[2];
        auto it = docStore.find(container);
        if (it != docStore.end()) {
            for (auto& entry : it->second) {
                const Value& doc = entry.second;
                if (doc.type != Value::Type::MAP || !doc.map) continue;
                for (auto& kv : *doc.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == field && valuesEqual(kv.second, target)) {
                        return doc;
                    }
                }
            }
        }
        return Value::nil();
    };

    // docIds(collection) -> مصفوفة أسماء (ids) كل المستندات بترتيب الإدخال
    natives["docIds"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("docIds", a, 1, line);
        std::string container = asString(a[0], "docIds", line);
        auto result = std::make_shared<ArrayData>();
        auto it = docStore.find(container);
        if (it != docStore.end()) for (auto& entry : it->second) result->push_back(Value::string(entry.first));
        return Value::makeArray(result);
    };

    // allDocs(collection) -> مصفوفة كل المستندات (كل عنصر map) بترتيب الإدخال
    natives["allDocs"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("allDocs", a, 1, line);
        std::string container = asString(a[0], "allDocs", line);
        auto result = std::make_shared<ArrayData>();
        auto it = docStore.find(container);
        if (it != docStore.end()) for (auto& entry : it->second) result->push_back(entry.second);
        return Value::makeArray(result);
    };

    // countDocs(collection) -> عدد المستندات
    natives["countDocs"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("countDocs", a, 1, line);
        std::string container = asString(a[0], "countDocs", line);
        auto it = docStore.find(container);
        return Value::num(it != docStore.end() ? static_cast<double>(it->second.size()) : 0.0);
    };

    // ---- schema: مخطط حقول اختياري لمجموعة مستندات (يُفرَض تلقائياً داخل insertDoc/updateDoc/document) ----

    // defineSchema(collection, {field: "type", ...}) -> true. أنواع مدعومة: string/number/bool
    // (أو boolean)/array/map (أو object)/any. يستبدل أي مخطط سابق لنفس المجموعة بالكامل.
    natives["defineSchema"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("defineSchema", a, 2, line);
        std::string container = asString(a[0], "defineSchema", line);
        if (a[1].type != Value::Type::MAP) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'defineSchema' يتوقّع كائناً/قاموساً (map) كوسيط ثانٍ: {field: \"type\", ...}");
        }
        std::vector<std::pair<std::string, std::string>> fields;
        for (auto& kv : *a[1].map) {
            if (kv.first.type != Value::Type::STRING || kv.second.type != Value::Type::STRING) {
                throw diagErr(diag::Code::E0004_InvalidType, line, "'defineSchema': كل مفتاح/قيمة يجب أن يكونا نصّين (اسم حقل -> اسم نوع)");
            }
            fields.push_back({kv.first.str, kv.second.str});
        }
        schemaStore[container] = std::move(fields);
        return Value::boolean_(true);
    };

    // getSchema(collection) -> map {field: "type", ...} أو nil إن لم يُعرَّف مخطط لهذه المجموعة
    natives["getSchema"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("getSchema", a, 1, line);
        std::string container = asString(a[0], "getSchema", line);
        auto it = schemaStore.find(container);
        if (it == schemaStore.end()) return Value::nil();
        auto result = std::make_shared<MapData>();
        for (auto& fieldType : it->second) result->push_back({Value::string(fieldType.first), Value::string(fieldType.second)});
        return Value::makeMap(result);
    };

    // dropSchema(collection) -> true إن كان هناك مخطط فحُذف، false إن لم يوجد أصلاً
    natives["dropSchema"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("dropSchema", a, 1, line);
        std::string container = asString(a[0], "dropSchema", line);
        return Value::boolean_(schemaStore.erase(container) > 0);
    };

    // validateDoc(collection, fields) -> مصفوفة رسائل الأخطاء (فارغة = صالح أو لا يوجد مخطط أصلاً)،
    // بلا رفض/استثناء -- للاستخدام كفحص مسبق يدوي قبل insertDoc/updateDoc عند الحاجة.
    natives["validateDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("validateDoc", a, 2, line);
        std::string container = asString(a[0], "validateDoc", line);
        if (a[1].type != Value::Type::MAP) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'validateDoc' يتوقّع كائناً/قاموساً (map) كوسيط ثانٍ لحقول المستند");
        }
        auto errors = schemaErrors(container, a[1]);
        auto result = std::make_shared<ArrayData>();
        for (auto& e : errors) result->push_back(Value::string(e));
        return Value::makeArray(result);
    };

    // ---- index: فهرسة قيم حقل داخل مجموعة مستندات (تسريع/تسهيل البحث المتساوي عبر findByIndex) ----

    // createIndex(collection, field) -> true إن أُنشئ فهرس جديد، false إن كان موجوداً أصلاً (يُعاد بناؤه بأي حال)
    natives["createIndex"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("createIndex", a, 2, line);
        std::string container = asString(a[0], "createIndex", line);
        std::string field = asString(a[1], "createIndex", line);
        auto& fields = indexStore[container];
        bool exists = false;
        for (auto& fe : fields) if (fe.first == field) { exists = true; break; }
        if (!exists) fields.push_back({field, IndexBuckets{}});
        refreshIndexesForContainer(container);
        return Value::boolean_(!exists);
    };

    // dropIndex(collection, field) -> true إن حُذف فعلاً، false إن لم يوجد أصلاً
    natives["dropIndex"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("dropIndex", a, 2, line);
        std::string container = asString(a[0], "dropIndex", line);
        std::string field = asString(a[1], "dropIndex", line);
        auto it = indexStore.find(container);
        if (it == indexStore.end()) return Value::boolean_(false);
        auto& fields = it->second;
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == field) { fields.erase(fields.begin() + i); return Value::boolean_(true); }
        }
        return Value::boolean_(false);
    };

    // listIndexes(collection) -> مصفوفة أسماء الحقول المفهرسة حالياً لهذه المجموعة، بترتيب الإنشاء
    natives["listIndexes"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("listIndexes", a, 1, line);
        std::string container = asString(a[0], "listIndexes", line);
        auto result = std::make_shared<ArrayData>();
        auto it = indexStore.find(container);
        if (it != indexStore.end()) for (auto& fe : it->second) result->push_back(Value::string(fe.first));
        return Value::makeArray(result);
    };

    // findByIndex(collection, field, value) -> مصفوفة كل المستندات المطابقة (map)، بترتيب الإدخال.
    // إن لم يوجد فهرس على field بعد، يُنشأ تلقائياً (فهرسة كسولة/lazy) قبل البحث.
    natives["findByIndex"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("findByIndex", a, 3, line);
        std::string container = asString(a[0], "findByIndex", line);
        std::string field = asString(a[1], "findByIndex", line);
        Value target = a[2];
        auto& fields = indexStore[container];
        IndexBuckets* buckets = nullptr;
        for (auto& fe : fields) if (fe.first == field) { buckets = &fe.second; break; }
        if (!buckets) {
            fields.push_back({field, IndexBuckets{}});
            buckets = &fields.back().second;
            refreshIndexesForContainer(container);
        }
        auto result = std::make_shared<ArrayData>();
        auto keyIt = buckets->find(reprValue(target));
        if (keyIt != buckets->end()) {
            auto docIt = docStore.find(container);
            if (docIt != docStore.end()) {
                for (auto& id : keyIt->second) {
                    for (auto& entry : docIt->second) if (entry.first == id) { result->push_back(entry.second); break; }
                }
            }
        }
        return Value::makeArray(result);
    };

    // ---- relation: ربط مجموعتَي مستندات بحقلَي انضمام (join)، شبيه بمفتاح أجنبي بسيط ----

    // defineRelation(name, fromCollection, fromField, toCollection, toField) -> true (يستبدل أي علاقة بنفس الاسم)
    natives["defineRelation"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("defineRelation", a, 5, line);
        std::string name = asString(a[0], "defineRelation", line);
        std::string fromContainer = asString(a[1], "defineRelation", line);
        std::string fromField = asString(a[2], "defineRelation", line);
        std::string toContainer = asString(a[3], "defineRelation", line);
        std::string toField = asString(a[4], "defineRelation", line);
        if (!relationStore.count(name)) relationOrder.push_back(name);
        relationStore[name] = RelationDef{fromContainer, fromField, toContainer, toField};
        return Value::boolean_(true);
    };

    // relatedDocs(relationName, fromId) -> مصفوفة كل مستندات toCollection التي يساوي فيها toField
    // قيمة fromField لمستند fromId في fromCollection (مصفوفة فارغة إن لم يوجد fromId أو لا تطابق له)
    natives["relatedDocs"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("relatedDocs", a, 2, line);
        std::string name = asString(a[0], "relatedDocs", line);
        std::string fromId = asString(a[1], "relatedDocs", line);
        auto result = std::make_shared<ArrayData>();
        auto relIt = relationStore.find(name);
        if (relIt == relationStore.end()) {
            auto d = diagErr(diag::Code::E0022_InvalidRelation, line, "no relation named `" + name + "`");
            d.diagnostic->withReason("`" + name + "` was never registered")
             .withHint("call `defineRelation(\"" + name + "\", ...)` before using it");
            throw d;
        }
        const RelationDef& rel = relIt->second;
        auto srcIt = docStore.find(rel.fromContainer);
        if (srcIt == docStore.end()) return Value::makeArray(result);
        Value joinVal;
        bool haveJoin = false;
        for (auto& entry : srcIt->second) {
            if (entry.first != fromId) continue;
            if (entry.second.type == Value::Type::MAP && entry.second.map) {
                for (auto& kv : *entry.second.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == rel.fromField) { joinVal = kv.second; haveJoin = true; break; }
                }
            }
            break;
        }
        if (!haveJoin) return Value::makeArray(result);
        auto dstIt = docStore.find(rel.toContainer);
        if (dstIt != docStore.end()) {
            for (auto& entry : dstIt->second) {
                const Value& doc = entry.second;
                if (doc.type != Value::Type::MAP || !doc.map) continue;
                for (auto& kv : *doc.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == rel.toField && valuesEqual(kv.second, joinVal)) {
                        result->push_back(doc);
                        break;
                    }
                }
            }
        }
        return Value::makeArray(result);
    };

    // listRelations() -> مصفوفة أسماء كل العلاقات المُعرَّفة، بترتيب أول تعريف
    natives["listRelations"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("listRelations", a, 0, line);
        auto result = std::make_shared<ArrayData>();
        for (auto& n : relationOrder) result->push_back(Value::string(n));
        return Value::makeArray(result);
    };

    // ---- transaction: commit/rollback حقيقيَّين حول docStore (لقطات عميقة، انظر deepCloneValue) ----

    // beginTransaction() -> true. يدعم التداخل (nested): كل begin يضيف لقطة جديدة فوق مكدّس txStack.
    natives["beginTransaction"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("beginTransaction", a, 0, line);
        std::unordered_map<std::string, std::vector<std::pair<std::string, Value>>> snapshot;
        for (auto& kv : docStore) {
            std::vector<std::pair<std::string, Value>> docs;
            docs.reserve(kv.second.size());
            for (auto& entry : kv.second) docs.push_back({entry.first, deepCloneValue(entry.second)});
            snapshot[kv.first] = std::move(docs);
        }
        txStack.push_back(std::move(snapshot));
        return Value::boolean_(true);
    };

    // commitTransaction() -> true إن كانت هناك معاملة مفتوحة فأُغلقت (تُعتمَد التغييرات)، false إن لم توجد
    natives["commitTransaction"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("commitTransaction", a, 0, line);
        if (txStack.empty()) return Value::boolean_(false);
        txStack.pop_back();
        return Value::boolean_(true);
    };

    // rollbackTransaction() -> true إن استُعيدت حالة docStore من آخر beginTransaction، false إن لم توجد معاملة مفتوحة
    natives["rollbackTransaction"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("rollbackTransaction", a, 0, line);
        if (txStack.empty()) return Value::boolean_(false);
        docStore = std::move(txStack.back());
        txStack.pop_back();
        for (auto& kv : indexStore) refreshIndexesForContainer(kv.first);
        return Value::boolean_(true);
    };

    // inTransaction() -> true إن كانت هناك معاملة واحدة مفتوحة على الأقل حالياً
    natives["inTransaction"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("inTransaction", a, 0, line);
        return Value::boolean_(!txStack.empty());
    };

    // ---- migration: خطوات ترحيل مُسمّاة (up/down)، مع تتبّع ما طُبِّق منها فعلاً ----

    // defineMigration(name, upFn[, downFn]) -> true. upFn إلزامية (دالة Rin بلا وسائط)، downFn اختيارية
    // (لأجل rollbackMigration). تعريف مكرَّر لنفس name يستبدل up/down بلا التأثير على appliedMigrations.
    natives["defineMigration"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgsRange("defineMigration", a, 2, 3, line);
        std::string name = asString(a[0], "defineMigration", line);
        if (a[1].type != Value::Type::FUNCTION) {
            throw diagErr(diag::Code::E0024_MigrationError, line, "'defineMigration': the second argument (up) must be a Rin function with no parameters");
        }
        Value down = Value::nil();
        if (a.size() == 3) {
            if (a[2].type != Value::Type::NIL && a[2].type != Value::Type::FUNCTION) {
                throw diagErr(diag::Code::E0024_MigrationError, line, "'defineMigration': the third argument (down) must be a Rin function or nil");
            }
            down = a[2];
        }
        if (!migrationStore.count(name)) migrationOrder.push_back(name);
        migrationStore[name] = MigrationDef{a[1], down};
        return Value::boolean_(true);
    };

    // runMigration(name) -> true إن نُفِّذت الآن للمرة الأولى، false إن كانت مُطبَّقة أصلاً (لا تكرار)
    natives["runMigration"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("runMigration", a, 1, line);
        std::string name = asString(a[0], "runMigration", line);
        auto it = migrationStore.find(name);
        if (it == migrationStore.end()) {
            auto d = diagErr(diag::Code::E0024_MigrationError, line, "no migration named `" + name + "`");
            d.diagnostic->withReason("`" + name + "` was never registered")
             .withHint("call `defineMigration(\"" + name + "\", ...)` before running it");
            throw d;
        }
        for (auto& applied : appliedMigrations) if (applied == name) return Value::boolean_(false);
        std::vector<Value> noArgs;
        callFunction(it->second.up.function, noArgs, line);
        appliedMigrations.push_back(name);
        return Value::boolean_(true);
    };

    // rollbackMigration(name) -> true إن كانت مُطبَّقة فأُرجعت (استدعاء down إن وُجدت)، false إن لم تكن مُطبَّقة
    natives["rollbackMigration"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("rollbackMigration", a, 1, line);
        std::string name = asString(a[0], "rollbackMigration", line);
        auto pos = std::find(appliedMigrations.begin(), appliedMigrations.end(), name);
        if (pos == appliedMigrations.end()) return Value::boolean_(false);
        auto it = migrationStore.find(name);
        if (it != migrationStore.end() && it->second.down.type == Value::Type::FUNCTION) {
            std::vector<Value> noArgs;
            callFunction(it->second.down.function, noArgs, line);
        }
        appliedMigrations.erase(pos);
        return Value::boolean_(true);
    };

    // appliedMigrations() -> مصفوفة أسماء الترحيلات المُطبَّقة فعلاً، بترتيب التطبيق
    natives["appliedMigrations"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("appliedMigrations", a, 0, line);
        auto result = std::make_shared<ArrayData>();
        for (auto& n : appliedMigrations) result->push_back(Value::string(n));
        return Value::makeArray(result);
    };

    // pendingMigrations() -> مصفوفة أسماء الترحيلات المُعرَّفة ولم تُطبَّق بعد، بترتيب التعريف
    natives["pendingMigrations"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("pendingMigrations", a, 0, line);
        auto result = std::make_shared<ArrayData>();
        for (auto& n : migrationOrder) {
            bool applied = false;
            for (auto& app : appliedMigrations) if (app == n) { applied = true; break; }
            if (!applied) result->push_back(Value::string(n));
        }
        return Value::makeArray(result);
    };

    // ---- cache: تخزين مؤقّت للقيم بمهلة صلاحية اختيارية (TTL بالثواني) ----

    // cacheSet(key, value[, ttlSeconds]) -> true. بلا ttlSeconds = بلا انتهاء صلاحية.
    natives["cacheSet"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgsRange("cacheSet", a, 2, 3, line);
        std::string key = asString(a[0], "cacheSet", line);
        long long expiresAt = 0;
        if (a.size() == 3) {
            double ttl = asNumber(a[2], "cacheSet", line);
            expiresAt = static_cast<long long>(std::time(nullptr)) + static_cast<long long>(ttl);
        }
        cacheStore[key] = CacheEntry{a[1], expiresAt};
        return Value::boolean_(true);
    };

    // cacheGet(key) -> القيمة المخزَّنة، أو nil إن لم توجد أو انتهت صلاحيتها (تُحذَف حينها تلقائياً)
    natives["cacheGet"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("cacheGet", a, 1, line);
        std::string key = asString(a[0], "cacheGet", line);
        auto it = cacheStore.find(key);
        if (it == cacheStore.end()) return Value::nil();
        if (it->second.expiresAt > 0 && it->second.expiresAt <= static_cast<long long>(std::time(nullptr))) {
            cacheStore.erase(it);
            return Value::nil();
        }
        return it->second.value;
    };

    // cacheHas(key) -> true/false (بنفس منطق انتهاء الصلاحية اللحظي في cacheGet)
    natives["cacheHas"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("cacheHas", a, 1, line);
        std::string key = asString(a[0], "cacheHas", line);
        auto it = cacheStore.find(key);
        if (it == cacheStore.end()) return Value::boolean_(false);
        if (it->second.expiresAt > 0 && it->second.expiresAt <= static_cast<long long>(std::time(nullptr))) {
            cacheStore.erase(it);
            return Value::boolean_(false);
        }
        return Value::boolean_(true);
    };

    // cacheDelete(key) -> true إن حُذف فعلاً، false إن لم يوجد أصلاً
    natives["cacheDelete"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("cacheDelete", a, 1, line);
        std::string key = asString(a[0], "cacheDelete", line);
        return Value::boolean_(cacheStore.erase(key) > 0);
    };

    // cacheClear() -> true (يفرّغ كل التخزين المؤقّت)
    natives["cacheClear"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("cacheClear", a, 0, line);
        cacheStore.clear();
        return Value::boolean_(true);
    };

    // cacheKeys() -> مصفوفة كل المفاتيح غير منتهية الصلاحية حالياً (تُحذَف أي مفاتيح منتهية أثناء المرور)
    natives["cacheKeys"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("cacheKeys", a, 0, line);
        long long now = static_cast<long long>(std::time(nullptr));
        auto result = std::make_shared<ArrayData>();
        for (auto it = cacheStore.begin(); it != cacheStore.end();) {
            if (it->second.expiresAt > 0 && it->second.expiresAt <= now) { it = cacheStore.erase(it); continue; }
            result->push_back(Value::string(it->first));
            ++it;
        }
        return Value::makeArray(result);
    };

    // ---- watch: استدعاء دالة Rin تلقائياً عند تغيّر مجموعة مستندات (insertDoc/updateDoc/deleteDoc/document) ----

    // watch(collection, fn) -> true. fn بتوقيع fun(id, doc, event) حيث event = "insert"/"update"/"delete"
    // (doc = حقول المستند وقت الحدث؛ للحذف: المستند كما كان قبل حذفه مباشرة).
    natives["watch"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("watch", a, 2, line);
        std::string container = asString(a[0], "watch", line);
        if (a[1].type != Value::Type::FUNCTION) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'watch' يتوقّع دالة Rin كوسيط ثانٍ: fun(id, doc, event) { ... }");
        }
        docWatchers[container].push_back(a[1]);
        return Value::boolean_(true);
    };

    // unwatch(collection) -> true إن كان هناك مراقبون فحُذفوا جميعاً، false إن لم يوجد أي مراقب أصلاً
    natives["unwatch"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("unwatch", a, 1, line);
        std::string container = asString(a[0], "unwatch", line);
        return Value::boolean_(docWatchers.erase(container) > 0);
    };

    // ---- subscribe/publish: قناة أحداث عامة (pub/sub) مستقلة تماماً عن مجموعات المستندات ----

    // subscribe(channel, fn) -> true. fn بتوقيع fun(payload) (payload = ما مرَّره publish كما هو)
    natives["subscribe"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("subscribe", a, 2, line);
        std::string channel = asString(a[0], "subscribe", line);
        if (a[1].type != Value::Type::FUNCTION) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'subscribe' يتوقّع دالة Rin كوسيط ثانٍ: fun(payload) { ... }");
        }
        channelSubs[channel].push_back(a[1]);
        return Value::boolean_(true);
    };

    // unsubscribe(channel) -> true إن كان هناك مشتركون فحُذفوا جميعاً، false إن لم يوجد أي مشترك أصلاً
    natives["unsubscribe"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("unsubscribe", a, 1, line);
        std::string channel = asString(a[0], "unsubscribe", line);
        return Value::boolean_(channelSubs.erase(channel) > 0);
    };

    // publish(channel, payload) -> عدد المشتركين الذين استُدعيت دوالهم فعلاً (0 إن لم يوجد أي مشترك بالقناة)
    natives["publish"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("publish", a, 2, line);
        std::string channel = asString(a[0], "publish", line);
        int count = 0;
        auto it = channelSubs.find(channel);
        if (it != channelSubs.end()) {
            for (auto& fn : it->second) {
                if (fn.type != Value::Type::FUNCTION || !fn.function) continue;
                std::vector<Value> cbArgs{a[1]};
                callFunction(fn.function, cbArgs, line);
                count++;
            }
        }
        return Value::num(static_cast<double>(count));
    };

    // ---- container.api: نقاط route المسجَّلة بداخله ----
    // فُعِّلت الآن فعلياً: إن نادى برنامج Rin apiRegister(name, baseUrl) بنفس اسم الحاوية من داخل
    // container.api نفسها، يصبح call()/callApi() طلب شبكة حقيقياً فعلياً (نفس محرّك apiGet/apiPost
    // أدناه)، و route هنا يبقى فقط توثيقاً/عقداً متوقَّعاً للنقطة. بلا apiRegister مطابق، يبقى السلوك
    // القديم كما هو تماماً: محاكاة صرفة بلا شبكة تطابق route المسجَّلة (جيدة للاختبار بلا اتصال).
    // call(method, path, body?) -> يبحث داخل container.api الحالي (الذي نُنفَّذ بداخله الآن)
    natives["call"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgsRange("call", a, 2, 3, line);
        std::string method = asString(a[0], "call", line);
        std::string path = asString(a[1], "call", line);
        std::string key = containerStack.empty() ? "" : containerStack.back();
        Value body = a.size() > 2 ? a[2] : Value::nil();
        return performApiCall(key, method, path, line, body);
    };
    // callApi(apiContainerName, method, path, body?) -> يستدعي أي container.api باسمه من أي مكان في البرنامج
    natives["callApi"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgsRange("callApi", a, 3, 4, line);
        std::string key = asString(a[0], "callApi", line);
        std::string method = asString(a[1], "callApi", line);
        std::string path = asString(a[2], "callApi", line);
        Value body = a.size() > 3 ? a[3] : Value::nil();
        return performApiCall(key, method, path, line, body);
    };

    // ================= HTTP حقيقي وفعلي (اتصال شبكة حقيقي) =================
    // على عكس container.api/route/call أعلاه (محاكاة صرفة بلا شبكة، جيدة للاختبار)، كل ما يلي
    // يُجري طلب شبكة حقيقياً فعلياً: على أندرويد عبر java.net.HttpURLConnection حقيقي (انظر
    // RinHttpBridge.kt + jni_bridge.cpp)، وعلى أدوات سطر الأوامر عبر curl حقيقي (rin_http.cpp).
    natives["httpSetTimeout"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpSetTimeout", a, 1, line);
        defaultHttpTimeoutMs = (int)asNumber(a[0], "httpSetTimeout", line);
        return Value::nil();
    };
    natives["jsonEncode"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("jsonEncode", a, 1, line);
        return Value::string(json::encode(a[0]));
    };
    natives["jsonDecode"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("jsonDecode", a, 1, line);
        return json::decodeOrRaw(asString(a[0], "jsonDecode", line));
    };

    // tokens(source) -> مصفوفة tokens حقيقية (كل عنصر: {type, lexeme, line}) ناتجة عن تشغيل
    // rin::Lexer الفعلي على النص source، تماماً كما يراه المحلل النحوي الداخلي بالضبط قبل أي بناء AST.
    // يكشف مفهوم "token" للمبرمج نفسه (تعلّم/تصحيح أخطاء بناء الجملة/أدوات تحليل)، بدل أن يبقى تفصيلاً
    // داخلياً غير مرئي. أخطاء اللغة الصريحة (نص غير مُغلَق، محرف غير متوقّع...) تُرمى كـ RinError عادي
    // بنفس أسلوب باقي اللغة (وليس بصمت كـ token من نوع ERROR)، لأن rin::Lexer::scanTokens() نفسها
    // تفعل ذلك بالفعل. لا يُنتج أبداً TokenType::ERROR عملياً حالياً؛ يبقى مُعالَجاً في tokenTypeName
    // فقط للاكتمال في حال أُضيف مستقبلاً.
    natives["tokens"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("tokens", a, 1, line);
        std::string source = asString(a[0], "tokens", line);
        Lexer lexer(source);
        std::vector<Token> toks;
        try {
            toks = lexer.scanTokens();
        } catch (RinError& e) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "'tokens': " + e.message);
        }
        auto arr = std::make_shared<ArrayData>();
        arr->reserve(toks.size());
        for (auto& t : toks) {
            auto m = std::make_shared<MapData>();
            m->push_back({Value::string("type"), Value::string(tokenTypeName(t.type))});
            m->push_back({Value::string("lexeme"), Value::string(t.lexeme)});
            m->push_back({Value::string("line"), Value::num((double)t.line)});
            arr->push_back(Value::makeMap(m));
        }
        return Value::makeArray(arr);
    };
    // tokenType(source) -> نوع أول token فقط (نص)، اختصار مريح عند فحص رمز واحد بدل مصفوفة كاملة
    // مثلاً: tokenType("let") == "LET"، tokenType("foo") == "IDENT"، tokenType("==") == "EQUAL_EQUAL"
    natives["tokenType"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("tokenType", a, 1, line);
        std::string source = asString(a[0], "tokenType", line);
        Lexer lexer(source);
        std::vector<Token> toks;
        try {
            toks = lexer.scanTokens();
        } catch (RinError& e) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, "'tokenType': " + e.message);
        }
        for (auto& t : toks) {
            if (t.type != TokenType::END_OF_FILE) return Value::string(tokenTypeName(t.type));
        }
        return Value::string("EOF");
    };

    natives["httpRequest"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpRequest", a, 4, line);
        std::string method = asString(a[0], "httpRequest", line);
        std::string url = asString(a[1], "httpRequest", line);
        auto headers = headersFromValue(a[2], "httpRequest", line);
        std::string body = bodyToString(a[3], headers);
        auto result = http::performRequest(method, url, headers, body, defaultHttpTimeoutMs);
        return httpResultToValue(result);
    };
    natives["httpGet"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpGet", a, 1, line);
        std::string url = asString(a[0], "httpGet", line);
        auto result = http::performRequest("GET", url, {}, "", defaultHttpTimeoutMs);
        return httpResultToValue(result);
    };
    natives["httpPost"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpPost", a, 2, line);
        std::string url = asString(a[0], "httpPost", line);
        http::HeaderList headers;
        std::string body = bodyToString(a[1], headers);
        auto result = http::performRequest("POST", url, headers, body, defaultHttpTimeoutMs);
        return httpResultToValue(result);
    };
    natives["httpPut"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpPut", a, 2, line);
        std::string url = asString(a[0], "httpPut", line);
        http::HeaderList headers;
        std::string body = bodyToString(a[1], headers);
        auto result = http::performRequest("PUT", url, headers, body, defaultHttpTimeoutMs);
        return httpResultToValue(result);
    };
    natives["httpPatch"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpPatch", a, 2, line);
        std::string url = asString(a[0], "httpPatch", line);
        http::HeaderList headers;
        std::string body = bodyToString(a[1], headers);
        auto result = http::performRequest("PATCH", url, headers, body, defaultHttpTimeoutMs);
        return httpResultToValue(result);
    };
    natives["httpDelete"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("httpDelete", a, 1, line);
        std::string url = asString(a[0], "httpDelete", line);
        auto result = http::performRequest("DELETE", url, {}, "", defaultHttpTimeoutMs);
        return httpResultToValue(result);
    };

    // ---- apiRegister/apiHeader: يسجّل المبرمج API خاصته الحقيقي (اسم + baseUrl + ترويساته
    // الخاصة كمفتاح API/Authorization)، ثم يستدعيه لاحقاً باسمه من أي مكان في البرنامج ----
    natives["apiRegister"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiRegister", a, 2, line);
        std::string name = asString(a[0], "apiRegister", line);
        std::string baseUrl = asString(a[1], "apiRegister", line);
        apiEndpoints[name].baseUrl = baseUrl; // يحافظ على أي ترويسات مسجَّلة له مسبقاً إن أُعيد التسجيل
        return Value::boolean_(true);
    };
    natives["apiHeader"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiHeader", a, 3, line);
        std::string name = asString(a[0], "apiHeader", line);
        std::string key = asString(a[1], "apiHeader", line);
        std::string value = asString(a[2], "apiHeader", line);
        auto it = apiEndpoints.find(name);
        if (it == apiEndpoints.end()) throw diagErr(diag::Code::E0037_NetworkError, line, "apiHeader: لا وجود لـ API باسم '" + name + "' — سجِّله أولاً عبر apiRegister(name, baseUrl)");
        auto& hs = it->second.headers;
        for (auto& kv : hs) if (kv.first == key) { kv.second = value; return Value::boolean_(true); }
        hs.push_back({key, value});
        return Value::boolean_(true);
    };
    natives["apiCall"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiCall", a, 4, line);
        std::string name = asString(a[0], "apiCall", line);
        std::string method = asString(a[1], "apiCall", line);
        std::string path = asString(a[2], "apiCall", line);
        return performRealApiCall(name, method, path, a[3], line);
    };
    // ---- Banner convenience API (see docs/banner.md) ----
    // Deliberately flat identifiers (bannerSuccess, not banner.success) because Rin's call()
    // parser only ever accepts a single IDENT followed by '(' -- there is no general member-call
    // expression ('receiver.method(...)') anywhere in the grammar today (verified: rin_parser.cpp
    // Parser::call() only special-cases VariableExpr + LPAREN; the handful of existing dotted
    // forms like container.pipe/link.id/Containers.Group are hand-written *statement*-level
    // keywords, not a general expression feature). Adding real receiver.method(...) call syntax
    // is a legitimate, separate, cross-cutting parser+interpreter feature -- not something to
    // bolt on unsafely here -- so until that lands, this flat naming is the honest, non-breaking
    // way to expose banner.success("...")-equivalent convenience calls, and it matches the
    // existing apiGet/apiPost/apiRegister naming convention already used in this file.
    //
    // Each call logs the exact BANNER_CREATED / BANNER_SHOWN execution events requested (task
    // §20), through the interpreter's real `output` stream -- the same sink print/level= already
    // writes to -- with a real wall-clock HH:MM:SS timestamp, not a placeholder string.
    auto emitBannerEvent = [this](const std::string& eventName) {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        output << buf << " " << eventName << "\n";
    };
    auto bannerConvenience = [this, emitBannerEvent](const std::string& fnName, const std::string& type) {
        return [this, emitBannerEvent, fnName, type](std::vector<Value>& a, int line) -> Value {
            expectArgs(fnName, a, 1, line);
            std::string text = asString(a[0], fnName, line);
            emitBannerEvent("BANNER_CREATED");
            emitBannerEvent("BANNER_SHOWN");
            // Human-readable line in the Code Output panel (level= reuses the same icons print
            // already uses for info/success/warn/error so a banner reads consistently next to
            // ordinary print output).
            std::string levelKey = (type == "warning") ? "warn" : type;
            std::string prefix;
            if (levelKey == "info") prefix = "\u2139\uFE0F ";
            else if (levelKey == "success") prefix = "\u2705 ";
            else if (levelKey == "warn") prefix = "\u26A0\uFE0F ";
            else if (levelKey == "error") prefix = "\u274C ";
            output << prefix << "[banner:" << type << "] " << text << "\n";
            return Value::nil();
        };
    };
    natives["bannerSuccess"] = bannerConvenience("bannerSuccess", "success");
    natives["bannerError"]   = bannerConvenience("bannerError", "error");
    natives["bannerWarning"] = bannerConvenience("bannerWarning", "warning");
    natives["bannerInfo"]    = bannerConvenience("bannerInfo", "info");
    natives["bannerDismiss"] = [this, emitBannerEvent](std::vector<Value>& a, int line) -> Value {
        expectArgsRange("bannerDismiss", a, 0, 0, line);
        emitBannerEvent("BANNER_DISMISSED");
        return Value::nil();
    };

    natives["apiGet"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiGet", a, 2, line);
        std::string name = asString(a[0], "apiGet", line);
        std::string path = asString(a[1], "apiGet", line);
        return performRealApiCall(name, "GET", path, Value::nil(), line);
    };
    natives["apiPost"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiPost", a, 3, line);
        std::string name = asString(a[0], "apiPost", line);
        std::string path = asString(a[1], "apiPost", line);
        return performRealApiCall(name, "POST", path, a[2], line);
    };
    natives["apiPut"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiPut", a, 3, line);
        std::string name = asString(a[0], "apiPut", line);
        std::string path = asString(a[1], "apiPut", line);
        return performRealApiCall(name, "PUT", path, a[2], line);
    };
    natives["apiPatch"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiPatch", a, 3, line);
        std::string name = asString(a[0], "apiPatch", line);
        std::string path = asString(a[1], "apiPatch", line);
        return performRealApiCall(name, "PATCH", path, a[2], line);
    };
    natives["apiDelete"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("apiDelete", a, 2, line);
        std::string name = asString(a[0], "apiDelete", line);
        std::string path = asString(a[1], "apiDelete", line);
        return performRealApiCall(name, "DELETE", path, Value::nil(), line);
    };

    // ---- مصفوفات وقواميس (arrays & maps) ----
    natives["push"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("push", a, 2, line);
        if (a[0].type != Value::Type::ARRAY) throw diagErr(diag::Code::E0004_InvalidType, line, "'push' expects an array");
        a[0].array->push_back(a[1]);
        return Value::num(static_cast<double>(a[0].array->size()));
    };
    natives["pop"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("pop", a, 1, line);
        if (a[0].type != Value::Type::ARRAY) throw diagErr(diag::Code::E0004_InvalidType, line, "'pop' expects an array");
        if (a[0].array->empty()) throw diagErr(diag::Code::E0004_InvalidType, line, "'pop': المصفوفة فارغة");
        Value v = a[0].array->back();
        a[0].array->pop_back();
        return v;
    };
    natives["sort"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("sort", a, 1, line);
        if (a[0].type != Value::Type::ARRAY) throw diagErr(diag::Code::E0004_InvalidType, line, "'sort' expects an array");
        std::sort(a[0].array->begin(), a[0].array->end(), [line](const Value& x, const Value& y) {
            if (x.type == Value::Type::NUMBER && y.type == Value::Type::NUMBER) return x.number < y.number;
            if (x.type == Value::Type::STRING && y.type == Value::Type::STRING) return x.str < y.str;
            throw diagErr(diag::Code::E0035_RuntimeError, line, "'sort' يتطلب أن تكون كل العناصر أرقاماً أو نصوصاً فقط");
        });
        return a[0];
    };
    natives["keys"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("keys", a, 1, line);
        if (a[0].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'keys' expects a map");
        auto result = std::make_shared<ArrayData>();
        for (auto& kv : *a[0].map) result->push_back(kv.first);
        return Value::makeArray(result);
    };
    natives["values"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("values", a, 1, line);
        if (a[0].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'values' expects a map");
        auto result = std::make_shared<ArrayData>();
        for (auto& kv : *a[0].map) result->push_back(kv.second);
        return Value::makeArray(result);
    };
    natives["has"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("has", a, 2, line);
        if (a[0].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'has' expects a map");
        for (auto& kv : *a[0].map) if (valuesEqual(kv.first, a[1])) return Value::boolean_(true);
        return Value::boolean_(false);
    };
    natives["remove"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("remove", a, 2, line);
        if (a[0].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'remove' expects a map");
        for (auto it = a[0].map->begin(); it != a[0].map->end(); ++it) {
            if (valuesEqual(it->first, a[1])) { a[0].map->erase(it); return Value::boolean_(true); }
        }
        return Value::boolean_(false);
    };

    // ---- ملفات حقيقية على القرص (تُبنى فوق basePath عبر resolvePath) ----
    natives["writeFile"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("writeFile", a, 2, line);
        std::string path = asString(a[0], "writeFile", line);
        std::string content = a[1].type == Value::Type::STRING ? a[1].str : a[1].toDisplayString();
        writeRealFile(path, content, line, "writeFile");
        return Value::boolean_(true);
    };
    natives["appendFile"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("appendFile", a, 2, line);
        std::string path = asString(a[0], "appendFile", line);
        std::string content = a[1].type == Value::Type::STRING ? a[1].str : a[1].toDisplayString();
        std::string fullPath = resolvePath(path, line);
        ensureParentDir(fullPath);
        std::ofstream out(fullPath, std::ios::binary | std::ios::app);
        if (!out) throw diagErr(diag::Code::E0036_IOFailure, line, "appendFile: تعذّر فتح الملف '" + path + "' للإضافة إليه");
        out << content;
        return Value::boolean_(true);
    };
    natives["readFile"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("readFile", a, 1, line);
        std::string path = asString(a[0], "readFile", line);
        std::ifstream in(resolvePath(path, line), std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "readFile: تعذّر فتح الملف '" + path + "' للقراءة (غير موجود؟)");
        std::ostringstream buf;
        buf << in.rdbuf();
        return Value::string(buf.str());
    };
    natives["fileExists"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileExists", a, 1, line);
        std::string path = asString(a[0], "fileExists", line);
        rin_stat_t rst{};
        return Value::boolean_(RIN_STAT(resolvePath(path, line).c_str(), &rst) == 0);
    };
    natives["deleteFile"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("deleteFile", a, 1, line);
        std::string path = asString(a[0], "deleteFile", line);
        return Value::boolean_(::remove(resolvePath(path, line).c_str()) == 0);
    };

    // ---- تثبيتات حقيقية ومستمرة (installation) ----
    natives["isInstalled"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("isInstalled", a, 1, line);
        std::string name = asString(a[0], "isInstalled", line);
        return Value::boolean_(installedNames.count(name) > 0);
    };
    natives["listInstalled"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("listInstalled", a, 0, line);
        std::vector<std::string> names(installedNames.begin(), installedNames.end());
        std::sort(names.begin(), names.end());
        auto result = std::make_shared<ArrayData>();
        for (auto& n : names) result->push_back(Value::string(n));
        return Value::makeArray(result);
    };
    // loadInstalled(name) -> يقرأ نسخة حاوية مثبَّتة فعلياً من rin_installed/ وينفّذها (يعيد تسجيلها كحاوية عاملة)،
    // ويُرجع true/false حسب نجاح العثور عليها وتحميلها. يفضّل النسخة الكاملة (.rin) على المبسّطة (.min.rin) إن وُجدت الاثنتان.
    natives["loadInstalled"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("loadInstalled", a, 1, line);
        std::string name = asString(a[0], "loadInstalled", line);
        std::string fullVariant = resolvePath("rin_installed/" + name + ".rin");
        std::string minVariant = resolvePath("rin_installed/" + name + ".min.rin");
        std::string chosen;
        { std::ifstream t(fullVariant, std::ios::binary); if (t.good()) chosen = fullVariant; }
        if (chosen.empty()) { std::ifstream t(minVariant, std::ios::binary); if (t.good()) chosen = minVariant; }
        if (chosen.empty()) return Value::boolean_(false);
        std::ifstream in(chosen, std::ios::binary);
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string src = buf.str();
        try {
            Lexer lex(src);
            auto toks = lex.scanTokens();
            Parser p(toks);
            auto stmts = p.parse();
            executeBlock(stmts, globals);
        } catch (RinError& e) {
            throw diagErr(diag::Code::E0028_ImportError, line, "loadInstalled('" + name + "'): خطأ داخل الملف المحمّل فعلياً من القرص (سطر " +
                            std::to_string(e.line) + "): " + e.message);
        }
        installedNames.insert(name);
        return Value::boolean_(true);
    };
}

// ================= تخزين حقيقي على القرص (save/file/installation) =================

std::string Interpreter::resolvePath(const std::string& rawPath, int line) const {
    if (rawPath.empty()) return rawPath;
    if (basePath.empty()) return rawPath; // بدون basePath لا يوجد "معزول" للخروج منه أصلاً (استخدام مباشر خارج أندرويد)

    // كل المسارات (نسبية أو مطلقة) تُطبَّع بتفكيكها إلى مقاطع وحلّ "." و".." يدوياً، بدل تمريرها كما هي:
    // مسار مطلق صريح (يبدأ بـ '/') كان سابقاً يتجاوز basePath كلياً، و"../" متكررة كانت تصعد فوق جذر
    // basePath بلا أي رادع — كلاهما يسمح لكود Rin (مثلاً مكتبة @import من مصدر غير موثوق) بقراءة/كتابة
    // ملفات عشوائية خارج مجلد المشروع المعزول. الآن أي محاولة صعود فوق basePath نفسه تُرفَض بخطأ واضح.
    std::vector<std::string> segments;
    size_t i = 0;
    while (i <= rawPath.size()) {
        size_t slash = rawPath.find('/', i);
        std::string seg = (slash == std::string::npos) ? rawPath.substr(i) : rawPath.substr(i, slash - i);
        if (seg.empty() || seg == ".") {
            // تجاهل: فاصل مكرر أو "المجلد الحالي"
        } else if (seg == "..") {
            if (segments.empty()) {
                throw diagErr(diag::Code::E0036_IOFailure, line, "مسار غير مسموح به (يحاول الخروج خارج مجلد المشروع المعزول): '" + rawPath + "'");
            }
            segments.pop_back();
        } else {
            segments.push_back(seg);
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    std::string base = basePath;
    if (!base.empty() && base.back() != '/') base += '/';
    std::string result = base;
    for (size_t idx = 0; idx < segments.size(); idx++) {
        result += segments[idx];
        if (idx + 1 < segments.size()) result += '/';
    }
    return result;
}

void Interpreter::ensureParentDir(const std::string& fullPath) const {
    size_t pos = fullPath.find_last_of('/');
    if (pos == std::string::npos) return; // لا يوجد مجلد أب (ملف في المجلد الحالي)
    std::string dir = fullPath.substr(0, pos);
    if (dir.empty()) return;
    // ينشئ المجلدات تدريجياً (يعادل mkdir -p) دون الاعتماد على <filesystem> لضمان توافق أوسع (NDK/g++ القديم).
    std::string partial;
    size_t start = 0;
    if (dir.front() == '/') { partial = "/"; start = 1; }
    while (start <= dir.size()) {
        size_t slash = dir.find('/', start);
        std::string segment = (slash == std::string::npos) ? dir.substr(start) : dir.substr(start, slash - start);
        if (!segment.empty()) {
            partial += segment;
            if (RIN_MKDIR(partial.c_str()) != 0 && errno != EEXIST) {
                // تُترك بصمت: محاولة الكتابة اللاحقة (ofstream) سترمي خطأ واضحاً إن كان هذا هو السبب الفعلي للفشل.
            }
            partial += "/";
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
}

// ================= PNG خفيف الوزن (بلا اعتماديات خارجية) — لأجل table.save/png =================
// table.save/png يرسم صورة PNG حقيقية للجدول تُظهر محتوى الخلايا الفعلي كنص مرسوم بخط
// متجهي (vector-stroke) مُضمَّن بالكامل في هذا الملف — بلا أي مكتبة خطوط خارجية ولا محرّك
// تشكيل (shaping engine). يغطي هذا الخط: الأرقام 0-9، الحروف اللاتينية A-Z/a-z، علامات
// ترقيم شائعة، وأشكال مبسَّطة (منفصلة/غير مُشكَّلة) لحروف الأبجدية العربية الأساسية.
// أمانة واجبة: الحروف العربية هنا أشكال "استنسل" هندسية تقريبية بخطوط مستقيمة لكل حرف على
// حدة (لتمييزه بصرياً عن غيره) وليست خطاً عربياً حقيقياً — فلا رَبْط بين الحروف (ligatures) ولا
// أشكال سياقية (ابتدائي/وسطي/نهائي)، لأن ذلك يتطلب محرّك تشكيل كامل غير متوفر هنا. اتجاه النص
// العام (RTL للعربية) مطبَّق فعلياً عبر ترتيب "runs" (مقاطع عربية/لاتينية) بصرياً بشكل صحيح.
namespace pngutil {

static uint32_t crc32(const unsigned char* data, size_t len) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint32_t adler32(const unsigned char* data, size_t len) {
    uint32_t a = 1, b = 0;
    const uint32_t MOD = 65521;
    for (size_t i = 0; i < len; i++) { a = (a + data[i]) % MOD; b = (b + a) % MOD; }
    return (b << 16) | a;
}

static void putU32BE(std::string& out, uint32_t v) {
    out += static_cast<char>((v >> 24) & 0xFF);
    out += static_cast<char>((v >> 16) & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
    out += static_cast<char>(v & 0xFF);
}

static void putChunk(std::string& out, const char tag[4], const std::string& data) {
    putU32BE(out, static_cast<uint32_t>(data.size()));
    std::string typeAndData(tag, 4);
    typeAndData += data;
    out += typeAndData;
    putU32BE(out, crc32(reinterpret_cast<const unsigned char*>(typeAndData.data()), typeAndData.size()));
}

// تيار zlib يحتوي 'raw' داخل كتل deflate من نوع "stored" (بلا ضغط فعلي) — صالح تماماً حسب
// RFC 1950/1951، ويُقسَّم كتلاً ≤ 65535 بايت عند الحاجة.
static std::string zlibStored(const std::string& raw) {
    std::string out;
    out += static_cast<char>(0x78);
    out += static_cast<char>(0x01);
    size_t pos = 0;
    const size_t maxBlock = 65535;
    if (raw.empty()) {
        out += static_cast<char>(1);
        out += static_cast<char>(0); out += static_cast<char>(0);
        out += static_cast<char>(0xFF); out += static_cast<char>(0xFF);
    }
    while (pos < raw.size()) {
        size_t remaining = raw.size() - pos;
        size_t blockLen = std::min(remaining, maxBlock);
        bool isLast = (pos + blockLen) >= raw.size();
        out += static_cast<char>(isLast ? 1 : 0);
        auto len = static_cast<uint16_t>(blockLen);
        auto nlen = static_cast<uint16_t>(~len);
        out += static_cast<char>(len & 0xFF); out += static_cast<char>((len >> 8) & 0xFF);
        out += static_cast<char>(nlen & 0xFF); out += static_cast<char>((nlen >> 8) & 0xFF);
        out.append(raw, pos, blockLen);
        pos += blockLen;
    }
    putU32BE(out, adler32(reinterpret_cast<const unsigned char*>(raw.data()), raw.size()));
    return out;
}

// يبني ملف PNG كامل من مصفوفة بكسلات RGB (3 بايت للبكسل) بأبعاد width x height.
static std::string encodeRgbPng(int width, int height, const std::vector<unsigned char>& rgb) {
    std::string out;
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.append(reinterpret_cast<const char*>(sig), 8);

    std::string ihdr;
    putU32BE(ihdr, static_cast<uint32_t>(width));
    putU32BE(ihdr, static_cast<uint32_t>(height));
    ihdr += static_cast<char>(8); ihdr += static_cast<char>(2);
    ihdr += static_cast<char>(0); ihdr += static_cast<char>(0); ihdr += static_cast<char>(0);
    putChunk(out, "IHDR", ihdr);

    std::string raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
    for (int y = 0; y < height; y++) {
        raw += static_cast<char>(0); // فلتر "none" لكل سطر
        raw.append(reinterpret_cast<const char*>(&rgb[static_cast<size_t>(y) * width * 3]),
                    static_cast<size_t>(width) * 3);
    }
    putChunk(out, "IDAT", zlibStored(raw));
    putChunk(out, "IEND", "");
    return out;
}

} // namespace pngutil

// ================= خط متجهي مُضمَّن (vector-stroke font) — لرسم نص حقيقي داخل صورة الجدول =================
// كل حرف مُعرَّف كقائمة "أضلاع" (segments) على شبكة تصميم ثابتة GW×GH (7×10 وحدة)، تُرسَم
// كخطوط مستقيمة بعد تحجيمها (scale) وإزاحتها إلى موضع الحرف الفعلي داخل الصورة. هذا يكفي
// لرسم أرقام/حروف لاتينية واضحة تماماً (نفس أسلوب خطوط "stencil/plotter" التقليدية)، ولإعطاء
// كل حرف عربي شكلاً مميزاً خاصاً به بدل تلوين الخلية كاملة بلون واحد.
namespace rinfont {

struct Seg { int x0, y0, x1, y1; };
constexpr int GW = 7;  // عرض شبكة التصميم (وحدات)
constexpr int GH = 10; // ارتفاع شبكة التصميم (وحدات)

using GlyphList = std::vector<Seg>;
using GlyphMap = std::unordered_map<char32_t, GlyphList>;

// فك ترميز UTF-8 إلى نقاط كود (code points) — يتعامل بأمان مع أي بايتات غير صالحة بتجاهلها.
static std::vector<char32_t> utf8Decode(const std::string& s) {
    std::vector<char32_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        char32_t cp; int len;
        if ((c0 & 0x80u) == 0) { cp = c0; len = 1; }
        else if ((c0 & 0xE0u) == 0xC0u) { cp = c0 & 0x1Fu; len = 2; }
        else if ((c0 & 0xF0u) == 0xE0u) { cp = c0 & 0x0Fu; len = 3; }
        else if ((c0 & 0xF8u) == 0xF0u) { cp = c0 & 0x07u; len = 4; }
        else { i++; continue; } // بايت غير صالح كبداية تسلسل: تجاهله
        if (i + static_cast<size_t>(len) > n) { i++; continue; }
        bool ok = true;
        char32_t acc = cp;
        for (int k = 1; k < len; k++) {
            unsigned char cx = static_cast<unsigned char>(s[i + k]);
            if ((cx & 0xC0u) != 0x80u) { ok = false; break; }
            acc = (acc << 6) | (cx & 0x3Fu);
        }
        if (!ok) { i++; continue; }
        out.push_back(acc);
        i += static_cast<size_t>(len);
    }
    return out;
}

static bool isArabicCp(char32_t cp) {
    return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F);
}

// ---- بناء جدول الحروف (يُبنى مرّة واحدة فقط) ----
static const GlyphMap& glyphTable() {
    static GlyphMap g = [] {
        GlyphMap m;
        // نقاط مرجعية مشتركة: يسار=1، يمين=5، وسط=3 على المحور x؛ أعلى=0، منتصف=4، أسفل=9 على y.
        // -------- الأرقام (تخطيط 7-segment قياسي) --------
        m[U'0'] = { {1,0,5,0},{5,0,5,4},{5,4,5,9},{1,9,5,9},{1,4,1,9},{1,0,1,4} };
        m[U'1'] = { {5,0,5,4},{5,4,5,9} };
        m[U'2'] = { {1,0,5,0},{5,0,5,4},{1,4,5,4},{1,4,1,9},{1,9,5,9} };
        m[U'3'] = { {1,0,5,0},{5,0,5,4},{1,4,5,4},{5,4,5,9},{1,9,5,9} };
        m[U'4'] = { {1,0,1,4},{1,4,5,4},{5,0,5,4},{5,4,5,9} };
        m[U'5'] = { {1,0,5,0},{1,0,1,4},{1,4,5,4},{5,4,5,9},{1,9,5,9} };
        m[U'6'] = { {1,0,5,0},{1,0,1,4},{1,4,5,4},{1,4,1,9},{1,9,5,9},{5,4,5,9} };
        m[U'7'] = { {1,0,5,0},{5,0,5,4},{5,4,5,9} };
        m[U'8'] = { {1,0,5,0},{5,0,5,4},{5,4,5,9},{1,9,5,9},{1,4,1,9},{1,0,1,4},{1,4,5,4} };
        m[U'9'] = { {1,0,5,0},{5,0,5,4},{5,4,5,9},{1,9,5,9},{1,0,1,4},{1,4,5,4} };
        // -------- الحروف اللاتينية الكبيرة (تُستخدَم أيضاً للصغيرة) --------
        m[U'A'] = { {1,9,1,3},{1,3,3,0},{3,0,5,3},{5,3,5,9},{1,6,5,6} };
        m[U'B'] = { {1,0,1,9},{1,0,4,0},{4,0,4,4},{1,4,4,4},{4,4,4,9},{1,9,4,9} };
        m[U'C'] = { {5,0,1,0},{1,0,1,9},{1,9,5,9} };
        m[U'D'] = { {1,0,4,0},{4,0,4,9},{4,9,1,9},{1,9,1,0} };
        m[U'E'] = { {1,0,1,9},{1,0,4,0},{1,4,4,4},{1,9,4,9} };
        m[U'F'] = { {1,0,1,9},{1,0,4,0},{1,4,4,4} };
        m[U'G'] = { {5,0,1,0},{1,0,1,9},{1,9,5,9},{5,9,5,4},{5,4,3,4} };
        m[U'H'] = { {1,0,1,9},{5,0,5,9},{1,4,5,4} };
        m[U'I'] = { {1,0,5,0},{3,0,3,9},{1,9,5,9} };
        m[U'J'] = { {4,0,4,7},{4,7,2,9},{2,9,1,7} };
        m[U'K'] = { {1,0,1,9},{1,4,4,0},{1,4,4,9} };
        m[U'L'] = { {1,0,1,9},{1,9,4,9} };
        m[U'M'] = { {1,0,1,9},{1,0,3,5},{3,5,5,0},{5,0,5,9} };
        m[U'N'] = { {1,0,1,9},{1,0,5,9},{5,0,5,9} };
        m[U'O'] = { {1,0,4,0},{4,0,4,9},{4,9,1,9},{1,9,1,0} };
        m[U'P'] = { {1,0,1,9},{1,0,4,0},{4,0,4,4},{1,4,4,4} };
        m[U'Q'] = { {1,0,4,0},{4,0,4,9},{4,9,1,9},{1,9,1,0},{3,6,5,9} };
        m[U'R'] = { {1,0,1,9},{1,0,4,0},{4,0,4,4},{1,4,4,4},{1,4,4,9} };
        m[U'S'] = { {4,0,1,0},{1,0,1,4},{1,4,4,4},{4,4,4,9},{4,9,1,9} };
        m[U'T'] = { {1,0,5,0},{3,0,3,9} };
        m[U'U'] = { {1,0,1,9},{1,9,4,9},{4,0,4,9} };
        m[U'V'] = { {1,0,3,9},{3,9,5,0} };
        m[U'W'] = { {1,0,2,9},{2,9,3,3},{3,3,4,9},{4,9,5,0} };
        m[U'X'] = { {1,0,5,9},{5,0,1,9} };
        m[U'Y'] = { {1,0,3,5},{5,0,3,5},{3,5,3,9} };
        m[U'Z'] = { {1,0,5,0},{5,0,1,9},{1,9,5,9} };
        // -------- علامات ترقيم/رموز شائعة --------
        m[U'.'] = { {3,8,3,9} };
        m[U','] = { {3,8,2,9} };
        m[U':'] = { {3,3,3,4},{3,6,3,7} };
        m[U';'] = { {3,3,3,4},{3,6,2,7} };
        m[U'-'] = { {1,4,5,4} };
        m[U'_'] = { {1,9,5,9} };
        m[U'+'] = { {1,4,5,4},{3,1,3,7} };
        m[U'/'] = { {1,9,5,0} };
        m[U'\\'] = { {1,0,5,9} };
        m[U'('] = { {3,0,2,2},{2,2,2,7},{2,7,3,9} };
        m[U')'] = { {3,0,4,2},{4,2,4,7},{4,7,3,9} };
        m[U'['] = { {2,0,2,9},{2,0,4,0},{2,9,4,9} };
        m[U']'] = { {4,0,4,9},{2,0,4,0},{2,9,4,9} };
        m[U'{'] = m[U'('];
        m[U'}'] = m[U')'];
        m[U'='] = { {1,3,5,3},{1,6,5,6} };
        m[U'*'] = { {2,3,4,6},{4,3,2,6},{3,2,3,7} };
        m[U'#'] = { {2,0,2,9},{4,0,4,9},{1,3,5,3},{1,6,5,6} };
        m[U'@'] = { {1,0,4,0},{4,0,4,9},{4,9,1,9},{1,9,1,0},{3,4,3,5} };
        m[U'!'] = { {3,0,3,6},{3,8,3,9} };
        m[U'?'] = { {2,0,4,0},{4,0,4,3},{4,3,3,4},{3,4,3,5},{3,8,3,9} };
        m[U'\''] = { {3,0,3,2} };
        m[U'"'] = { {2,0,2,2},{4,0,4,2} };
        m[U'<'] = { {4,0,1,4},{1,4,4,9} };
        m[U'>'] = { {1,0,4,4},{4,4,1,9} };
        m[U'|'] = { {3,0,3,9} };
        m[U'~'] = { {1,5,3,3},{3,3,5,5} };
        m[U'^'] = { {1,3,3,0},{3,0,5,3} };
        m[U'$'] = { {4,0,1,0},{1,0,1,4},{1,4,4,4},{4,4,4,9},{4,9,1,9},{3,0,3,9} };
        // lowercase تُعامَل كـ uppercase (تبسيط مقصود لخط شبكي أحادي الحجم)
        for (char32_t c = U'a'; c <= U'z'; c++) m[c] = m[U'A' + (c - U'a')];
        // -------- حروف عربية مبسَّطة (أشكال منفصلة/غير مُشكَّلة — راجع الملاحظة أعلى الملف) --------
        m[U'ا'] = { {3,0,3,9} };
        m[U'أ'] = { {3,1,3,9},{4,0,4,1} };
        m[U'إ'] = { {3,0,3,8},{2,9,3,9} };
        m[U'آ'] = { {3,0,3,9},{2,0,4,0} };
        m[U'ب'] = { {1,6,5,6},{3,8,3,9} };
        m[U'ت'] = { {1,6,5,6},{2,3,2,4},{4,3,4,4} };
        m[U'ث'] = { {1,6,5,6},{2,3,2,4},{4,3,4,4},{3,1,3,2} };
        m[U'ج'] = { {1,4,4,4},{4,4,3,7},{3,7,1,7},{2,9,3,9} };
        m[U'ح'] = { {1,4,4,4},{4,4,3,7},{3,7,1,7} };
        m[U'خ'] = { {1,4,4,4},{4,4,3,7},{3,7,1,7},{2,1,3,1} };
        m[U'د'] = { {4,1,2,1},{2,1,2,6},{2,6,4,7} };
        m[U'ذ'] = { {4,1,2,1},{2,1,2,6},{2,6,4,7},{2,0,3,0} };
        m[U'ر'] = { {3,2,3,5},{3,5,4,8} };
        m[U'ز'] = { {3,2,3,5},{3,5,4,8},{3,0,4,0} };
        m[U'س'] = { {1,7,5,7},{1,5,1,7},{3,5,3,7},{5,5,5,7} };
        m[U'ش'] = { {1,7,5,7},{1,5,1,7},{3,5,3,7},{5,5,5,7},{2,3,4,3} };
        m[U'ص'] = { {1,5,4,5},{4,5,4,8},{4,8,1,8},{1,8,1,5},{4,5,5,3} };
        m[U'ض'] = { {1,5,4,5},{4,5,4,8},{4,8,1,8},{1,8,1,5},{4,5,5,3},{2,2,3,2} };
        m[U'ط'] = { {3,1,3,8},{1,6,4,6},{4,6,4,8},{4,8,1,8},{1,8,1,6} };
        m[U'ظ'] = { {3,1,3,8},{1,6,4,6},{4,6,4,8},{4,8,1,8},{1,8,1,6},{2,0,3,0} };
        m[U'ع'] = { {4,2,2,3},{2,3,2,6},{2,6,4,8} };
        m[U'غ'] = { {4,2,2,3},{2,3,2,6},{2,6,4,8},{3,0,4,0} };
        m[U'ف'] = { {2,4,4,4},{4,4,4,6},{4,6,2,6},{2,6,2,4},{3,1,3,2} };
        m[U'ق'] = { {2,4,4,4},{4,4,4,6},{4,6,2,6},{2,6,2,4},{2,1,4,1},{3,6,3,9} };
        m[U'ك'] = { {2,1,2,8},{2,1,4,1},{2,5,4,5},{3,5,4,7} };
        m[U'ل'] = { {3,0,3,7},{3,7,1,8} };
        m[U'م'] = { {2,3,4,3},{4,3,4,5},{4,5,2,5},{2,5,2,3},{3,5,3,9} };
        m[U'ن'] = { {1,7,2,6},{2,6,4,6},{4,6,5,7},{3,3,3,4} };
        m[U'ه'] = { {3,4,4,5},{4,5,3,6},{3,6,2,5},{2,5,3,4} };
        m[U'و'] = { {2,2,4,2},{4,2,4,4},{4,4,2,4},{2,4,2,2},{3,4,3,8} };
        m[U'ؤ'] = { {2,2,4,2},{4,2,4,4},{4,4,2,4},{2,4,2,2},{3,4,3,8},{4,1,5,1} };
        m[U'ي'] = { {1,6,3,7},{3,7,5,6},{2,9,3,9} };
        m[U'ئ'] = { {1,6,3,7},{3,7,5,6},{2,9,3,9},{3,5,4,5} };
        m[U'ى'] = { {1,6,3,7},{3,7,5,6} };
        m[U'ء'] = { {3,3,4,4} };
        m[U'ة'] = { {2,5,4,5},{4,5,4,7},{4,7,2,7},{2,7,2,5},{2,3,2,4},{4,3,4,4} };
        m[U'ّ']  = { {2,0,4,1} }; // شدّة (تُعامَل كحرف مستقل مبسَّط، بلا وضع فوق الحرف السابق فعلياً)
        return m;
    }();
    return g;
}

// رسم خط مستقيم بسماكة (thickness) داخل مخزن RGB بأبعاد width×height.
static void drawLine(std::vector<unsigned char>& rgb, int width, int height,
                      int x0, int y0, int x1, int y1, int thickness,
                      unsigned char r, unsigned char g, unsigned char b) {
    auto plot = [&](int px, int py) {
        for (int dy = -(thickness / 2); dy <= thickness / 2; dy++) {
            for (int dx = -(thickness / 2); dx <= thickness / 2; dx++) {
                int x = px + dx, y = py + dy;
                if (x < 0 || y < 0 || x >= width || y >= height) continue;
                size_t idx = (static_cast<size_t>(y) * width + x) * 3;
                rgb[idx] = r; rgb[idx + 1] = g; rgb[idx + 2] = b;
            }
        }
    };
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    while (true) {
        plot(x, y);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

// رسم حرف واحد (cp) بحيث تقع زاويته العلوية اليسرى عند (x0px,y0px)، بتحجيم scale وسماكة خط ثابتة.
static void drawGlyph(std::vector<unsigned char>& rgb, int width, int height,
                      int x0px, int y0px, int scale, char32_t cp,
                      unsigned char r, unsigned char g, unsigned char b) {
    if (cp == U' ' || cp == U'\t') return;
    int thickness = std::max(1, scale / 2 + 1);
    const auto& table = glyphTable();
    auto it = table.find(cp);
    if (it == table.end()) {
        // رمز غير معروف: مربع صغير كإشارة صريحة "حرف غير مدعوم" (بدل تجاهله أو تلوينه اعتباطاً).
        drawLine(rgb, width, height, x0px + 2 * scale, y0px + 4 * scale, x0px + 4 * scale, y0px + 4 * scale, thickness, r, g, b);
        drawLine(rgb, width, height, x0px + 4 * scale, y0px + 4 * scale, x0px + 4 * scale, y0px + 6 * scale, thickness, r, g, b);
        drawLine(rgb, width, height, x0px + 4 * scale, y0px + 6 * scale, x0px + 2 * scale, y0px + 6 * scale, thickness, r, g, b);
        drawLine(rgb, width, height, x0px + 2 * scale, y0px + 6 * scale, x0px + 2 * scale, y0px + 4 * scale, thickness, r, g, b);
        return;
    }
    for (const Seg& seg : it->second) {
        drawLine(rgb, width, height,
                  x0px + seg.x0 * scale, y0px + seg.y0 * scale,
                  x0px + seg.x1 * scale, y0px + seg.y1 * scale,
                  thickness, r, g, b);
    }
}

// إعادة ترتيب نقاط الكود بصرياً (تقريب مبسَّط لخوارزمية bidi): مقاطع عربية متتالية تُعكَس
// داخلياً وتُبدَّل مواضعها مع مقاطع لاتينية/أرقام متتالية بحيث يظهر النص الناتج بالاتجاه
// الصحيح تقريبياً حين يحوي السطر مزيجاً من عربي ولاتيني (كما في "مطوّر (Kotlin)" مثلاً).
static std::vector<char32_t> visualOrder(const std::vector<char32_t>& cps, bool& outHasArabic) {
    outHasArabic = false;
    for (char32_t c : cps) if (isArabicCp(c)) { outHasArabic = true; break; }
    if (!outHasArabic) return cps;

    struct Run { bool arabic; std::vector<char32_t> chars; };
    std::vector<Run> runs;
    for (char32_t c : cps) {
        bool arabic = isArabicCp(c);
        if (c == U' ' && !runs.empty()) { runs.back().chars.push_back(c); continue; }
        if (runs.empty() || runs.back().arabic != arabic) runs.push_back({arabic, {}});
        runs.back().chars.push_back(c);
    }
    std::vector<char32_t> out;
    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
        if (it->arabic) out.insert(out.end(), it->chars.rbegin(), it->chars.rend());
        else out.insert(out.end(), it->chars.begin(), it->chars.end());
    }
    return out;
}

} // namespace rinfont

// ================= ZIP خفيف الوزن (تخزين stored بلا ضغط) — لأجل save/installation عند format=zip =================
// يبني أرشيف .zip صالحاً قياسياً (يُفتح بأي أداة zip عادية)، كل عنصر بلا ضغط (STORED)، وهو كافٍ
// تماماً هنا لأن المحتويات أصلاً نصوص/صور صغيرة، بلا حاجة لربط مكتبة ضغط خارجية (zlib) بالمشروع.
static std::string buildZipArchive(const std::vector<std::pair<std::string, std::string>>& entries) {
    auto putU16 = [](std::string& s, uint16_t v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); };
    auto putU32 = [](std::string& s, uint32_t v) {
        s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF);
        s += static_cast<char>((v >> 16) & 0xFF); s += static_cast<char>((v >> 24) & 0xFF);
    };

    std::string out;
    std::vector<std::string> centralDir;
    for (auto& entry : entries) {
        const std::string& name = entry.first;
        const std::string& data = entry.second;
        uint32_t crc = pngutil::crc32(reinterpret_cast<const unsigned char*>(data.data()), data.size());
        uint32_t offset = static_cast<uint32_t>(out.size());

        std::string local;
        putU32(local, 0x04034b50);
        putU16(local, 20); putU16(local, 0); putU16(local, 0); putU16(local, 0); putU16(local, 0);
        putU32(local, crc);
        putU32(local, static_cast<uint32_t>(data.size()));
        putU32(local, static_cast<uint32_t>(data.size()));
        putU16(local, static_cast<uint16_t>(name.size())); putU16(local, 0);
        local += name; local += data;
        out += local;

        std::string central;
        putU32(central, 0x02014b50);
        putU16(central, 20); putU16(central, 20);
        putU16(central, 0); putU16(central, 0); putU16(central, 0); putU16(central, 0);
        putU32(central, crc);
        putU32(central, static_cast<uint32_t>(data.size()));
        putU32(central, static_cast<uint32_t>(data.size()));
        putU16(central, static_cast<uint16_t>(name.size()));
        putU16(central, 0); putU16(central, 0); putU16(central, 0); putU16(central, 0);
        putU32(central, 0);
        putU32(central, offset);
        central += name;
        centralDir.push_back(central);
    }

    auto centralStart = static_cast<uint32_t>(out.size());
    uint32_t centralSize = 0;
    for (auto& c : centralDir) { out += c; centralSize += static_cast<uint32_t>(c.size()); }

    std::string end;
    putU32(end, 0x06054b50);
    putU16(end, 0); putU16(end, 0);
    putU16(end, static_cast<uint16_t>(entries.size()));
    putU16(end, static_cast<uint16_t>(entries.size()));
    putU32(end, centralSize);
    putU32(end, centralStart);
    putU16(end, 0);
    out += end;
    return out;
}

// يرسم الجدول (صفوفه المسجَّلة عبر row + نمطه المسجَّل عبر style إن وُجد) كصورة PNG حقيقية
// تُظهر نص كل خلية فعلياً (عبر rinfont)، وليس مجرد تلوين اعتباطي للخلايا.
std::string Interpreter::buildTablePng(const std::string& key) const {
    auto rowsIt = tableRows.find(key);
    const std::vector<Value>* rows = (rowsIt != tableRows.end()) ? &rowsIt->second : nullptr;

    size_t rowCount = rows ? rows->size() : 0;
    size_t colCount = 1;
    if (rows) {
        for (auto& row : *rows) {
            if (row.type == Value::Type::ARRAY && row.array) colCount = std::max(colCount, row.array->size());
        }
    }
    if (rowCount == 0) rowCount = 1;

    std::string style;
    auto styleIt = containerStyles.find(key);
    if (styleIt != containerStyles.end()) style = styleIt->second;
    bool dark = (style.find("dark") != std::string::npos);

    // نص كل خلية (بالفهرسة [صف][عمود])، مُفكَّكاً مسبقاً إلى نقاط كود بترتيبها البصري الصحيح.
    struct CellText { std::vector<char32_t> order; bool rtl; };
    std::vector<std::vector<CellText>> cells(rowCount, std::vector<CellText>(colCount, CellText{{}, false}));
    for (size_t ry = 0; ry < rowCount; ry++) {
        const Value* rowVal = (rows && ry < rows->size()) ? &(*rows)[ry] : nullptr;
        for (size_t cx = 0; cx < colCount; cx++) {
            std::string text;
            if (rowVal && rowVal->type == Value::Type::ARRAY && rowVal->array && cx < rowVal->array->size()) {
                text = (*rowVal->array)[cx].toDisplayString();
            }
            auto cps = rinfont::utf8Decode(text);
            bool rtl = false;
            cells[ry][cx].order = rinfont::visualOrder(cps, rtl);
            cells[ry][cx].rtl = rtl;
        }
    }

    // ---- أبعاد الخط والخلايا ----
    const int scale = 2;                              // تحجيم شبكة التصميم (rinfont::GW×GH) إلى بكسل
    const int glyphAdvance = (rinfont::GW + 1) * scale; // تقدّم أفقي أحادي (monospace) لكل حرف
    const int glyphHeightPx = rinfont::GH * scale;
    const int hPad = 8, vPad = 6, gridPx = 2;
    const int minColWidth = 3 * glyphAdvance + 2 * hPad;
    const int rowHeight = glyphHeightPx + 2 * vPad;

    std::vector<int> colWidth(colCount, minColWidth);
    for (size_t ry = 0; ry < rowCount; ry++) {
        for (size_t cx = 0; cx < colCount; cx++) {
            int need = static_cast<int>(cells[ry][cx].order.size()) * glyphAdvance + 2 * hPad;
            colWidth[cx] = std::max(colWidth[cx], need);
        }
    }

    int width = gridPx;
    std::vector<int> colX(colCount + 1, 0);
    for (size_t cx = 0; cx < colCount; cx++) { colX[cx] = width; width += colWidth[cx] + gridPx; }
    colX[colCount] = width;
    int height = static_cast<int>(rowCount) * rowHeight + gridPx;

    // ---- ألوان الثيم ----
    unsigned char bg = dark ? 22 : 250;
    unsigned char rowA = dark ? 30 : 250;    // صفوف زوجية
    unsigned char rowB = dark ? 25 : 242;    // صفوف فردية (تخطيط "zebra" خفيف)
    unsigned char headerBg = dark ? 55 : 210;
    unsigned char gridShade = dark ? 70 : 200;
    unsigned char textR = dark ? 235 : 20, textG = dark ? 235 : 20, textB = dark ? 235 : 20;
    unsigned char headerTextR = dark ? 255 : 10, headerTextG = dark ? 255 : 10, headerTextB = dark ? 255 : 10;

    std::vector<unsigned char> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, bg);

    auto fillRect = [&](int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b) {
        for (int yy = std::max(0, y0); yy < std::min(height, y1); yy++)
            for (int xx = std::max(0, x0); xx < std::min(width, x1); xx++) {
                size_t idx = (static_cast<size_t>(yy) * width + xx) * 3;
                rgb[idx] = r; rgb[idx + 1] = g; rgb[idx + 2] = b;
            }
    };

    for (size_t ry = 0; ry < rowCount; ry++) {
        int y0 = static_cast<int>(ry) * rowHeight + gridPx;
        int y1 = y0 + rowHeight - gridPx;
        unsigned char cr, cg, cb;
        bool isHeader = (ry == 0);
        if (isHeader) { cr = cg = cb = headerBg; }
        else { cr = cg = cb = (ry % 2 == 0) ? rowA : rowB; }
        for (size_t cx = 0; cx < colCount; cx++) {
            fillRect(colX[cx], y0, colX[cx] + colWidth[cx], y1, cr, cg, cb);
            unsigned char tr = isHeader ? headerTextR : textR;
            unsigned char tg = isHeader ? headerTextG : textG;
            unsigned char tb = isHeader ? headerTextB : textB;
            const CellText& ct = cells[ry][cx];
            int textW = static_cast<int>(ct.order.size()) * glyphAdvance;
            int startX = ct.rtl ? (colX[cx] + colWidth[cx] - hPad - textW) : (colX[cx] + hPad);
            int startY = y0 + vPad;
            int cursor = startX;
            for (char32_t cp : ct.order) {
                rinfont::drawGlyph(rgb, width, height, cursor, startY, scale, cp, tr, tg, tb);
                cursor += glyphAdvance;
            }
            if (isHeader) { // تأثير شبه-عريض بسيط للعناوين: إعادة رسم بإزاحة بكسل واحد
                cursor = startX + 1;
                for (char32_t cp : ct.order) {
                    rinfont::drawGlyph(rgb, width, height, cursor, startY, scale, cp, tr, tg, tb);
                    cursor += glyphAdvance;
                }
            }
        }
    }

    // خطوط الشبكة (رأسية بين الأعمدة + أفقية بين الصفوف) + إطار خارجي.
    for (size_t cx = 0; cx <= colCount; cx++) {
        int x0 = colX[cx] - gridPx;
        for (int yy = 0; yy < height; yy++)
            for (int t = 0; t < gridPx; t++) {
                int xx = x0 + t;
                if (xx < 0 || xx >= width) continue;
                size_t idx = (static_cast<size_t>(yy) * width + xx) * 3;
                rgb[idx] = gridShade; rgb[idx + 1] = gridShade; rgb[idx + 2] = gridShade;
            }
    }
    for (size_t ry = 0; ry <= rowCount; ry++) {
        int y0 = static_cast<int>(ry) * rowHeight;
        for (int xx = 0; xx < width; xx++)
            for (int t = 0; t < gridPx; t++) {
                int yy = y0 + t;
                if (yy < 0 || yy >= height) continue;
                size_t idx = (static_cast<size_t>(yy) * width + xx) * 3;
                rgb[idx] = gridShade; rgb[idx + 1] = gridShade; rgb[idx + 2] = gridShade;
            }
    }

    return pngutil::encodeRgbPng(width, height, rgb);
}

std::string Interpreter::buildSaveDocument(const std::string& key, const EnvPtr& containerEnv,
                                            ContainerKind kind, bool simplified) const {
    std::string tag = containerTagName(kind);
    std::string body = serializeEnvBody(containerEnv, simplified);

    // متغيرات env تكفي لأي حاوية عادية، لكن الجدول (TABLE) يخزّن صفوفه ونمطه في بنى منفصلة داخل
    // المفسّر (tableRows/containerStyles) لا داخل بيئته، فيجب إلحاقها هنا نصياً حتى يبقى الملف المحفوظ
    // قابلاً لإعادة القراءة كاملاً عبر container.import/loadInstalled (تُعاد صفوفه إلى tableRows تلقائياً
    // عند إعادة تنفيذ عبارات row/style بداخله).
    if (kind == ContainerKind::TABLE) {
        auto rowsIt = tableRows.find(key);
        if (rowsIt != tableRows.end()) {
            for (auto& row : rowsIt->second) {
                std::string cellsLit = serializeValueLiteral(row);
                body += simplified ? ("row cells=" + cellsLit + ";")
                                    : ("    row cells=" + cellsLit + ";\n");
            }
        }
    } else if (kind == ContainerKind::DOC) {
        // نفس المبدأ: مستندات container.doc/doc مخزَّنة في docStore داخل المفسّر، لا في بيئة الحاوية،
        // فتُلحَق هنا نصياً كسلسلة عبارات 'document id=... fields=...;' قابلة لإعادة القراءة كاملة.
        auto docsIt = docStore.find(key);
        if (docsIt != docStore.end()) {
            for (auto& entry : docsIt->second) {
                std::string idLit = "\"" + escapeStringLiteral(entry.first) + "\"";
                std::string fieldsLit = serializeValueLiteral(entry.second);
                body += simplified ? ("document id=" + idLit + " fields=" + fieldsLit + ";")
                                    : ("    document id=" + idLit + " fields=" + fieldsLit + ";\n");
            }
        }
    }

    // 'style' متاحة الآن داخل أي حاوية من هذه الأنواع (وليس container.table/table حصراً)، وهي مخزَّنة
    // في containerStyles داخل المفسّر لا في بيئة الحاوية، فتُلحَق هنا نصياً حتى يبقى الملف المحفوظ
    // قابلاً لإعادة القراءة كاملاً (تُعاد إلى containerStyles تلقائياً عند إعادة تنفيذ عبارة style بداخله).
    auto styleIt = containerStyles.find(key);
    if (styleIt != containerStyles.end()) {
        std::string styleLit = "\"" + escapeStringLiteral(styleIt->second) + "\"";
        body += simplified ? ("style value=" + styleLit + ";")
                            : ("    style value=" + styleLit + ";\n");
    }

    std::ostringstream doc;
    if (simplified) {
        doc << "@" << tag << "=" << key << " " << body << " .end/" << tag;
    } else {
        doc << "// تم توليد هذا الملف تلقائياً بواسطة Rin (save/installation) - قابل لإعادة الاستيراد عبر container.import أو loadInstalled()\n";
        doc << "@" << tag << "=" << key << "\n";
        doc << body;
        doc << ".end/" << tag << "\n";
    }
    return doc.str();
}

void Interpreter::writeRealFile(const std::string& relPath, const std::string& content, int line, const std::string& who) const {
    std::string fullPath = resolvePath(relPath, line);
    ensureParentDir(fullPath);
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر فتح/إنشاء الملف '" + relPath + "' للكتابة الفعلية على القرص");
    }
    out << content;
    if (!out.good()) {
        throw diagErr(diag::Code::E0036_IOFailure, line, who + ": حدث خطأ أثناء الكتابة الفعلية إلى '" + relPath + "'");
    }
}

void Interpreter::loadInstalledIndex() {
    if (installedIndexLoaded) return;
    installedIndexLoaded = true;
    std::string fullPath = resolvePath("rin_installed/index.rininstall");
    std::ifstream in(fullPath, std::ios::binary);
    if (!in) return; // لا يوجد فهرس بعد؛ أول تشغيل، لا مشكلة
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        std::string name = (tab == std::string::npos) ? line : line.substr(0, tab);
        if (!name.empty()) installedNames.insert(name);
    }
}

void Interpreter::appendInstalledIndex(const std::string& name, const std::string& relPath, bool simplified) const {
    std::string fullPath = resolvePath("rin_installed/index.rininstall");
    ensureParentDir(fullPath);
    std::ofstream out(fullPath, std::ios::binary | std::ios::app);
    if (!out) return; // فشل تسجيل الفهرس لا يجب أن يوقف تنفيذ البرنامج بأكمله
    out << name << "\t" << relPath << "\t" << (simplified ? "1" : "0") << "\t" << static_cast<long long>(std::time(nullptr)) << "\n";
}

bool Interpreter::callTopLevelFunction(const std::vector<StmtPtr>& program,
                                        const std::string& fnName,
                                        std::vector<Value>& args,
                                        const std::vector<std::string>& paramAliases,
                                        std::unordered_map<std::string, Value>& globalsInOut,
                                        std::string& errorOut) {
    callDepth = 0;

    // Hoist every top-level function first (mirrors run()'s hoist pass) so the callee -- and
    // anything it calls in turn -- resolves regardless of source order, and simple recursion works.
    for (const auto& s : program) {
        if (auto fn = std::dynamic_pointer_cast<FunctionStmt>(s)) {
            auto callable = std::make_shared<Callable>();
            callable->declaration = fn;
            callable->closure = globals;
            Value v;
            v.type = Value::Type::FUNCTION;
            v.function = callable;
            globals->define(fn->name, v);
        }
    }
    // Seed every known Warp cell as a plain global, so a zero-arg handler that mutates a
    // same-named global directly (rather than via a parameter) also works.
    for (auto& kv : globalsInOut) globals->define(kv.first, kv.second);

    Value target;
    if (!globals->get(fnName, target) || target.type != Value::Type::FUNCTION) {
        errorOut = "no top-level function named '" + fnName + "'";
        return false;
    }
    auto& params = target.function->declaration->params;
    if (args.size() != params.size()) {
        errorOut = "'" + fnName + "' expects " + std::to_string(params.size()) +
                   " argument(s) but got " + std::to_string(args.size());
        return false;
    }

    auto callEnv = std::make_shared<Environment>(target.function->closure);
    for (size_t i = 0; i < args.size(); i++) callEnv->define(params[i], args[i]);

    try {
        executeBlock(target.function->declaration->body->statements, callEnv);
    } catch (ReturnSignal&) {
        // a bare `return;`/`return expr;` inside an onTap handler is fine -- the value isn't used,
        // any state it already mutated (Warp cells, params) stands as-is.
    } catch (RinError& e) {
        errorOut = e.message;
        return false;
    }

    // Write back: for every originally-seeded name, prefer its final value from an aliased
    // parameter (a parameter shadows a same-named global inside callEnv, so plain global
    // reassignment never reaches it in that case); otherwise read the (possibly reassigned) global.
    for (auto& kv : globalsInOut) {
        bool viaParam = false;
        for (size_t i = 0; i < paramAliases.size() && i < params.size(); i++) {
            if (paramAliases[i] == kv.first) {
                auto it = callEnv->values.find(params[i]);
                if (it != callEnv->values.end()) { kv.second = it->second; viaParam = true; }
                break;
            }
        }
        if (!viaParam) globals->get(kv.first, kv.second);
    }
    return true;
}

std::string Interpreter::run(const std::vector<StmtPtr>& statements) {
    g_diagFile = sourceFile; // مزامنة نظام Diagnostics: الدوال الحرة/lambdas تستخدم g_diagFile
    loadInstalledIndex(); // يحمّل أسماء أي تثبيتات فعلية سابقة على نفس basePath (استمرارية عبر التشغيلات)
    importedPaths.clear(); // كل تشغيل جديد يبدأ بسجل @import نظيف (لا يرث استيرادات تشغيل سابق)
    callDepth = 0; // كل تشغيل جديد يبدأ بعدّاد عمق استدعاء نظيف (احتياطاً عند إعادة استخدام نفس الكائن)
    // First pass: hoist function declarations so they can be called
    // regardless of source order (and support simple recursion).
    for (const auto& s : statements) {
        if (auto fn = std::dynamic_pointer_cast<FunctionStmt>(s)) {
            auto callable = std::make_shared<Callable>();
            callable->declaration = fn;
            callable->closure = globals;
            Value v;
            v.type = Value::Type::FUNCTION;
            v.function = callable;
            globals->define(fn->name, v);
        }
    }
    try {
        for (const auto& s : statements) {
            if (std::dynamic_pointer_cast<FunctionStmt>(s)) continue; // already hoisted
            // البث الحي (streamSink_): نلتقط موضع الكتابة *قبل* تنفيذ هذا الـ statement العلوي
            // و*بعده*، ونمرّر الفرق فقط -- تمامًا ما أضافه هذا الـ statement بالذات، لا أكثر ولا
            // أقل. هذا يمنح دقة "لكل statement علوي" حقيقية بلا أي تعديل على الـ 40+ موضع طباعة
            // الداخلية المتناثرة في هذا الملف (كل منها ما زال يكتب إلى نفس [output] كما كان دائماً).
            const bool streaming = static_cast<bool>(streamSink_);
            std::streamoff before = streaming ? static_cast<std::streamoff>(output.tellp()) : 0;
            execute(s, globals);
            if (streaming) {
                std::streamoff after = output.tellp();
                if (after > before) {
                    streamSink_(output.str().substr(static_cast<size_t>(before), static_cast<size_t>(after - before)));
                }
            }
        }
    } catch (RinError& e) {
        std::string appended;
        if (e.diagnostic) {
            lastDiagnostic_ = e.diagnostic;
            appended = "\n" + diag::renderPlain(*e.diagnostic, diag::globalSourceManager()) + "\n";
        } else {
            appended = "\n[Error line " + std::to_string(e.line) + "]: " + e.message + "\n";
        }
        output << appended;
        lastErrorMessage_ = e.message;
        lastErrorLine_ = e.line;
        if (streamSink_) streamSink_(appended);
    } catch (ReturnSignal&) {
        const char* appended = "\n[Error]: 'return' used outside of a function\n";
        output << appended;
        lastErrorMessage_ = "'return' used outside of a function";
        lastErrorLine_ = 0;
        if (streamSink_) streamSink_(appended);
    }
    return output.str();
}

void Interpreter::executeBlock(const std::vector<StmtPtr>& statements, EnvPtr env) {
    for (const auto& s : statements) execute(s, env);
}

void Interpreter::execute(const StmtPtr& stmt, EnvPtr env) {
    if (auto s = std::dynamic_pointer_cast<ExpressionStmt>(stmt)) {
        evaluate(s->expr, env);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
        // if= : بوابة تنفيذ كاملة — عند falsy، لا يُقيَّم أي شيء آخر إطلاقاً (لا exprs ولا أي سمة
        // أخرى)، فيبقى أمر print معطَّلاً تماماً بلا أي أثر جانبي، تماماً كأنه لم يُكتب أصلاً.
        if (s->ifCond) {
            Value condVal = evaluate(s->ifCond, env);
            if (!condVal.isTruthy()) return;
        }

        std::string sep = " ";
        if (s->sep) {
            Value sepVal = evaluate(s->sep, env);
            if (sepVal.type != Value::Type::STRING) {
                throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'sep' يجب أن يكون نصاً (string)، لكن وُجد نوع " + sepVal.typeName());
            }
            sep = sepVal.str;
        }
        std::string end = "\n";
        if (s->end) {
            Value endVal = evaluate(s->end, env);
            if (endVal.type != Value::Type::STRING) {
                throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'end' يجب أن يكون نصاً (string)، لكن وُجد نوع " + endVal.typeName());
            }
            end = endVal.str;
        }
        bool prettyOn = false;
        if (s->pretty) prettyOn = evaluate(s->pretty, env).isTruthy();

        // بناء المحتوى الأساسي (القيم مفصولة بـ sep؛ pretty= يوسّع أي array/map متداخلة).
        std::ostringstream body;
        for (size_t i = 0; i < s->exprs.size(); i++) {
            if (i > 0) body << sep;
            Value v = evaluate(s->exprs[i], env);
            if (prettyOn && (v.type == Value::Type::ARRAY || v.type == Value::Type::MAP)) {
                body << prettyPrintValue(v, 0);
            } else {
                body << v.toDisplayString();
            }
        }
        std::string content = body.str();

        // upper=/lower= : تحويل حالة الأحرف على المحتوى المُجمَّع بالكامل؛ الاثنان معاً خطأ صريح.
        bool upperOn = s->upper && evaluate(s->upper, env).isTruthy();
        bool lowerOn = s->lower && evaluate(s->lower, env).isTruthy();
        if (upperOn && lowerOn) {
            throw diagErr(diag::Code::E0035_RuntimeError, stmt->line, "'print': لا يمكن استخدام 'upper' و'lower' معاً في نفس الأمر");
        }
        if (upperOn) {
            std::transform(content.begin(), content.end(), content.begin(),
                            [](unsigned char c) { return std::toupper(c); });
        } else if (lowerOn) {
            std::transform(content.begin(), content.end(), content.begin(),
                            [](unsigned char c) { return std::tolower(c); });
        }

        // width=/align= : حشو المحتوى بمسافات لعرض أدنى؛ align بلا width خطأ صريح (لا هدف للمحاذاة).
        if (s->width) {
            Value wv = evaluate(s->width, env);
            if (wv.type != Value::Type::NUMBER) {
                throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'width' يجب أن يكون رقماً، لكن وُجد نوع " + wv.typeName());
            }
            std::string alignMode = "left";
            if (s->align) {
                Value av = evaluate(s->align, env);
                if (av.type != Value::Type::STRING) {
                    throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'align' يجب أن يكون نصاً، لكن وُجد نوع " + av.typeName());
                }
                if (av.str != "left" && av.str != "right" && av.str != "center") {
                    throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'align' يجب أن يكون \"left\" أو \"right\" أو \"center\"، لكن وُجد \"" + av.str + "\"");
                }
                alignMode = av.str;
            }
            int w = static_cast<int>(wv.number);
            int pad = w - static_cast<int>(content.size());
            if (pad > 0) {
                if (alignMode == "left") {
                    content += std::string(static_cast<size_t>(pad), ' ');
                } else if (alignMode == "right") {
                    content = std::string(static_cast<size_t>(pad), ' ') + content;
                } else { // center
                    int leftPad = pad / 2, rightPad = pad - leftPad;
                    content = std::string(static_cast<size_t>(leftPad), ' ') + content + std::string(static_cast<size_t>(rightPad), ' ');
                }
            }
        } else if (s->align) {
            throw diagErr(diag::Code::E0035_RuntimeError, stmt->line, "'print': 'align' يتطلّب تحديد 'width' معه");
        }

        // level= : رمز تصنيف في بداية السطر (يلتقطه RinConsoleFormatter.kt لتلوين الكونسول).
        std::string prefix;
        if (s->level) {
            Value lvv = evaluate(s->level, env);
            if (lvv.type != Value::Type::STRING) {
                throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'level' يجب أن يكون نصاً، لكن وُجد نوع " + lvv.typeName());
            }
            if (lvv.str == "info") prefix = "\u2139\uFE0F ";
            else if (lvv.str == "success") prefix = "\u2705 ";
            else if (lvv.str == "warn" || lvv.str == "warning") prefix = "\u26A0\uFE0F ";
            else if (lvv.str == "error") prefix = "\u274C ";
            else if (lvv.str == "debug") prefix = "\U0001F41E ";
            else throw diagErr(diag::Code::E0035_RuntimeError, stmt->line, "'print': 'level' غير معروف \"" + lvv.str + "\" (المتاح: info, success, warn, error, debug)");
        }
        // label= : وسم مخصص "[TAG] " يُضاف بعد رمز level (أو في البداية إن غاب level).
        if (s->label) {
            Value lbv = evaluate(s->label, env);
            if (lbv.type != Value::Type::STRING) {
                throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'label' يجب أن يكون نصاً، لكن وُجد نوع " + lbv.typeName());
            }
            prefix += "[" + lbv.str + "] ";
        }

        // repeat= : يكرر السطر كاملاً (بادئة + محتوى + end) n مرة؛ n=0 لا يطبع شيئاً إطلاقاً.
        long long times = 1;
        if (s->repeatN) {
            Value rv = evaluate(s->repeatN, env);
            if (rv.type != Value::Type::NUMBER) {
                throw diagErr(diag::Code::E0004_InvalidType, stmt->line, "'print': 'repeat' يجب أن يكون رقماً، لكن وُجد نوع " + rv.typeName());
            }
            if (rv.number < 0) {
                throw diagErr(diag::Code::E0035_RuntimeError, stmt->line, "'print': 'repeat' يجب ألا يكون سالباً");
            }
            times = static_cast<long long>(rv.number);
        }
        for (long long i = 0; i < times; i++) {
            output << prefix << content << end;
        }
        return;
    }
    if (auto s = std::dynamic_pointer_cast<LetStmt>(stmt)) {
        Value v = Value::nil();
        if (s->initializer) v = evaluate(s->initializer, env);
        env->define(s->name, v);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<BlockStmt>(stmt)) {
        auto blockEnv = std::make_shared<Environment>(env);
        executeBlock(s->statements, blockEnv);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        if (evaluate(s->condition, env).isTruthy()) {
            execute(s->thenBranch, env);
        } else if (s->elseBranch) {
            execute(s->elseBranch, env);
        }
        return;
    }
    if (auto s = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
        while (evaluate(s->condition, env).isTruthy()) {
            try {
                execute(s->body, env);
            } catch (BreakSignal&) {
                break;
            } catch (ContinueSignal&) {
                continue;
            }
        }
        return;
    }
    // for (initializer; condition; increment) body -> حلقة for على طراز C (إضافة جديدة additive بحتة)
    // initializer يُنفَّذ مرة واحدة داخل بيئة (Environment) جديدة خاصة بالحلقة كلها، بحيث يبقى أي
    // متغيّر يُعلَن فيها (let i = 0) محصوراً ضمن نطاق الحلقة تماماً كسلوك for المعتاد. condition
    // الغائب يُعامل كـ true دائماً. increment يُنفَّذ بعد كل تكرار، بما في ذلك بعد continue.
    if (auto s = std::dynamic_pointer_cast<ForStmt>(stmt)) {
        auto forEnv = std::make_shared<Environment>(env);
        if (s->initializer) execute(s->initializer, forEnv);
        while (!s->condition || evaluate(s->condition, forEnv).isTruthy()) {
            try {
                execute(s->body, forEnv);
            } catch (BreakSignal&) {
                break;
            } catch (ContinueSignal&) {
                // لا شيء إضافي هنا: increment أدناه ينفَّذ دائماً بعد الـ catch، سواء بـ continue أو
                // بانتهاء الجسم طبيعياً، تماماً كسلوك for القياسي.
            }
            if (s->increment) evaluate(s->increment, forEnv);
        }
        return;
    }
    // plus.condition (condition) { trueBranch } / { falseBranch } -> شرط ثلاثي عام: يقيّم condition
    // مرة واحدة، وينفّذ إحدى الكتلتين (كل كتلة تنفَّذ عبر execute(BlockStmt) العادية، أي تحصل على
    // بيئة/نطاق (Environment) خاص بها تماماً كأي كتلة {} أخرى في اللغة).
    if (auto s = std::dynamic_pointer_cast<PlusConditionStmt>(stmt)) {
        if (evaluate(s->condition, env).isTruthy()) {
            execute(s->trueBranch, env);
        } else {
            execute(s->falseBranch, env);
        }
        return;
    }
    if (auto s = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
        auto callable = std::make_shared<Callable>();
        callable->declaration = s;
        callable->closure = env;
        Value v;
        v.type = Value::Type::FUNCTION;
        v.function = callable;
        env->define(s->name, v);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
        Value v = Value::nil();
        if (s->value) v = evaluate(s->value, env);
        throw ReturnSignal{v};
    }
    if (std::dynamic_pointer_cast<BreakStmt>(stmt)) {
        throw BreakSignal{};
    }
    if (std::dynamic_pointer_cast<ContinueStmt>(stmt)) {
        throw ContinueSignal{};
    }

    // ---- لغة الحاويات/البيانات ----

    if (auto s = std::dynamic_pointer_cast<TextStmt>(stmt)) {
        Value v = Value::nil();
        if (s->initializer) v = evaluate(s->initializer, env);
        if (v.type != Value::Type::STRING) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "'" + s->name + "' من نوع text ويجب أن تكون قيمته نصاً (string)");
        }
        env->define(s->name, v);
        return;
    }

    // ---- Rin Loom (@view / warp / @theme) مربوطة الآن فعلياً بالحاوية ----
    // كانت الأنواع الثلاثة بلا أي معالج هنا إطلاقاً: أي warp/@theme/@view يُكتَب داخل @container كان
    // يُنفَّذ بصمت تامة بلا أي أثر (لا يُعرَّف كمتغيّر ولا يُسجَّل في أي مكان)، لأن Loomtime كانت تعمل
    // فقط كخط أنابيب مستقل (loom::runColdPipeline) يفحص جذر البرنامج العلوي وحده. الآن warp تُعرَّف
    // كمتغيّر حقيقي في بيئة الحاوية الحالية (فيصبح قابلاً للقراءة داخلها كأي متغيّر آخر)، @theme تُعرَّف
    // كقاموس أدوار لونية حقيقي بنفس اسمها، و@view تُسجَّل كجذر واجهة الحاوية الحالية — وكلاهما (warp/
    // theme) يُحفَظان أيضاً بترتيبهما في containerWarpDecls/containerThemeDecls ليستخدمهما
    // loom::runColdPipelineForContainer عند بناء Fabric حقيقي مِن @view هذه الحاوية بالذات.
    if (auto s = std::dynamic_pointer_cast<WarpStmt>(stmt)) {
        Value v = Value::nil();
        if (s->initializer) v = evaluate(s->initializer, env);
        env->define(s->name, v);
        if (!containerStack.empty()) {
            containerWarpDecls[containerStack.back()].push_back(s);
        }
        return;
    }
    if (auto s = std::dynamic_pointer_cast<ThemeStmt>(stmt)) {
        auto themeMap = std::make_shared<MapData>();
        for (auto& attr : s->attrs) {
            Value v = attr.value ? evaluate(attr.value, env) : Value::nil();
            themeMap->push_back({Value::string(attr.key), v});
        }
        if (!s->name.empty()) env->define(s->name, Value::makeMap(themeMap));
        if (!containerStack.empty()) {
            containerThemeDecls[containerStack.back()].push_back(s);
            output << "🎨 theme" << (s->name.empty() ? "" : (" = " + s->name))
                   << " مرتبط بالحاوية " << containerStack.back() << "\n";
        }
        return;
    }
    if (auto s = std::dynamic_pointer_cast<ViewStmt>(stmt)) {
        if (!containerStack.empty()) {
            std::string key = containerStack.back();
            if (!containerViews.count(key)) containerViews[key] = s; // أول @view بداخلها فقط، كنفس مبدأ الجذر العلوي
            output << "🖼️ view" << (s->name.empty() ? "" : (" = " + s->name))
                   << " مرتبط بالحاوية " << key << "\n";
        }
        return;
    }

    if (auto s = std::dynamic_pointer_cast<ContainerStmt>(stmt)) {
        auto containerEnv = std::make_shared<Environment>(env);
        std::string tag = containerTagName(s->kind);
        std::string icon = containerIcon(s->kind);
        output << icon << " " << tag << (s->name.empty() ? "" : (" = " + s->name)) << "\n";
        std::string containerKey = s->name.empty() ? ("#" + std::to_string(containers.size())) : s->name;
        containers[containerKey] = containerEnv;
        containerKinds[containerKey] = s->kind;
        if (!groupStack.empty()) {
            groupMembers[groupStack.back()].push_back(containerKey);
        }
        std::string savedFilePath = currentFilePath;
        currentFilePath.clear(); // كل حاوية تبدأ بمسار ملف نظيف حتى لا يرث container.import مساراً من حاوية سابقة
        containerStack.push_back(containerKey);
        executeBlock(s->body, containerEnv);
        containerStack.pop_back();

        if (s->kind == ContainerKind::IMPORT) {
            // ---- استيراد حقيقي: قراءة ملف .rin آخر من القرص، تحليله، وتنفيذه فعلياً ----
            if (currentFilePath.empty()) {
                throw diagErr(diag::Code::E0028_ImportError, s->line,
                          "'container.import' requires a `file path=\"...\";` inside it to specify what to import");
            }
            std::ifstream in(resolvePath(currentFilePath), std::ios::binary);
            if (!in) {
                throw errWithReason(diag::Code::E0036_IOFailure, s->line,
                                     "container.import: could not open file `" + currentFilePath + "`",
                                     "the file does not exist or is not readable at that path");
            }
            std::ostringstream buf;
            buf << in.rdbuf();
            std::string importedSource = buf.str();
            try {
                Lexer importedLexer(importedSource, currentFilePath);
                auto importedTokens = importedLexer.scanTokens();
                Parser importedParser(importedTokens, currentFilePath);
                auto importedStatements = importedParser.parse();
                // تُنفَّذ عبارات الملف المستورد داخل بيئة حاوية الاستيراد نفسها؛ أي @container بداخله
                // يُسجَّل عالمياً (متاح لاحقاً عبر link/tying/merge)، وأي let/text أعلى المستوى فيه
                // يصبح متغيراً داخل حاوية container.import هذه.
                executeBlock(importedStatements, containerEnv);
            } catch (RinError& e) {
                auto d = diagErr(diag::Code::E0028_ImportError, s->line,
                             "container.import: error inside imported file \"" + currentFilePath + "\"");
                d.diagnostic->withReason("line " + std::to_string(e.line) + " of \"" + currentFilePath + "\": " + e.message);
                if (e.diagnostic) d.diagnostic->withCause(diag::renderShort(*e.diagnostic));
                throw d;
            }
            output << "📥 container.import: تم استيراد \"" << currentFilePath << "\" وتنفيذه بنجاح\n";
        }
        currentFilePath = savedFilePath;

        output << "✅ .end/" << tag << (s->name.empty() ? "" : (" (" + s->name + ")")) << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<ImportStmt>(stmt)) {
        Value pathVal = evaluate(s->path, env);
        if (pathVal.type != Value::Type::STRING) {
            throw diagErr(diag::Code::E0028_ImportError, s->line, "'@import' requires a string path/library name");
        }
        const std::string& rawPath = pathVal.str;

        // ---- تقوية مفهوم lib/*.og.rin: اسم مكتبة "عارٍ" بلا مسار ولا امتداد (مثل @import "math";
        //      أو @import "myhelpers";) يُفهم تلقائياً على أنه lib/<name>.og.rin — سواء كانت مكتبة
        //      مدمجة قياسية أو مكتبة مستخدم حقيقية أنشأها/رفعها المستخدم من قسم "المكتبات" في المحرر
        //      (تُحفَظ داخل مجلد lib/ الخاص بمشروعه). المسارات الصريحة (تحتوي على '/') تبقى تماماً كما
        //      كُتبت لضمان التوافق الكامل مع أي شيفرة سابقة تستخدم @import "lib/xxx.og.rin" مباشرة.
        std::string libPath = rawPath;
        if (libPath.find('/') == std::string::npos) {
            static const std::string kLibExt = ".og.rin";
            bool hasExt = libPath.size() >= kLibExt.size() &&
                          libPath.compare(libPath.size() - kLibExt.size(), kLibExt.size(), kLibExt) == 0;
            if (!hasExt) libPath += kLibExt;
            libPath = "lib/" + libPath;
        }

        // 1) أولاً: هل هذا اسم مكتبة مدمجة داخل المفسّر نفسه (مثل lib/data.og.rin)؟
        //    هذا يجعل @import يعمل مباشرة على أي منصة (بما فيها أندرويد) دون أي ملفات إضافية على القرص.
        const auto& embedded = embeddedRinLibraries();
        auto libIt = embedded.find(libPath);
        bool fromEmbedded = (libIt != embedded.end());

        std::string source;
        if (fromEmbedded) {
            source = libIt->second;
        } else {
            // 2) وإلا: مكتبة/ملف فعلي على القرص (نسبةً إلى basePath، أي مجلد المشروع الحالي)،
            //    بنفس أسلوب container.import. هذا هو نفس المسار الذي يكتب فيه قسم "المكتبات" في
            //    المحرر أي مكتبة ينشئها أو يرفعها المستخدم، فتُستورَد بنفس عبارة @import مباشرة.
            std::ifstream in(resolvePath(libPath), std::ios::binary);
            if (!in) {
                auto d = diagErr(diag::Code::E0029_ModuleNotFound, s->line, "module not found: `" + rawPath + "`");
                d.diagnostic->message = "module not found: `" + rawPath + "`";
                d.diagnostic->withReason("searched for `" + libPath + "` among embedded libraries and the project's lib/ folder")
                 .withHint("create or upload it first from the \"Libraries\" section of the editor");
                throw d;
            }
            std::ostringstream buf;
            buf << in.rdbuf();
            source = buf.str();
        }

        // منع إعادة استيراد نفس المكتبة بنفس أسلوب الاستيراد (مباشر أو باسم مستعار) أكثر من مرة
        // في نفس التشغيل، تماماً كأنظمة الوحدات (modules) المعتادة.
        std::string importKey = libPath + (s->alias.empty() ? "" : ("#as:" + s->alias));
        if (importedPaths.count(importKey)) {
            output << "↺ @import: \"" << libPath << "\" مستورَدة مسبقاً بالفعل (تم تجاهل التكرار)\n";
            return;
        }

        std::vector<StmtPtr> importedStatements;
        try {
            Lexer importedLexer(source, libPath);
            auto importedTokens = importedLexer.scanTokens();
            Parser importedParser(importedTokens, libPath);
            importedStatements = importedParser.parse();
        } catch (RinError& e) {
            auto d = diagErr(diag::Code::E0028_ImportError, s->line, "@import: error parsing library \"" + libPath + "\"");
            d.diagnostic->withReason("line " + std::to_string(e.line) + " of \"" + libPath + "\": " + e.message);
            if (e.diagnostic) d.diagnostic->withCause(diag::renderShort(*e.diagnostic));
            throw d;
        }

        try {
            if (s->alias.empty()) {
                // دمج مباشر: كل fun/let/text أعلى مستوى في المكتبة تصبح متاحة في النطاق الحالي مباشرة،
                // تماماً كـ #include. أي @container بداخل المكتبة يُسجَّل عالمياً كأي حاوية عادية.
                executeBlock(importedStatements, env);
            } else {
                // استيراد باسم مستعار: يُسجَّل كحاوية باسم alias (بنفس دلالات container.import)،
                // فيمكن لاحقاً استخدام link/tying/merge معها كأي حاوية أخرى دون تلويث النطاق الحالي.
                auto libEnv = std::make_shared<Environment>(env);
                executeBlock(importedStatements, libEnv);
                containers[s->alias] = libEnv;
                containerKinds[s->alias] = ContainerKind::IMPORT;
                if (!groupStack.empty()) groupMembers[groupStack.back()].push_back(s->alias);
            }
        } catch (RinError& e) {
            auto d = diagErr(diag::Code::E0028_ImportError, s->line, "@import: error inside library \"" + libPath + "\"");
            d.diagnostic->withReason("line " + std::to_string(e.line) + " of \"" + libPath + "\": " + e.message);
            if (e.diagnostic) d.diagnostic->withCause(diag::renderShort(*e.diagnostic));
            throw d;
        }

        importedPaths.insert(importKey);
        output << (fromEmbedded ? "📦" : "📥") << " @import: تم استيراد \"" << libPath << "\""
               << (s->alias.empty() ? "" : (" باسم '" + s->alias + "'"))
               << (fromEmbedded ? " (مكتبة مدمجة)" : " (من القرص)") << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<ContainerGroupStmt>(stmt)) {
        // مفتاح داخلي للمجموعات المجهولة الاسم، بنفس أسلوب الحاويات المجهولة.
        std::string groupKey = s->name.empty() ? ("#group" + std::to_string(groupEnvs.size())) : s->name;
        auto groupEnv = std::make_shared<Environment>(env); // نطاق خاص بالمجموعة (بدل التنفيذ المباشر داخل البيئة الأب)
        groupEnvs[groupKey] = groupEnv;
        groupMembers.emplace(groupKey, std::vector<std::string>{}); // تضمن وجود مُدخَل حتى لو بقيت فارغة

        // مجموعة متداخلة داخل مجموعة أخرى: سجّلها كعضو في المجموعة الأب أيضاً.
        if (!groupStack.empty()) {
            groupMembers[groupStack.back()].push_back(groupKey);
        }

        output << "🗂️ Containers.Group" << (s->name.empty() ? "" : (" = " + s->name)) << "\n";
        groupStack.push_back(groupKey);
        executeBlock(s->body, groupEnv);
        groupStack.pop_back();

        auto& members = groupMembers[groupKey];
        output << "✅ .end/Containers.Group" << (s->name.empty() ? "" : (" (" + s->name + ")"));
        if (!members.empty()) {
            output << " [تحتوي: ";
            for (size_t i = 0; i < members.size(); i++) {
                if (i) output << ", ";
                output << members[i];
            }
            output << "]";
        }
        output << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<VolumeStmt>(stmt)) {
        output << "📚 Volume" << (s->name.empty() ? "" : (" = " + s->name)) << "\n";
        executeBlock(s->body, env);
        output << "✅ .end/Volume" << (s->name.empty() ? "" : (" (" + s->name + ")")) << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<SectionStmt>(stmt)) {
        auto sectionEnv = std::make_shared<Environment>(env);
        output << "🔹 Section" << (s->name.empty() ? "" : (" = " + s->name)) << "\n";
        executeBlock(s->body, sectionEnv);
        output << "◽ .end/Section" << (s->name.empty() ? "" : (" (" + s->name + ")")) << "\n";
        // يُحفَظ فقط إن كان للقسم اسم (الأقسام المجهولة تبقى زخرفية كالسابق، لا شيء يمكن الرجوع
        // إليه باسمها). إعادة تنفيذ نفس الاسم (مثلاً داخل حلقة) يستبدل بيئته المحفوظة بأحدث تنفيذ،
        // لكن لا يُكرَّر اسمه في sectionOrder.
        if (!s->name.empty()) {
            if (!sectionEnvs.count(s->name)) sectionOrder.push_back(s->name);
            sectionEnvs[s->name] = sectionEnv;
        }
        return;
    }

    if (auto s = std::dynamic_pointer_cast<TranslationsStmt>(stmt)) {
        output << "🌐 Translations\n";
        executeBlock(s->body, env);
        output << "◽ .end/Translations\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<TranslationStmt>(stmt)) {
        translations[s->lang] = s->text;
        output << "🌍 translation [" << s->lang << "] = \"" << s->text << "\"\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<LinkIdDeclStmt>(stmt)) {
        if (containerStack.empty())
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "لا يمكن استخدام 'link.id=' خارج جسم حاوية (container)");
        const std::string& cur = containerStack.back();
        linkIdToContainer[s->id] = cur;
        output << "🏷️ link.id=\"" << s->id << "\" -> " << cur << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<LinkStmt>(stmt)) {
        std::string target = s->target;
        bool byId = !s->byId.empty();
        if (byId) {
            auto it = linkIdToContainer.find(s->byId);
            if (it == linkIdToContainer.end())
                throw diagErr(diag::Code::E0035_RuntimeError, s->line, "لا يمكن تنفيذ link id=\"" + s->byId + "\": لا توجد حاوية مسجَّلة بهذا المعرّف عبر 'link.id='");
            target = it->second;
        }
        bool isContainer = containers.count(target) > 0;
        bool isGroup = groupMembers.count(target) > 0;
        if (!isContainer && !isGroup) {
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "لا يمكن تنفيذ link: '" + target + "' غير معرَّف كحاوية أو كمجموعة (Containers.Group)");
        }
        output << "🔗 link -> " << target << (byId ? (" (id=\"" + s->byId + "\")") : "") << (isGroup ? " (Containers.Group)" : "") << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<TyingStmt>(stmt)) {
        bool isGroup = copyTargetIntoCurrentContainer(s->target, s->line);
        output << "🪢 tying <-> " << s->target << (isGroup ? " (Containers.Group)" : "") << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<MergeStmt>(stmt)) {
        bool isGroup = copyTargetIntoCurrentContainer(s->target, s->line);
        output << "🧬 merge <- " << s->target << (isGroup ? " (Containers.Group)" : "") << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<InstallationStmt>(stmt)) {
        installedNames.insert(s->target);
        bool isContainer = containers.count(s->target) > 0;
        bool isGroup = !isContainer && groupMembers.count(s->target) > 0;
        std::string relPath;

        // installation name format=zip; -> أرشيف zip حقيقي، يعمل لكل المفاهيم: حاوية مفردة (عنصر
        // واحد بداخله)، أو مجموعة كاملة (Containers.Group) حيث تُغلَّف كل حاوية عضو بصيغتها
        // المناسبة (جداول -> .png ، وأي نوع آخر -> .rin). هذا هو "Container.group/Table.zip".
        if (s->format == "zip" && (isContainer || isGroup)) {
            std::vector<std::pair<std::string, std::string>> entries;
            if (isContainer) {
                ContainerKind kind = containerKinds.count(s->target) ? containerKinds[s->target] : ContainerKind::PLAIN;
                if (kind == ContainerKind::TABLE) entries.push_back({s->target + ".png", buildTablePng(s->target)});
                else entries.push_back({s->target + ".rin", buildSaveDocument(s->target, containers[s->target], kind, s->simplified)});
            } else {
                std::vector<std::string> members;
                collectGroupContainerNames(groupMembers, s->target, members);
                for (auto& m : members) {
                    if (!containers.count(m)) continue;
                    ContainerKind kind = containerKinds.count(m) ? containerKinds[m] : ContainerKind::PLAIN;
                    if (kind == ContainerKind::TABLE) entries.push_back({m + ".png", buildTablePng(m)});
                    else entries.push_back({m + ".rin", buildSaveDocument(m, containers[m], kind, s->simplified)});
                }
            }
            relPath = "rin_installed/" + s->target + ".zip";
            std::string zip = buildZipArchive(entries);
            writeRealFile(relPath, zip, s->line, "installation (zip)");
            appendInstalledIndex(s->target, relPath, s->simplified);
            output << "🗜️ installation (zip): " << s->target << " -> " << relPath
                   << " (" << zip.size() << " بايت، " << entries.size() << " عنصر)\n";
            return;
        }

        if (isContainer) {
            ContainerKind kind = containerKinds.count(s->target) ? containerKinds[s->target] : ContainerKind::PLAIN;
            std::string doc = buildSaveDocument(s->target, containers[s->target], kind, s->simplified);
            relPath = "rin_installed/" + s->target + (s->simplified ? ".min.rin" : ".rin");
            writeRealFile(relPath, doc, s->line, "installation");
        } else if (isGroup) {
            std::vector<std::string> members;
            collectGroupContainerNames(groupMembers, s->target, members);
            std::ostringstream doc;
            for (auto& m : members) {
                if (!containers.count(m)) continue;
                ContainerKind kind = containerKinds.count(m) ? containerKinds[m] : ContainerKind::PLAIN;
                doc << buildSaveDocument(m, containers[m], kind, s->simplified);
                if (!s->simplified) doc << "\n";
            }
            relPath = "rin_installed/" + s->target + (s->simplified ? ".min.rin" : ".rin");
            writeRealFile(relPath, doc.str(), s->line, "installation");
        }
        appendInstalledIndex(s->target, relPath, s->simplified);
        output << "⚙️ installation" << (s->simplified ? " (simplified)" : "") << ": " << s->target;
        if (!relPath.empty()) output << " -> " << relPath << " (تم الحفظ فعلياً على القرص، قابل للتحميل لاحقاً عبر loadInstalled(\"" << s->target << "\"))";
        else output << " (تسجيل اسم فقط، بلا بيانات حاوية مرتبطة به)";
        output << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<SaveStmt>(stmt)) {
        if (containerStack.empty() || !containers.count(containerStack.back())) {
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "'save' يجب أن تُستخدم داخل حاوية (container) حالية لحفظ متغيراتها فعلياً");
        }
        std::string key = containerStack.back();
        ContainerKind kind = containerKinds.count(key) ? containerKinds[key] : ContainerKind::PLAIN;

        // table.save/png : تصدير حقيقي لصورة PNG تمثّل الجدول، متاح فقط لحاويات الجدول
        // (@container.table أو @table)، ويعمل بغضّ النظر عن الشكل الذي فُتحت به.
        if (s->format == "png") {
            if (kind != ContainerKind::TABLE) {
                throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "save format=png متاحة فقط لحاوية جدول (@container.table أو @table)، وليس لـ '" +
                                containerTagName(kind) + "'");
            }
            std::string rawPath;
            if (s->path) rawPath = evaluate(s->path, env).toDisplayString();
            else if (!currentFilePath.empty()) rawPath = currentFilePath;
            else rawPath = key + ".png";
            std::string png = buildTablePng(key);
            writeRealFile(rawPath, png, s->line, "save (png)");
            output << "🖼️ save (png) -> " << rawPath << " (" << png.size() << " بايت)\n";
            return;
        }

        // save format=zip : يعمل لجميع المفاهيم (أي نوع حاوية)، ويُغلِّف نسخة حاوية واحدة كأرشيف zip.
        if (s->format == "zip") {
            std::string rawPath;
            if (s->path) rawPath = evaluate(s->path, env).toDisplayString();
            else rawPath = key + ".zip";
            std::vector<std::pair<std::string, std::string>> entries;
            if (kind == ContainerKind::TABLE) {
                entries.push_back({key + ".png", buildTablePng(key)});
            } else {
                entries.push_back({key + ".rin", buildSaveDocument(key, containers[key], kind, s->simplified)});
            }
            std::string zip = buildZipArchive(entries);
            writeRealFile(rawPath, zip, s->line, "save (zip)");
            output << "🗜️ save (zip) -> " << rawPath << " (" << zip.size() << " بايت، " << entries.size() << " عنصر)\n";
            return;
        }

        std::string rawPath;
        if (s->path) rawPath = evaluate(s->path, env).toDisplayString();
        else if (!currentFilePath.empty()) rawPath = currentFilePath;
        else rawPath = key + (s->simplified ? ".min.rin" : ".rin"); // مسار افتراضي معتمد على اسم الحاوية

        std::string doc = buildSaveDocument(key, containers[key], kind, s->simplified);
        writeRealFile(rawPath, doc, s->line, "save");

        output << "💾 save" << (s->simplified ? " (simplified)" : "") << " -> " << rawPath
               << " (تم الحفظ فعلياً على القرص، " << doc.size() << " بايت)\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<FileStmt>(stmt)) {
        currentFilePath = evaluate(s->path, env).toDisplayString();
        output << "📄 file path = \"" << currentFilePath << "\"\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<RouteStmt>(stmt)) {
        if (containerStack.empty()) {
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "عبارة 'route' يجب أن تُستخدم داخل @container.api");
        }
        Value methodVal = evaluate(s->method, env);
        if (methodVal.type != Value::Type::STRING) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "route: قيمة 'method' يجب أن تكون نصاً (مثال: \"GET\")");
        }
        Value pathVal = evaluate(s->path, env);
        if (pathVal.type != Value::Type::STRING) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "route: قيمة 'path' يجب أن تكون نصاً (مثال: \"/users/1\")");
        }
        Value statusVal = evaluate(s->status, env);
        if (statusVal.type != Value::Type::NUMBER) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "route: قيمة 'status' يجب أن تكون رقماً (مثال: 200)");
        }
        Value bodyVal = evaluate(s->body, env);

        ApiRoute route;
        route.method = toUpperAscii(methodVal.str);
        route.path = pathVal.str;
        route.status = statusVal.number;
        route.body = bodyVal;
        apiRoutes[containerStack.back()].push_back(route);

        output << "🌐 route [" << route.method << " " << route.path << "] -> " << statusVal.toDisplayString() << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<RowStmt>(stmt)) {
        if (containerStack.empty() || containerKinds[containerStack.back()] != ContainerKind::TABLE) {
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "عبارة 'row' يجب أن تُستخدم داخل @container.table أو @table");
        }
        Value cells = evaluate(s->cells, env);
        if (cells.type != Value::Type::ARRAY) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "row: قيمة 'cells' يجب أن تكون مصفوفة (مثال: row cells=[1, 2, 3];)");
        }
        tableRows[containerStack.back()].push_back(cells);
        output << "▦ row -> " << cells.toDisplayString() << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<StyleStmt>(stmt)) {
        // 'style' كانت خاصة بالجدول (container.table/table) فقط، وعُمِّمت الآن (مفاهيم التنسيق
        // والستايل) لتعمل أيضاً داخل container.object/Object، container.portal/portal، و
        // container.block/block.
        ContainerKind currentKind = containerStack.empty() ? ContainerKind::PLAIN
                                                            : containerKinds[containerStack.back()];
        bool allowed = currentKind == ContainerKind::TABLE || currentKind == ContainerKind::OBJECT ||
                        currentKind == ContainerKind::PORTAL || currentKind == ContainerKind::BLOCK ||
                        currentKind == ContainerKind::STICKER;
        if (containerStack.empty() || !allowed) {
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "عبارة 'style' يجب أن تُستخدم داخل @container.table/@table أو "
                            "@container.object/@Object أو @container.portal/@portal أو @container.block/@block "
                            "أو @container.sticker/@sticker");
        }
        Value v = evaluate(s->value, env);
        if (v.type != Value::Type::STRING) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "style: قيمة 'value' يجب أن تكون نصاً (مثال: style value=\"style://dark\";)");
        }
        containerStyles[containerStack.back()] = v.str;
        output << "🎨 style -> " << v.str << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<DocumentStmt>(stmt)) {
        if (containerStack.empty() || containerKinds[containerStack.back()] != ContainerKind::DOC) {
            throw diagErr(diag::Code::E0014_InvalidContainer, s->line, "عبارة 'document' يجب أن تُستخدم داخل @container.doc أو @doc");
        }
        Value idVal = evaluate(s->id, env);
        if (idVal.type != Value::Type::STRING) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "document: قيمة 'id' يجب أن تكون نصاً (مثال: document id=\"u1\" fields={...};)");
        }
        Value fieldsVal = evaluate(s->fields, env);
        if (fieldsVal.type != Value::Type::MAP) {
            throw diagErr(diag::Code::E0004_InvalidType, s->line, "document: قيمة 'fields' يجب أن تكون كائناً/قاموساً (مثال: fields={ name: \"Ali\" };)");
        }
        std::string containerName = containerStack.back();
        auto errors = schemaErrors(containerName, fieldsVal);
        if (!errors.empty()) {
            {
                auto d = diagErr(diag::Code::E0019_SchemaViolation, s->line, "schema violation in `" + containerName + "`");
                d.diagnostic->withReason(joinErrors(errors));
                throw d;
            }
        }
        auto& docs = docStore[containerName];
        bool updated = false;
        for (auto& entry : docs) {
            if (entry.first == idVal.str) { entry.second = fieldsVal; updated = true; break; }
        }
        if (!updated) docs.push_back({idVal.str, fieldsVal});
        refreshIndexesForContainer(containerName);
        notifyWatchers(containerName, idVal.str, fieldsVal, updated ? "update" : "insert", s->line);
        output << (updated ? "🔄 document (تحديث) -> " : "🧾 document (إدراج) -> ")
               << idVal.str << " = " << fieldsVal.toDisplayString() << "\n";
        return;
    }
}

bool Interpreter::copyTargetIntoCurrentContainer(const std::string& target, int line) {
    bool isContainer = containers.count(target) > 0;
    bool isGroup = groupMembers.count(target) > 0;
    if (!isContainer && !isGroup) {
        throw diagErr(diag::Code::E0014_InvalidContainer, line, "لا يمكن تنفيذ tying/merge: '" + target + "' غير معرَّف كحاوية أو كمجموعة (Containers.Group)");
    }
    if (containerStack.empty() || !containers.count(containerStack.back())) {
        return isGroup; // لا توجد حاوية حالية لنسخ المتغيرات إليها (نادراً ما يحدث هذا خارج أي container)
    }
    auto currentEnv = containers[containerStack.back()];
    if (isContainer) {
        auto otherEnv = containers[target];
        for (auto& kv : otherEnv->values) currentEnv->values[kv.first] = kv.second;
        return false;
    }
    // target مجموعة (Containers.Group): انسخ متغيرات كل الحاويات الأعضاء بداخلها،
    // بما فيها أعضاء أي مجموعات فرعية متداخلة، بترتيب الإدخال.
    std::vector<std::string> memberContainers;
    collectGroupContainerNames(groupMembers, target, memberContainers);
    for (auto& memberName : memberContainers) {
        if (!containers.count(memberName)) continue;
        auto otherEnv = containers[memberName];
        for (auto& kv : otherEnv->values) currentEnv->values[kv.first] = kv.second;
    }
    return true;
}

// انظر إعلانها في rin_interpreter.h (قسم schema) للشرح الكامل.
std::vector<std::string> Interpreter::schemaErrors(const std::string& container, const Value& fields) const {
    std::vector<std::string> errors;
    auto it = schemaStore.find(container);
    if (it == schemaStore.end()) return errors;
    for (auto& fieldType : it->second) {
        const std::string& field = fieldType.first;
        const std::string& type = fieldType.second;
        bool found = false;
        Value val;
        if (fields.type == Value::Type::MAP && fields.map) {
            for (auto& kv : *fields.map) {
                if (kv.first.type == Value::Type::STRING && kv.first.str == field) { found = true; val = kv.second; break; }
            }
        }
        if (!found) { errors.push_back("الحقل '" + field + "' مفقود"); continue; }
        if (!valueMatchesSchemaType(val, type)) {
            errors.push_back("الحقل '" + field + "' يجب أن يكون من نوع " + type + " لكنه " + val.typeName());
        }
    }
    return errors;
}

// انظر إعلانها في rin_interpreter.h (قسم index) للشرح الكامل. تُعاد بناء كل الحقول المفهرسة
// لهذه الحاوية من الصفر انطلاقاً من docStore الحالي.
void Interpreter::refreshIndexesForContainer(const std::string& container) {
    auto cIt = indexStore.find(container);
    if (cIt == indexStore.end()) return;
    auto docIt = docStore.find(container);
    for (auto& fieldEntry : cIt->second) {
        auto& buckets = fieldEntry.second;
        buckets.clear();
        if (docIt == docStore.end()) continue;
        for (auto& entry : docIt->second) {
            const Value& doc = entry.second;
            if (doc.type != Value::Type::MAP || !doc.map) continue;
            for (auto& kv : *doc.map) {
                if (kv.first.type == Value::Type::STRING && kv.first.str == fieldEntry.first) {
                    buckets[reprValue(kv.second)].push_back(entry.first);
                    break;
                }
            }
        }
    }
}

// انظر إعلانها في rin_interpreter.h (قسم watch/subscribe) للشرح الكامل.
void Interpreter::notifyWatchers(const std::string& container, const std::string& id, const Value& doc,
                                  const std::string& event, int line) {
    auto it = docWatchers.find(container);
    if (it == docWatchers.end()) return;
    for (auto& fn : it->second) {
        if (fn.type != Value::Type::FUNCTION || !fn.function) continue;
        std::vector<Value> cbArgs{Value::string(id), doc, Value::string(event)};
        callFunction(fn.function, cbArgs, line);
    }
}

Value Interpreter::callFunction(const std::shared_ptr<Callable>& fn, std::vector<Value>& args, int line) {
    if (args.size() != fn->declaration->params.size()) {
        auto d = diagErr(diag::Code::E0007_InvalidArguments, line,
                     "invalid number of arguments for `" + fn->declaration->name + "`");
        d.diagnostic->expected = std::to_string(fn->declaration->params.size()) + " argument(s)";
        d.diagnostic->found = std::to_string(args.size()) + " argument(s)";
        d.diagnostic->withReason("function `" + fn->declaration->name + "` expects " +
                                  std::to_string(fn->declaration->params.size()) + " parameter(s)");
        throw d;
    }
    // حارس عمق الاستدعاء: بلا هذا الفحص، دالة تتكرّر ذاتياً بلا حالة توقّف (نسيان شرط الإنهاء —
    // خطأ برمجي شائع جداً، وليس فقط سيناريو هجوم) تُسبِّب Stack Overflow حقيقياً في مكدّس C++ الأصلي
    // (segmentation fault لا يمكن لأي try/catch اعتراضه)، فيُسقِط التطبيق كاملاً. الآن يُحوَّل هذا إلى
    // RinError عادي وقابل للعرض في الكونسول، تماماً كأي خطأ Rin آخر.
    if (callDepth >= kMaxCallDepth) {
        auto d = diagErr(diag::Code::E0035_RuntimeError, line,
                     "maximum function call depth exceeded (" + std::to_string(kMaxCallDepth) + ")");
        d.diagnostic->withReason("this is almost always unbounded recursion (a missing base case)")
         .withHint("check that every recursive path in this function eventually returns without calling itself again");
        throw d;
    }
    callDepth++;
    struct DepthGuard {
        int& depth;
        ~DepthGuard() { depth--; }
    } guard{callDepth};

    auto callEnv = std::make_shared<Environment>(fn->closure);
    for (size_t i = 0; i < args.size(); i++) {
        callEnv->define(fn->declaration->params[i], args[i]);
    }
    try {
        executeBlock(fn->declaration->body->statements, callEnv);
    } catch (ReturnSignal& r) {
        return r.value;
    }
    return Value::nil();
}

// استُخرِجت حرفياً من حالة CallExpr داخل evaluate() القديمة (نفس الترتيب: builtinOps الصريحة، ثم
// natives، ثم دالة Rin مُعرَّفة) بلا أي تغيير سلوكي، لتُشارَك مع evaluatePipelineFlow (RinFlow) أدناه
// دون تكرار نفس منطق الفرز/الأخطاء مرتين (قسم 17: توافقية كاملة مع كل الكود القديم الذي يعتمد على
// هذا الترتيب بالضبط).
Value Interpreter::invokeCallee(const std::string& callee, std::vector<Value>& args, int line, const EnvPtr& env) {
    static const std::unordered_set<std::string> builtinOps = {
        "Addition", "Subtraction", "Multiplication", "Equal"
    };
    if (builtinOps.count(callee)) {
        if (args.size() != 2) {
            auto d = diagErr(diag::Code::E0007_InvalidArguments, line, "`" + callee + "` requires exactly 2 arguments");
            d.diagnostic->expected = "2 argument(s)";
            d.diagnostic->found = std::to_string(args.size()) + " argument(s)";
            throw d;
        }
        Value a = args[0];
        Value b = args[1];
        if (callee == "Equal") {
            return Value::boolean_(valuesEqual(a, b));
        }
        if (callee == "Addition" && (a.type == Value::Type::STRING || b.type == Value::Type::STRING)) {
            return Value::string(a.toDisplayString() + b.toDisplayString());
        }
        if (a.type != Value::Type::NUMBER || b.type != Value::Type::NUMBER) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "`" + callee + "` requires two numbers");
        }
        if (callee == "Addition") return Value::num(a.number + b.number);
        if (callee == "Subtraction") return Value::num(a.number - b.number);
        if (callee == "Multiplication") return Value::num(a.number * b.number);
    }

    auto nativeIt = natives.find(callee);
    if (nativeIt != natives.end()) {
        return nativeIt->second(args, line);
    }

    Value calleeVal;
    if (!env->get(callee, calleeVal) || calleeVal.type != Value::Type::FUNCTION) {
        throw unknownFunctionErr(callee, line);
    }
    return callFunction(calleeVal.function, args, line);
}

Value Interpreter::evaluate(const ExprPtr& expr, EnvPtr env) {
    if (auto e = std::dynamic_pointer_cast<LiteralExpr>(expr)) {
        switch (e->kind) {
            case LiteralExpr::Kind::NUMBER: return Value::num(e->number);
            case LiteralExpr::Kind::STRING: return Value::string(e->str);
            case LiteralExpr::Kind::BOOL: return Value::boolean_(e->boolean);
            case LiteralExpr::Kind::NIL: return Value::nil();
        }
    }
    if (auto e = std::dynamic_pointer_cast<VariableExpr>(expr)) {
        Value v;
        if (!env->get(e->name, v)) {
            throw undefinedVariableErr(e->name, e->line, env);
        }
        return v;
    }
    if (auto e = std::dynamic_pointer_cast<AssignExpr>(expr)) {
        Value v = evaluate(e->value, env);
        if (!env->assign(e->name, v)) {
            throw undefinedVariableErr(e->name, e->line, env);
        }
        return v;
    }
    if (auto e = std::dynamic_pointer_cast<LogicalExpr>(expr)) {
        Value left = evaluate(e->left, env);
        if (e->op == TokenType::OR) {
            if (left.isTruthy()) return left;
        } else { // AND
            if (!left.isTruthy()) return left;
        }
        return evaluate(e->right, env);
    }
    if (auto e = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
        Value right = evaluate(e->right, env);
        if (e->op == TokenType::MINUS) {
            if (right.type != Value::Type::NUMBER)
                throw diagErr(diag::Code::E0004_InvalidType, e->line, "unary `-` operand must be a number, found `" + right.typeName() + "`");
            return Value::num(-right.number);
        }
        if (e->op == TokenType::BANG) return Value::boolean_(!right.isTruthy());
    }
    if (auto e = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        Value left = evaluate(e->left, env);
        Value right = evaluate(e->right, env);
        switch (e->op) {
            case TokenType::PLUS:
                if (left.type == Value::Type::STRING || right.type == Value::Type::STRING) {
                    return Value::string(left.toDisplayString() + right.toDisplayString());
                }
                if (left.type == Value::Type::NUMBER && right.type == Value::Type::NUMBER)
                    return Value::num(left.number + right.number);
                throw diagErr(diag::Code::E0004_InvalidType, e->line,
                          "`+` operands must be numbers or strings, found `" + left.typeName() + "` and `" + right.typeName() + "`");
            case TokenType::MINUS:
                requireNumbers(left, right, "-", e->line);
                return Value::num(left.number - right.number);
            case TokenType::STAR:
                requireNumbers(left, right, "*", e->line);
                return Value::num(left.number * right.number);
            case TokenType::SLASH:
                requireNumbers(left, right, "/", e->line);
                if (right.number == 0) throw errWithReason(diag::Code::E0035_RuntimeError, e->line,
                                                            "division by zero", "the right-hand side of `/` evaluated to 0");
                return Value::num(left.number / right.number);
            case TokenType::PERCENT:
                requireNumbers(left, right, "%", e->line);
                if (right.number == 0) throw errWithReason(diag::Code::E0035_RuntimeError, e->line,
                                                            "division by zero", "the right-hand side of `%` evaluated to 0");
                return Value::num(std::fmod(left.number, right.number));
            case TokenType::GREATER:
                requireNumbers(left, right, ">", e->line);
                return Value::boolean_(left.number > right.number);
            case TokenType::GREATER_EQUAL:
                requireNumbers(left, right, ">=", e->line);
                return Value::boolean_(left.number >= right.number);
            case TokenType::LESS:
                requireNumbers(left, right, "<", e->line);
                return Value::boolean_(left.number < right.number);
            case TokenType::LESS_EQUAL:
                requireNumbers(left, right, "<=", e->line);
                return Value::boolean_(left.number <= right.number);
            case TokenType::EQUAL_EQUAL:
                return Value::boolean_(valuesEqual(left, right));
            case TokenType::BANG_EQUAL:
                return Value::boolean_(!valuesEqual(left, right));
            default: break;
        }
    }
    if (auto e = std::dynamic_pointer_cast<CallExpr>(expr)) {
        // RinFlow: سلسلة |> كاملة (جذرها هنا) مع جلسة Flow نشطة فعلاً -> تُنفَّذ عبر محرّك RinFlow
        // (Flow Graph حقيقي + Execution Events + تتبّع Node بالكامل)، بدل التقييم العادي أدناه. بلا
        // جلسة نشطة (الحالة الافتراضية دائماً لِـ run() العادي) هذا الفرع لا يُؤخَذ أبداً، فتبقى
        // نفس السلسلة تُقيَّم بنفس الطريقة العادية تماماً كما كانت قبل RinFlow (توافقية كاملة).
        if (e->isPipelineRoot && activeFlowSession_) {
            return evaluatePipelineFlow(e, env);
        }

        std::vector<Value> args;
        for (auto& a : e->args) args.push_back(evaluate(a, env));
        return invokeCallee(e->callee, args, e->line, env);
    }
    if (auto e = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        auto arr = std::make_shared<ArrayData>();
        arr->reserve(e->elements.size());
        for (auto& el : e->elements) arr->push_back(evaluate(el, env));
        return Value::makeArray(arr);
    }
    if (auto e = std::dynamic_pointer_cast<MapExpr>(expr)) {
        auto m = std::make_shared<MapData>();
        for (auto& entry : e->entries) {
            Value key = evaluate(entry.key, env);
            Value val = evaluate(entry.value, env);
            bool replaced = false;
            for (auto& kv : *m) {
                if (valuesEqual(kv.first, key)) { kv.second = val; replaced = true; break; }
            }
            if (!replaced) m->push_back({key, val});
        }
        return Value::makeMap(m);
    }
    if (auto e = std::dynamic_pointer_cast<IndexExpr>(expr)) {
        Value obj = evaluate(e->object, env);
        Value idx = evaluate(e->index, env);
        if (obj.type == Value::Type::ARRAY) {
            if (idx.type != Value::Type::NUMBER) {
                throw errWithReason(diag::Code::E0021_InvalidIndex, e->line,
                                     "array index must be a number", "found a `" + idx.typeName() + "` index instead");
            }
            long i = static_cast<long>(idx.number);
            if (i < 0 || static_cast<size_t>(i) >= obj.array->size()) {
                auto d = diagErr(diag::Code::E0021_InvalidIndex, e->line, "index out of range: " + std::to_string(i));
                d.diagnostic->withReason("this array has " + std::to_string(obj.array->size()) + " element(s) (valid indices: 0.." +
                                          (obj.array->empty() ? std::string("-") : std::to_string(obj.array->size() - 1)) + ")");
                throw d;
            }
            return (*obj.array)[static_cast<size_t>(i)];
        }
        if (obj.type == Value::Type::MAP) {
            for (auto& kv : *obj.map) {
                if (valuesEqual(kv.first, idx)) return kv.second;
            }
            return Value::nil();
        }
        if (obj.type == Value::Type::STRING) {
            if (idx.type != Value::Type::NUMBER) {
                throw errWithReason(diag::Code::E0021_InvalidIndex, e->line,
                                     "string index must be a number", "found a `" + idx.typeName() + "` index instead");
            }
            long i = static_cast<long>(idx.number);
            if (i < 0 || static_cast<size_t>(i) >= obj.str.size()) {
                auto d = diagErr(diag::Code::E0021_InvalidIndex, e->line, "index out of range: " + std::to_string(i));
                d.diagnostic->withReason("this string has " + std::to_string(obj.str.size()) + " character(s)");
                throw d;
            }
            return Value::string(std::string(1, obj.str[static_cast<size_t>(i)]));
        }
        throw diagErr(diag::Code::E0004_InvalidType, e->line, "cannot index a value of type `" + obj.typeName() + "`");
    }
    if (auto e = std::dynamic_pointer_cast<IndexSetExpr>(expr)) {
        Value obj = evaluate(e->object, env);
        Value idx = evaluate(e->index, env);
        Value val = evaluate(e->value, env);
        if (obj.type == Value::Type::ARRAY) {
            if (idx.type != Value::Type::NUMBER) {
                throw errWithReason(diag::Code::E0021_InvalidIndex, e->line,
                                     "array index must be a number", "found a `" + idx.typeName() + "` index instead");
            }
            long i = static_cast<long>(idx.number);
            if (i == static_cast<long>(obj.array->size())) {
                obj.array->push_back(val); // السماح بالتوسّع عبر arr[len(arr)] = value
            } else if (i < 0 || static_cast<size_t>(i) >= obj.array->size()) {
                auto d = diagErr(diag::Code::E0021_InvalidIndex, e->line, "index out of range: " + std::to_string(i));
                d.diagnostic->withReason("this array has " + std::to_string(obj.array->size()) + " element(s)")
                 .withHint("use `arr[" + std::to_string(obj.array->size()) + "] = value` to append instead");
                throw d;
            } else {
                (*obj.array)[static_cast<size_t>(i)] = val;
            }
            return val;
        }
        if (obj.type == Value::Type::MAP) {
            for (auto& kv : *obj.map) {
                if (valuesEqual(kv.first, idx)) { kv.second = val; return val; }
            }
            obj.map->push_back({idx, val});
            return val;
        }
        throw diagErr(diag::Code::E0004_InvalidType, e->line, "cannot assign into a value of type `" + obj.typeName() + "` via `[]`");
    }
    return Value::nil();
}

// يبحث عن أول route مطابق (method + path) مسجَّل داخل container.api صاحب المفتاح المُعطى، ويُعيد
// قيمة map حقيقية {status, ok, body}. إن لم يوجد تطابق، يُعيد {status: 404, ok: false, error: ...}
// دون رمي استثناء — تماماً كما يتصرف عميل HTTP حقيقي أمام رد 404.
Value Interpreter::performApiCall(const std::string& containerKey, const std::string& method,
                                   const std::string& path, int line, const Value& bodyValue) {
    std::string wantMethod = toUpperAscii(method);

    // ---- تفعيل فعلي: حاوية container.api سجَّلت apiEndpoint حقيقياً بنفس اسمها (عبر apiRegister
    // بداخلها) -> نفّذ طلب شبكة حقيقياً فعلياً بدل مطابقة route الوهمية. هذا هو ما يجعل container.api
    // "ميزة فعالة" فعلاً: نفس عبارة route تبقى صالحة كتوثيق/عقد متوقَّع، لكن التنفيذ صار حقيقياً.
    if (apiEndpoints.count(containerKey)) {
        return performRealApiCall(containerKey, wantMethod, path, bodyValue, line);
    }

    auto it = apiRoutes.find(containerKey);
    if (it != apiRoutes.end()) {
        for (auto& route : it->second) {
            if (route.method == wantMethod && route.path == path) {
                auto result = std::make_shared<MapData>();
                result->push_back({Value::string("status"), Value::num(route.status)});
                result->push_back({Value::string("ok"), Value::boolean_(route.status >= 200 && route.status < 300)});
                result->push_back({Value::string("body"), route.body});
                return Value::makeMap(result);
            }
        }
    }
    auto result = std::make_shared<MapData>();
    result->push_back({Value::string("status"), Value::num(404)});
    result->push_back({Value::string("ok"), Value::boolean_(false)});
    result->push_back({Value::string("body"), Value::nil()});
    result->push_back({Value::string("error"),
                        Value::string("لا يوجد route مطابق لـ " + wantMethod + " " + path +
                                      " داخل container.api = " + containerKey)});
    return Value::makeMap(result);
}

// ================= أدوات HTTP الحقيقي: تحويل Value <-> نوع الشبكة الفعلي =================

// map (نص -> نص) قادم من كود Rin -> قائمة ترويسات HTTP حقيقية. nil = بلا ترويسات إضافية.
static http::HeaderList headersFromValue(const Value& v, const std::string& fn, int line) {
    http::HeaderList out;
    if (v.type == Value::Type::NIL) return out;
    if (v.type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' يتوقّع الترويسات كقاموس {مفتاح: قيمة} أو nil");
    for (auto& kv : *v.map) out.push_back({kv.first.toDisplayString(), kv.second.toDisplayString()});
    return out;
}

// جسم الطلب: نص يُرسَل كما هو حرفياً؛ أي نوع آخر (map/array/رقم/bool) يُرمَّز تلقائياً JSON حقيقياً
// (ergonomics: apiPost(name, path, { title: "x" }) يعمل مباشرة بلا jsonEncode يدوي)، ويُضاف
// Content-Type: application/json تلقائياً حين لا توجد ترويسة Content-Type مذكورة أصلاً.
static std::string bodyToString(const Value& v, http::HeaderList& headers) {
    if (v.type == Value::Type::NIL) return "";
    if (v.type == Value::Type::STRING) return v.str;
    bool hasContentType = false;
    for (auto& h : headers) if (toUpperAscii(h.first) == "CONTENT-TYPE") { hasContentType = true; break; }
    if (!hasContentType) headers.push_back({"Content-Type", "application/json"});
    return json::encode(v);
}

// رد HTTP حقيقي (rin_http.h) -> Value(map) يستهلكه برنامج Rin مباشرة:
// { ok, status, body (نص خام), json (مُحلَّل تلقائياً إن كان JSON صالحاً، وإلا نفس body)، error }
static Value httpResultToValue(const http::HttpResult& r) {
    auto m = std::make_shared<MapData>();
    m->push_back({Value::string("ok"), Value::boolean_(r.ok)});
    m->push_back({Value::string("status"), Value::num((double)r.status)});
    m->push_back({Value::string("body"), Value::string(r.body)});
    m->push_back({Value::string("json"), json::decodeOrRaw(r.body)});
    m->push_back({Value::string("error"), Value::string(r.error)});
    return Value::makeMap(m);
}

// يستدعي API حقيقياً مسجَّلاً بالاسم [endpointName] (عبر apiRegister/apiHeader): يبني الرابط
// الكامل (baseUrl + path)، يدمج ترويسات النقطة المسجَّلة (مفتاح API الخاص بالمبرمج ضمنها)، ويُجري
// طلب شبكة حقيقياً فعلياً. يرمي RinError واضحاً إن لم يُسجَّل API بهذا الاسم أصلاً.
Value Interpreter::performRealApiCall(const std::string& endpointName, const std::string& method,
                                       const std::string& path, const Value& bodyValue, int line) {
    auto it = apiEndpoints.find(endpointName);
    if (it == apiEndpoints.end()) {
        throw diagErr(diag::Code::E0037_NetworkError, line, "لا يوجد API حقيقي مسجَّل باسم '" + endpointName +
                        "' — سجِّله أولاً: apiRegister(\"" + endpointName + "\", \"https://...\");");
    }
    std::string url = it->second.baseUrl;
    if (!path.empty()) {
        bool baseEndsSlash = !url.empty() && url.back() == '/';
        bool pathStartsSlash = !path.empty() && path.front() == '/';
        if (baseEndsSlash && pathStartsSlash) url += path.substr(1);
        else if (!baseEndsSlash && !pathStartsSlash) url += "/" + path;
        else url += path;
    }
    http::HeaderList headers = it->second.headers; // نسخة: bodyToString قد تضيف Content-Type محلياً فقط لهذا الطلب
    std::string body = bodyToString(bodyValue, headers);
    auto result = http::performRequest(toUpperAscii(method), url, headers, body, defaultHttpTimeoutMs);
    return httpResultToValue(result);
}

// ============================================================================
// RinFlow — Execution Flow Engine (implementation)
// ============================================================================
// انظر التوثيق الكامل عند تعريف الأنواع في rin_interpreter.h (namespace flow) وعند نقطة الدخول
// evaluate()'s CallExpr branch أعلاه. كل شيء هنا إضافي بحت: run() العادي بلا أي `|>` أو بلا جلسة
// Flow نشِطة لا يمرّ إطلاقاً من هذا الكود.
namespace flow {

std::string nodeTypeName(NodeType t) {
    switch (t) {
        case NodeType::INPUT: return "INPUT";
        case NodeType::OUTPUT: return "OUTPUT";
        case NodeType::FILTER: return "FILTER";
        case NodeType::MAP: return "MAP";
        case NodeType::TRANSFORM: return "TRANSFORM";
        case NodeType::SORT: return "SORT";
        case NodeType::REDUCE: return "REDUCE";
        case NodeType::CONTAINER: return "CONTAINER";
        case NodeType::FILE: return "FILE";
        case NodeType::NETWORK: return "NETWORK";
        case NodeType::PIPELINE: return "PIPELINE";
        case NodeType::CUSTOM: return "CUSTOM";
    }
    return "CUSTOM";
}

std::string nodeStatusName(NodeStatus s) {
    switch (s) {
        case NodeStatus::QUEUED: return "QUEUED";
        case NodeStatus::RUNNING: return "RUNNING";
        case NodeStatus::SUCCESS: return "SUCCESS";
        case NodeStatus::ERROR: return "ERROR";
        case NodeStatus::SKIPPED: return "SKIPPED";
        case NodeStatus::CANCELLED: return "CANCELLED";
        case NodeStatus::TIMEOUT: return "TIMEOUT";
    }
    return "ERROR";
}

std::string eventTypeName(EventType t) {
    switch (t) {
        case EventType::FLOW_STARTED: return "FLOW_STARTED";
        case EventType::NODE_QUEUED: return "NODE_QUEUED";
        case EventType::NODE_STARTED: return "NODE_STARTED";
        case EventType::NODE_OUTPUT: return "NODE_OUTPUT";
        case EventType::NODE_FINISHED: return "NODE_FINISHED";
        case EventType::NODE_ERROR: return "NODE_ERROR";
        case EventType::FLOW_FINISHED: return "FLOW_FINISHED";
        case EventType::FLOW_CANCELLED: return "FLOW_CANCELLED";
        case EventType::FLOW_TIMEOUT: return "FLOW_TIMEOUT";
    }
    return "FLOW_FINISHED";
}

std::string sessionStatusName(SessionStatus s) {
    switch (s) {
        case SessionStatus::RUNNING: return "RUNNING";
        case SessionStatus::SUCCESS: return "SUCCESS";
        case SessionStatus::ERROR: return "ERROR";
        case SessionStatus::CANCELLED: return "CANCELLED";
        case SessionStatus::TIMEOUT: return "TIMEOUT";
    }
    return "ERROR";
}

DataPreview makeDataPreview(const Value& v, int maxPreviewChars, int maxRecordsPreview) {
    DataPreview p;
    p.available = true;
    if (v.type == Value::Type::ARRAY && v.array) {
        p.recordCount = static_cast<long long>(v.array->size());
        std::ostringstream os;
        os << "[";
        size_t shown = std::min(v.array->size(), static_cast<size_t>(std::max(0, maxRecordsPreview)));
        for (size_t i = 0; i < shown; ++i) {
            if (i > 0) os << ", ";
            os << (*v.array)[i].toDisplayString();
        }
        if (v.array->size() > shown) {
            os << ", ... (+" << (v.array->size() - shown) << " more)";
            p.truncated = true;
        }
        os << "]";
        p.preview = os.str();
    } else {
        p.preview = v.toDisplayString();
    }
    if (maxPreviewChars > 0 && static_cast<int>(p.preview.size()) > maxPreviewChars) {
        p.preview = p.preview.substr(0, static_cast<size_t>(maxPreviewChars)) + "...";
        p.truncated = true;
    }
    return p;
}

long long PipelineTracer::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void PipelineTracer::emit(EventType type, int nodeId, const std::string& message, int line,
                            long long durationMs, std::unordered_map<std::string, std::string> metadata) {
    FlowEvent ev;
    ev.sequence = seq_.fetch_add(1, std::memory_order_relaxed) + 1;
    ev.timestamp = nowMs();
    ev.flowId = flowId_;
    ev.nodeId = nodeId;
    ev.type = type;
    ev.message = message;
    ev.line = line;
    ev.column = 1; // انظر تعليق FlowEvent::column في rin_interpreter.h
    ev.durationMs = durationMs;
    ev.metadata = std::move(metadata);

    EventSink sinkCopy;
    {
        std::lock_guard<std::mutex> lk(mu_);
        events_.push_back(ev);
        if (events_.size() > kMaxEvents) events_.erase(events_.begin()); // قسم 25: لا تراكم بلا حدود
        sinkCopy = sink_;
    }
    if (sinkCopy) sinkCopy(ev); // خارج القفل: sink قد يستدعي كوتلن/JNI (نفس فلسفة StreamSink)
}

void PipelineTracer::updateMetricsLocked() {
    FlowMetrics m;
    m.totalNodes = static_cast<int>(graph_.nodes.size());
    for (auto& n : graph_.nodes) {
        switch (n.status) {
            case NodeStatus::SUCCESS: m.completedNodes++; break;
            case NodeStatus::ERROR: m.failedNodes++; break;
            case NodeStatus::SKIPPED: m.skippedNodes++; break;
            case NodeStatus::CANCELLED: m.cancelledNodes++; break;
            case NodeStatus::TIMEOUT: m.timeoutNodes++; break;
            default: break;
        }
        m.totalDurationMs += n.durationMs;
        if (n.input.available && n.input.recordCount >= 0) m.totalInputRecords += n.input.recordCount;
        if (n.output.available && n.output.recordCount >= 0) m.totalOutputRecords += n.output.recordCount;
    }
    metrics_ = m;
}

int PipelineTracer::queueNode(NodeType type, const std::string& name, int line) {
    int id;
    {
        std::lock_guard<std::mutex> lk(mu_);
        FlowNode node;
        node.id = static_cast<int>(graph_.nodes.size());
        node.type = type;
        node.name = name;
        node.status = NodeStatus::QUEUED;
        node.line = line;
        if (!graph_.nodes.empty()) graph_.edges.push_back({graph_.nodes.back().id, node.id});
        id = node.id;
        graph_.nodes.push_back(std::move(node));
        updateMetricsLocked();
    }
    emit(EventType::NODE_QUEUED, id, name, line, 0);
    return id;
}

void PipelineTracer::startNode(int nodeId) {
    int line = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        auto& n = graph_.nodes[nodeId];
        n.status = NodeStatus::RUNNING;
        n.startedAt = nowMs();
        line = n.line;
        updateMetricsLocked();
    }
    emit(EventType::NODE_STARTED, nodeId, "", line, 0);
}

void PipelineTracer::recordInput(int nodeId, const Value& v, int maxPreviewChars, int maxRecordsPreview) {
    std::lock_guard<std::mutex> lk(mu_);
    if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
    graph_.nodes[nodeId].input = makeDataPreview(v, maxPreviewChars, maxRecordsPreview);
    updateMetricsLocked();
}

void PipelineTracer::recordOutput(int nodeId, const Value& v, int maxPreviewChars, int maxRecordsPreview) {
    int line = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        graph_.nodes[nodeId].output = makeDataPreview(v, maxPreviewChars, maxRecordsPreview);
        line = graph_.nodes[nodeId].line;
        updateMetricsLocked();
    }
    emit(EventType::NODE_OUTPUT, nodeId, "", line, 0);
}

void PipelineTracer::finishNode(int nodeId) {
    long long dur = 0; int line = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        auto& n = graph_.nodes[nodeId];
        n.status = NodeStatus::SUCCESS;
        n.finishedAt = nowMs();
        n.durationMs = n.startedAt > 0 ? (n.finishedAt - n.startedAt) : 0;
        dur = n.durationMs; line = n.line;
        updateMetricsLocked();
    }
    emit(EventType::NODE_FINISHED, nodeId, "SUCCESS", line, dur);
}

void PipelineTracer::failNode(int nodeId, const NodeError& err) {
    long long dur = 0; int line = err.line;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        auto& n = graph_.nodes[nodeId];
        n.status = NodeStatus::ERROR;
        n.finishedAt = nowMs();
        n.durationMs = n.startedAt > 0 ? (n.finishedAt - n.startedAt) : 0;
        n.error = err;
        dur = n.durationMs;
        updateMetricsLocked();
    }
    std::unordered_map<std::string, std::string> meta = {{"code", err.code}};
    emit(EventType::NODE_ERROR, nodeId, err.message, line, dur, meta);
}

void PipelineTracer::skipNode(int nodeId) {
    int line = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        graph_.nodes[nodeId].status = NodeStatus::SKIPPED;
        line = graph_.nodes[nodeId].line;
        updateMetricsLocked();
    }
    emit(EventType::NODE_FINISHED, nodeId, "SKIPPED", line, 0);
}

void PipelineTracer::cancelNode(int nodeId) {
    int line = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        graph_.nodes[nodeId].status = NodeStatus::CANCELLED;
        graph_.nodes[nodeId].finishedAt = nowMs();
        line = graph_.nodes[nodeId].line;
        updateMetricsLocked();
    }
    emit(EventType::NODE_FINISHED, nodeId, "CANCELLED", line, 0);
}

void PipelineTracer::timeoutNode(int nodeId) {
    int line = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (nodeId < 0 || nodeId >= static_cast<int>(graph_.nodes.size())) return;
        graph_.nodes[nodeId].status = NodeStatus::TIMEOUT;
        graph_.nodes[nodeId].finishedAt = nowMs();
        line = graph_.nodes[nodeId].line;
        updateMetricsLocked();
    }
    emit(EventType::NODE_FINISHED, nodeId, "TIMEOUT", line, 0);
}

void PipelineTracer::emitFlowStarted() { emit(EventType::FLOW_STARTED, -1, flowId_, 0, 0); }

void PipelineTracer::emitFlowFinished(SessionStatus status) {
    long long total = 0;
    { std::lock_guard<std::mutex> lk(mu_); total = metrics_.totalDurationMs; }
    EventType t = EventType::FLOW_FINISHED;
    if (status == SessionStatus::CANCELLED) t = EventType::FLOW_CANCELLED;
    else if (status == SessionStatus::TIMEOUT) t = EventType::FLOW_TIMEOUT;
    emit(t, -1, sessionStatusName(status), 0, total);
}

std::shared_ptr<FlowSession> RinFlowEngine::createSession(const std::string& id) {
    auto session = std::make_shared<FlowSession>();
    session->id = id;
    session->tracer = std::make_shared<PipelineTracer>(id);
    std::lock_guard<std::mutex> lk(mu_);
    sessions_[id] = session;
    order_.push_back(id);
    while (order_.size() > kMaxSessions) {
        // لا تحذف جلسات ما تزال RUNNING (قسم 25: لا تفقد جلسة نشطة أثناء التقليم)
        auto it = sessions_.find(order_.front());
        if (it != sessions_.end() && it->second->status == SessionStatus::RUNNING) break;
        sessions_.erase(order_.front());
        order_.erase(order_.begin());
    }
    return session;
}

std::shared_ptr<FlowSession> RinFlowEngine::getSession(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second;
}

bool RinFlowEngine::requestCancel(const std::string& id) {
    std::shared_ptr<FlowSession> session;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return false;
        session = it->second;
    }
    if (session->status != SessionStatus::RUNNING) return false;
    session->cancelFlag->store(true, std::memory_order_relaxed);
    return true;
}

std::vector<std::string> RinFlowEngine::listSessionIds() const {
    std::lock_guard<std::mutex> lk(mu_);
    return order_;
}

} // namespace flow

flow::NodeType Interpreter::inferFlowNodeType(const std::string& fnName) const {
    auto it = flowNodeTypeOverrides_.find(fnName);
    if (it != flowNodeTypeOverrides_.end()) return it->second;
    // جدول تخمين افتراضي بأسماء شائعة لمراحل |> -- قابل للتوسع بالكامل عبر
    // Interpreter::registerFlowNodeType دون الحاجة لتعديل هذا الجدول نفسه (قسم 1).
    static const std::unordered_map<std::string, flow::NodeType> defaults = {
        {"filter", flow::NodeType::FILTER},
        {"map", flow::NodeType::MAP},
        {"transform", flow::NodeType::TRANSFORM},
        {"sort", flow::NodeType::SORT},
        {"sortBy", flow::NodeType::SORT},
        {"reduce", flow::NodeType::REDUCE},
        {"fold", flow::NodeType::REDUCE},
        {"sum", flow::NodeType::REDUCE},
        {"mean", flow::NodeType::REDUCE},
        {"output", flow::NodeType::OUTPUT},
        {"print", flow::NodeType::OUTPUT},
        {"save", flow::NodeType::CONTAINER},
        {"insertDoc", flow::NodeType::CONTAINER},
        {"updateDoc", flow::NodeType::CONTAINER},
        {"table", flow::NodeType::CONTAINER},
        {"file", flow::NodeType::FILE},
        {"readFile", flow::NodeType::FILE},
        {"writeFile", flow::NodeType::FILE},
        {"httpGet", flow::NodeType::NETWORK},
        {"httpPost", flow::NodeType::NETWORK},
        {"apiCall", flow::NodeType::NETWORK},
        {"apiGet", flow::NodeType::NETWORK},
        {"apiPost", flow::NodeType::NETWORK},
        {"pipe", flow::NodeType::PIPELINE},
    };
    auto d = defaults.find(fnName);
    return d == defaults.end() ? flow::NodeType::CUSTOM : d->second;
}

void Interpreter::flattenPipelineChain(const std::shared_ptr<CallExpr>& root,
                                        ExprPtr& outOriginalInput,
                                        std::vector<std::shared_ptr<CallExpr>>& outStages) const {
    // كل CallExpr ناتج عن |> يضع الإدخال القادم من يساره في args[0] (انظر Parser::pipeline في
    // rin_parser.cpp)؛ ننزل بهذا حتى نصل لتعبير ليس CallExpr::isPipelineNode -- وهذا هو الإدخال
    // الأصلي الحقيقي (قد يكون متغيراً، حرفياً، أو أي تعبير آخر).
    std::vector<std::shared_ptr<CallExpr>> reversed;
    std::shared_ptr<CallExpr> cur = root;
    ExprPtr originalInput;
    while (cur && cur->isPipelineNode) {
        reversed.push_back(cur);
        if (cur->args.empty()) { originalInput = nullptr; break; }
        auto prevCall = std::dynamic_pointer_cast<CallExpr>(cur->args[0]);
        if (prevCall && prevCall->isPipelineNode) {
            cur = prevCall;
        } else {
            originalInput = cur->args[0];
            cur = nullptr;
        }
    }
    outOriginalInput = originalInput;
    outStages.assign(reversed.rbegin(), reversed.rend());
}

Value Interpreter::evaluatePipelineFlow(const std::shared_ptr<CallExpr>& root, EnvPtr env) {
    auto& session = *activeFlowSession_;
    auto tracer = session.tracer;
    const auto& opts = activeFlowOptions_;

    // نتذكّر آخر سلسلة/بيئة نُفِّذت في هذه الجلسة (قسم 11: Replay) بلا أي تعديل على الجلسات الأخرى.
    session.lastRootExpr = root;
    session.lastEnv = env;

    ExprPtr originalInputExpr;
    std::vector<std::shared_ptr<CallExpr>> stages;
    flattenPipelineChain(root, originalInputExpr, stages);

    // إلغاء طُلِب فعلاً *قبل* بدء أي شيء من هذه السلسلة بالذات -> لا تُنشأ أي Node جديدة إطلاقاً
    // (قسم 9: "لا تستمر Nodes الجديدة بعد الإلغاء" -- يشمل هذا أي سلسلة |> لاحقة في نفس البرنامج).
    if (session.isCancelled()) return Value::nil();
    if (session.isTimedOut()) return Value::nil();

    // ---- Node الإدخال (INPUT) ----
    int inputLine = originalInputExpr ? originalInputExpr->line : root->line;
    int inputNodeId = tracer->queueNode(flow::NodeType::INPUT, "input", inputLine);
    tracer->startNode(inputNodeId);
    Value currentValue = originalInputExpr ? evaluate(originalInputExpr, env) : Value::nil();
    tracer->recordOutput(inputNodeId, currentValue, opts.maxPreviewChars, opts.maxRecordsPreview);
    tracer->finishNode(inputNodeId);

    // ---- مراحل |> بالترتيب الحقيقي ----
    for (size_t i = 0; i < stages.size(); ++i) {
        auto& stageCall = stages[i];
        flow::NodeType type = inferFlowNodeType(stageCall->callee);
        // آخر مرحلة تُسمَّى output صراحة إن لم يكن اسمها نفسه يوحي بنوع أدقّ (مثال: آخر مرحلة اسمها
        // "save" تبقى CONTAINER لأن هذا أدقّ من OUTPUT العام).
        if (i + 1 == stages.size() && type == flow::NodeType::CUSTOM) type = flow::NodeType::OUTPUT;

        if (session.isCancelled()) {
            int nid = tracer->queueNode(type, stageCall->callee, stageCall->line);
            tracer->cancelNode(nid);
            for (size_t j = i + 1; j < stages.size(); ++j) {
                int skipId = tracer->queueNode(inferFlowNodeType(stages[j]->callee), stages[j]->callee, stages[j]->line);
                tracer->skipNode(skipId);
            }
            return Value::nil();
        }
        if (session.isTimedOut()) {
            int nid = tracer->queueNode(type, stageCall->callee, stageCall->line);
            tracer->timeoutNode(nid);
            for (size_t j = i + 1; j < stages.size(); ++j) {
                int skipId = tracer->queueNode(inferFlowNodeType(stages[j]->callee), stages[j]->callee, stages[j]->line);
                tracer->skipNode(skipId);
            }
            return Value::nil();
        }

        int nodeId = tracer->queueNode(type, stageCall->callee, stageCall->line);
        tracer->startNode(nodeId);
        tracer->recordInput(nodeId, currentValue, opts.maxPreviewChars, opts.maxRecordsPreview);

        // args[0] هو تعبير المدخل القادم من السلسلة (لا يُعاد تقييمه -- currentValue هو ناتجه
        // الفعلي المُحسَب أعلاه بالفعل)؛ باقي args[1..] هي وسائط إضافية حقيقية مكتوبة صراحة في
        // الكود، كل واحد يُقيَّم في نفس env تماماً كأي CallExpr عادي.
        std::vector<Value> callArgs;
        callArgs.push_back(currentValue);
        for (size_t k = 1; k < stageCall->args.size(); ++k) {
            callArgs.push_back(evaluate(stageCall->args[k], env));
        }

        try {
            currentValue = invokeCallee(stageCall->callee, callArgs, stageCall->line, env);
        } catch (RinError& e) {
            flow::NodeError nerr;
            nerr.line = e.line;
            nerr.message = e.message;
            // كود RinFlow داخلي (RIN-Fxxxx) مستقل عن diag::Code اللغوي -- قسم 8: Flow Diagnostics.
            nerr.code = e.diagnostic ? ("RIN-F-" + diag::codeString(e.diagnostic->code)) : "RIN-F2000";
            tracer->failNode(nodeId, nerr);
            for (size_t j = i + 1; j < stages.size(); ++j) {
                int skipId = tracer->queueNode(inferFlowNodeType(stages[j]->callee), stages[j]->callee, stages[j]->line);
                tracer->skipNode(skipId);
            }
            throw; // نفس RinError يصعد كما كان دائماً؛ run() العادي يعالجه بنفس الطريقة تماماً
        }

        tracer->recordOutput(nodeId, currentValue, opts.maxPreviewChars, opts.maxRecordsPreview);
        tracer->finishNode(nodeId);
    }

    return currentValue;
}

Interpreter::FlowRunResult Interpreter::runProgramAsFlow(const std::vector<StmtPtr>& statements,
                                                           const flow::FlowRunOptions& opts,
                                                           flow::EventSink sink) {
    static std::atomic<long long> counter{0};
    std::string sessionId = "flow-" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(counter.fetch_add(1));
    auto session = flowEngine_.createSession(sessionId);
    if (sink) session->tracer->setSink(std::move(sink));
    if (opts.timeoutMs > 0) {
        session->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.timeoutMs);
    }

    activeFlowSession_ = session;
    activeFlowOptions_ = opts;
    session->tracer->emitFlowStarted();

    FlowRunResult result;
    result.sessionId = sessionId;
    result.output = run(statements); // نفس run() العادي بالضبط -- hoisting/output/lastDiagnostic كلها كما هي

    flow::SessionStatus finalStatus;
    if (session->isCancelled()) finalStatus = flow::SessionStatus::CANCELLED;
    else if (session->isTimedOut()) finalStatus = flow::SessionStatus::TIMEOUT;
    else if (hadError()) finalStatus = flow::SessionStatus::ERROR;
    else finalStatus = flow::SessionStatus::SUCCESS;
    session->status = finalStatus;
    session->tracer->emitFlowFinished(finalStatus);

    result.status = finalStatus;
    result.graph = session->tracer->snapshotGraph();
    result.metrics = session->tracer->snapshotMetrics();

    activeFlowSession_.reset();
    return result;
}

std::optional<Interpreter::FlowRunResult> Interpreter::replayFlow(const std::string& previousSessionId,
                                                                    const flow::FlowRunOptions& opts,
                                                                    flow::EventSink sink) {
    auto previous = flowEngine_.getSession(previousSessionId);
    if (!previous || !previous->lastRootExpr || !previous->lastEnv) return std::nullopt;

    static std::atomic<long long> counter{0};
    std::string sessionId = "flow-replay-" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(counter.fetch_add(1));
    auto session = flowEngine_.createSession(sessionId);
    if (sink) session->tracer->setSink(std::move(sink));
    if (opts.timeoutMs > 0) {
        session->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.timeoutMs);
    }

    activeFlowSession_ = session;
    activeFlowOptions_ = opts;
    session->tracer->emitFlowStarted();

    FlowRunResult result;
    result.sessionId = sessionId;
    flow::SessionStatus finalStatus = flow::SessionStatus::SUCCESS;
    try {
        evaluatePipelineFlow(previous->lastRootExpr, previous->lastEnv);
    } catch (RinError&) {
        finalStatus = flow::SessionStatus::ERROR;
    }
    if (session->isCancelled()) finalStatus = flow::SessionStatus::CANCELLED;
    else if (session->isTimedOut()) finalStatus = flow::SessionStatus::TIMEOUT;

    session->status = finalStatus;
    session->tracer->emitFlowFinished(finalStatus);
    result.status = finalStatus;
    result.graph = session->tracer->snapshotGraph();
    result.metrics = session->tracer->snapshotMetrics();

    activeFlowSession_.reset();
    return result;
}

} // namespace rin
