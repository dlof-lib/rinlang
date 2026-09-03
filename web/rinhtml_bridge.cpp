// web/rinhtml_bridge.cpp
// =============================================================================
// RinHTML — جسر WebAssembly *ذو جلسة* (session) بين HTML/JS ومحرك Rin الرسمي
// (rin_lexer.cpp + rin_parser.cpp + rin_interpreter.cpp — نفس الملفات الثلاثة
// المستخدمة في tools/rin_run.cpp والتطبيق، بلا أي تعديل عليها).
//
// الفرق عن web/rin_wasm_bridge.cpp (الجسر القديم، ما زال قائماً وصالحاً):
//   القديم: rin_run_source(source) ينفّذ البرنامج كاملاً مرة واحدة ويعيد نص
//           print() فقط — مناسب لصفحة تُبنى بـ writeFile() وتُقرأ من MEMFS.
//   هذا:    يبقي الجلسة (Interpreter + AST + قيم المتغيرات العامة) حيّة بين
//           الاستدعاءات، بحيث يمكن لزر HTML استدعاء دالة Rin باسمها فعلياً
//           (مثل increment() في main.rin) ثم قراءة الحالة الجديدة — بدون
//           إعادة تنفيذ البرنامج من الصفر في كل نقرة.
//
// هذا ممكن بفضل دالتين موجودتين أصلاً في rin_interpreter.h لغرض Loomtime:
//   - Interpreter::callTopLevelFunction(...)  — استدعاء دالة Rin علوية حقيقية
//     بكل دلالات اللغة (حلقات/تكرار/مكتبة قياسية)، مع تمرير/استرجاع قيم
//     المتغيرات العامة عبر خريطة globalsInOut.
//   - Interpreter::exportGlobals()            — قراءة كل المتغيرات العامة
//     المرئية بعد run() (نُستخدم لالتقاط القيم الابتدائية فقط).
//
// هذا الملف "غلاف" (glue) بحت: لا يعيد تنفيذ أي جزء من اللغة، ولا يضيف أي
// دلالة جديدة إلى Rin نفسها.
// =============================================================================
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "rin_common.h"
#include <emscripten.h>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cctype>

using namespace rin;

namespace {

// ---- جلسة واحدة = برنامج مُحلَّل (AST) + مفسِّر خاص به + آخر قيم عامة معروفة ----
struct RinHtmlSession {
    std::vector<StmtPtr> program;
    std::unique_ptr<Interpreter> interp;
    std::unordered_map<std::string, Value> globals; // "مصدر الحقيقة" الحالي لحالة الجلسة
    std::string bootOutput;                          // ناتج print() أثناء التنفيذ الأول فقط
};

std::unordered_map<int, std::unique_ptr<RinHtmlSession>> g_sessions;
int g_nextId = 1;
std::string g_lastError;
std::string g_scratch; // مُعاد استخدامه كمخزن للقيمة المُعادة إلى JS (نفس أسلوب rin_wasm_bridge.cpp)

// =====================  JSON صغير جداً (كتابة) — للقيم البسيطة فقط  =====================
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) { /* تجاهل أحرف تحكم أخرى */ }
                else out += c;
        }
    }
    return out;
}

std::string numToJson(double n) {
    // أعداد صحيحة تُطبع بلا ".0" (أنظف لمستهلك JS: count == 3 وليس 3.0)
    if (n == static_cast<long long>(n) &&
        n < 1e15 && n > -1e15) {
        return std::to_string(static_cast<long long>(n));
    }
    std::ostringstream o;
    o << n;
    return o.str();
}

// القيم من نوع ARRAY/MAP/FUNCTION ليست جزءاً من واجهة الربط الرسمية في هذا الإصدار
// (v1 من RinHTML): نُمرِّرها كنص عرض فقط (toDisplayString) حتى لا تُفشل rin-text
// عند عرضها، لكن state/rin-model الحقيقيان مخصَّصان للقيم البدائية.
std::string valueToJson(const Value& v) {
    switch (v.type) {
        case Value::Type::NUMBER: return numToJson(v.number);
        case Value::Type::STRING: return "\"" + jsonEscape(v.str) + "\"";
        case Value::Type::BOOL:   return v.boolean ? "true" : "false";
        case Value::Type::NIL:    return "null";
        default:                  return "\"" + jsonEscape(v.toDisplayString()) + "\"";
    }
}

std::string globalsToJson(const std::unordered_map<std::string, Value>& g) {
    std::ostringstream o;
    o << "{";
    bool first = true;
    for (auto& kv : g) {
        // الدوال (FUNCTION) لا تُصدَّر إلى JS كـ "حالة" — لا قيمة لعرضها، وليست بيانات.
        if (kv.second.type == Value::Type::FUNCTION) continue;
        if (!first) o << ",";
        first = false;
        o << "\"" << jsonEscape(kv.first) << "\":" << valueToJson(kv.second);
    }
    o << "}";
    return o.str();
}

// =====================  JSON صغير جداً (قراءة) — لوسائط استدعاء الدوال فقط  =====================
// يقرأ مصفوفة JSON مسطّحة من قيم بدائية فقط: أرقام / سلاسل نصية "..." / true / false / null.
// هذا كافٍ تماماً لوسائط استدعاء دالة Rin من onclick بسيط في HTML؛ ليس محلِّل JSON عاماً.
struct MiniJsonReader {
    const std::string& s;
    size_t i = 0;
    explicit MiniJsonReader(const std::string& src) : s(src) {}

    void skipWs() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; }

    Value parseValue() {
        skipWs();
        if (i >= s.size()) return Value::nil();
        char c = s[i];
        if (c == '"') return parseString();
        if (c == 't' && s.compare(i, 4, "true") == 0) { i += 4; return Value::boolean_(true); }
        if (c == 'f' && s.compare(i, 5, "false") == 0) { i += 5; return Value::boolean_(false); }
        if (c == 'n' && s.compare(i, 4, "null") == 0) { i += 4; return Value::nil(); }
        return parseNumber();
    }

    Value parseString() {
        std::string out;
        i++; // "
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    default: out += e;
                }
            } else out += c;
        }
        if (i < s.size()) i++; // "
        return Value::string(out);
    }

    Value parseNumber() {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                                 s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) i++;
        if (i == start) { i++; return Value::nil(); } // محرف غير متوقَّع: تجاوزه بلا انهيار
        try { return Value::num(std::stod(s.substr(start, i - start))); }
        catch (...) { return Value::nil(); }
    }

    std::vector<Value> parseArray() {
        std::vector<Value> out;
        skipWs();
        if (i >= s.size() || s[i] != '[') return out;
        i++; // [
        skipWs();
        if (i < s.size() && s[i] == ']') { i++; return out; }
        while (i < s.size()) {
            out.push_back(parseValue());
            skipWs();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; break; }
            break; // JSON مشوَّه: نتوقف بهدوء بدل التعليق
        }
        return out;
    }
};

std::vector<Value> parseArgsJson(const std::string& src) {
    MiniJsonReader r(src);
    return r.parseArray();
}

Value parseScalarJson(const std::string& src) {
    MiniJsonReader r(src);
    return r.parseValue();
}

} // namespace

extern "C" {

// ينشئ جلسة جديدة من نص .rin كامل: Lex -> Parse -> تشغيل أولي واحد (لتنفيذ let/تعريف fun/أي
// print() علوي)، ثم يلتقط قيم المتغيرات العامة الناتجة كحالة ابتدائية للجلسة.
// يُعيد معرِّف جلسة (>=1) عند النجاح، أو -1 عند الفشل (استخدم rinhtml_last_error لمعرفة السبب).
EMSCRIPTEN_KEEPALIVE
int rinhtml_create(const char* source) {
    auto sess = std::make_unique<RinHtmlSession>();
    try {
        Lexer lexer(std::string(source ? source : ""));
        auto tokens = lexer.scanTokens();
        Parser parser(tokens);
        sess->program = parser.parse();

        sess->interp = std::make_unique<Interpreter>();
        sess->bootOutput = sess->interp->run(sess->program);

        if (sess->interp->hadError()) {
            g_lastError = sess->interp->lastErrorMessage().value_or("خطأ غير معروف أثناء التشغيل الأولي");
            return -1;
        }
        sess->globals = sess->interp->exportGlobals();
    } catch (RinError& e) {
        g_lastError = std::to_string(e.line) + ": " + e.message;
        return -1;
    } catch (std::exception& e) {
        g_lastError = e.what();
        return -1;
    } catch (...) {
        g_lastError = "خطأ غير معروف أثناء إنشاء جلسة RinHTML";
        return -1;
    }

    int id = g_nextId++;
    g_sessions[id] = std::move(sess);
    return id;
}

// آخر رسالة خطأ سُجِّلت من rinhtml_create/rinhtml_call الأخيرة التي فشلت في هذه الجلسة العامة
// (سلسلة واحدة عامة كافية: JS يقرأها فوراً بعد أي استدعاء يعيد ok:false أو -1).
EMSCRIPTEN_KEEPALIVE
const char* rinhtml_last_error() {
    g_scratch = g_lastError;
    return g_scratch.c_str();
}

// كل ما طُبع عبر print()/show() أثناء التشغيل الأولي فقط (وليس أثناء استدعاءات rinhtml_call
// اللاحقة، والتي لا تُعيد نص print الخاص بها حالياً في v1 حتى تبقى الواجهة بسيطة).
EMSCRIPTEN_KEEPALIVE
const char* rinhtml_boot_output(int session) {
    auto it = g_sessions.find(session);
    g_scratch = (it == g_sessions.end()) ? "" : it->second->bootOutput;
    return g_scratch.c_str();
}

// كل المتغيرات العامة الحالية للجلسة كـ JSON مسطّح {name: value, ...} (أرقام/سلاسل/منطقية/null
// فقط — الدوال تُستثنى). هذا ما تستخدمه rin-text/rin-model لعرض الحالة الابتدائية عند mount().
EMSCRIPTEN_KEEPALIVE
const char* rinhtml_get_globals(int session) {
    auto it = g_sessions.find(session);
    if (it == g_sessions.end()) { g_scratch = "{}"; return g_scratch.c_str(); }
    g_scratch = globalsToJson(it->second->globals);
    return g_scratch.c_str();
}

// يستدعي دالة Rin علوية باسمها الحرفي (fnName) بوسائط جاهزة (argsJson: مصفوفة JSON من قيم
// بدائية) داخل الجلسة المُعطاة، باستخدام نفس المفسِّر (تكرار/حلقات/مكتبة قياسية كاملة، وليس
// تقييماً سطحياً). يحدِّث حالة الجلسة في مكانها إن نجح الاستدعاء، ويُعيد JSON:
//   {"ok":true, "globals":{...}}                          عند النجاح
//   {"ok":false, "error":"...", "globals":{...}}           عند الفشل (globals كما كانت قبل المحاولة)
EMSCRIPTEN_KEEPALIVE
const char* rinhtml_call(int session, const char* fnName, const char* argsJson) {
    auto it = g_sessions.find(session);
    if (it == g_sessions.end()) {
        g_scratch = "{\"ok\":false,\"error\":\"invalid session\",\"globals\":{}}";
        return g_scratch.c_str();
    }
    auto& sess = *it->second;
    std::vector<Value> args = parseArgsJson(argsJson ? argsJson : "[]");
    std::vector<std::string> aliases(args.size(), std::string());

    // callTopLevelFunction يعدِّل globalsInOut في مكانها فقط عند النجاح؛ نمرر نسخة حتى لا نفقد
    // آخر حالة معروفة إن فشل الاستدعاء (arity خاطئة، RinError من داخل الدالة، ...).
    std::unordered_map<std::string, Value> attempt = sess.globals;
    std::string err;
    bool ok = false;
    try {
        ok = sess.interp->callTopLevelFunction(sess.program, fnName ? fnName : "", args, aliases, attempt, err);
    } catch (RinError& e) {
        ok = false;
        err = std::to_string(e.line) + ": " + e.message;
    } catch (std::exception& e) {
        ok = false;
        err = e.what();
    }

    if (ok) sess.globals = attempt;

    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false");
    if (!ok) o << ",\"error\":\"" << jsonEscape(err.empty() ? ("لا توجد دالة باسم '" + std::string(fnName ? fnName : "") + "'") : err) << "\"";
    o << ",\"globals\":" << globalsToJson(sess.globals);
    o << "}";
    g_scratch = o.str();
    return g_scratch.c_str();
}

// يضبط متغيراً عاماً واحداً محلياً في الجلسة (side الـ JS فقط — لا يُشغِّل أي كود Rin). يُستخدَم
// من rin-model (ربط ثنائي الاتجاه من <input>) قبل استدعاء أي دالة تعتمد عليه، ولعرض تعديلات
// الواجهة فوراً بلا استدعاء دالة Rin في كل ضغطة مفتاح.
EMSCRIPTEN_KEEPALIVE
void rinhtml_set_global(int session, const char* name, const char* valueJson) {
    auto it = g_sessions.find(session);
    if (it == g_sessions.end() || !name) return;
    it->second->globals[name] = parseScalarJson(valueJson ? valueJson : "null");
}

// يحرِّر جلسة (تُستدعى من beforeunload أو rinhtml.unmount()).
EMSCRIPTEN_KEEPALIVE
void rinhtml_free_session(int session) {
    g_sessions.erase(session);
}

} // extern "C"
