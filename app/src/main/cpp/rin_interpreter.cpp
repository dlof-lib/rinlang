#include "rin_interpreter.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_stdlib_libs.h"
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

namespace rin {

// تمثيل "متداخل" لقيمة (يُستخدم داخل عناصر المصفوفات/القواميس عند الطباعة): النصوص توضع بين علامتي تنصيص.
static std::string reprValue(const Value& v) {
    if (v.type == Value::Type::STRING) return "\"" + v.str + "\"";
    return v.toDisplayString();
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

Interpreter::Interpreter() {
    globals = std::make_shared<Environment>();
    globals->define("PI", Value::num(3.14159265358979323846));
    globals->define("E", Value::num(2.71828182845904523536));
    registerNatives();
}

static double asNumber(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::NUMBER) {
        throw RinError("'" + fn + "' expects a number but got " + v.typeName(), line);
    }
    return v.number;
}

static std::string asString(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::STRING) {
        throw RinError("'" + fn + "' expects a string but got " + v.typeName(), line);
    }
    return v.str;
}

static void expectArgs(const std::string& fn, std::vector<Value>& args, size_t count, int line) {
    if (args.size() != count) {
        throw RinError("'" + fn + "' expects " + std::to_string(count) +
                        " argument(s) but got " + std::to_string(args.size()), line);
    }
}

// يتحقّق أن طرفَي عملية حسابية/مقارنة (غير + التي تدعم النصوص أيضاً) هما رقمان فعلاً، بدلاً من الاعتماد
// الصامت على Value::number الذي يساوي 0.0 افتراضياً لأي نوع آخر (نص/مصفوفة/قاموس/nil) — وهو ما كان
// يجعل عبارة مثل "abc" - 5 تُحسَب بصمت كـ 0 - 5 بدل رمي خطأ واضح.
static void requireNumbers(const Value& left, const Value& right, const std::string& op, int line) {
    if (left.type != Value::Type::NUMBER || right.type != Value::Type::NUMBER) {
        const Value& bad = (left.type != Value::Type::NUMBER) ? left : right;
        throw RinError("العملية '" + op + "' تتطلّب رقمين، لكن وُجد نوع " + bad.typeName(), line);
    }
}

// يحوّل قيمة من نوع array إلى مصفوفة أرقام C++ لاستخدامها في الدوال الإحصائية.
static std::vector<double> asNumberArray(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::ARRAY) {
        throw RinError("'" + fn + "' expects an array of numbers but got " + v.typeName(), line);
    }
    std::vector<double> out;
    out.reserve(v.array->size());
    for (auto& item : *v.array) {
        if (item.type != Value::Type::NUMBER) {
            throw RinError("'" + fn + "' expects an array of numbers, found a " + item.typeName() + " element", line);
        }
        out.push_back(item.number);
    }
    if (out.empty()) {
        throw RinError("'" + fn + "' لا يقبل مصفوفة فارغة (empty array)", line);
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
        if (n < 0) throw RinError("'sqrt' لا يقبل عدداً سالباً", line);
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
        throw RinError("'len' expects a string, array or map but got " + a[0].typeName(), line);
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
            throw RinError("'substr' expects 2 or 3 argument(s) but got " + std::to_string(a.size()), line);
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
            throw RinError("'join' expects an array as the first argument", line);
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
        throw RinError("'contains' expects a string or array as the first argument", line);
    };
    natives["charAt"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("charAt", a, 2, line);
        std::string s = asString(a[0], "charAt", line);
        long i = static_cast<long>(asNumber(a[1], "charAt", line));
        if (i < 0 || static_cast<size_t>(i) >= s.size()) {
            throw RinError("'charAt': index out of range", line);
        }
        return Value::string(std::string(1, s[static_cast<size_t>(i)]));
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
            throw RinError("'toNumber': \"" + s + "\" ليست رقماً صالحاً", line);
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
                throw RinError("'toBool': \"" + v.str + "\" ليست true/false صالحة", line);
            }
            default:
                throw RinError("'toBool' لا يدعم تحويل نوع " + v.typeName() + " إلى Boolean", line);
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
            throw RinError("'insertDoc' يتوقّع كائناً/قاموساً (map) كوسيط ثالث لحقول المستند", line);
        }
        if (!containerKinds.count(container) || containerKinds[container] != ContainerKind::DOC) {
            throw RinError("'insertDoc': '" + container + "' ليست مجموعة مستندات NoSQL (container.doc / doc)", line);
        }
        auto& docs = docStore[container];
        for (auto& entry : docs) {
            if (entry.first == id) { entry.second = a[2]; return Value::boolean_(false); }
        }
        docs.push_back({id, a[2]});
        return Value::boolean_(true);
    };

    // updateDoc(collection, id, partialFields) -> true إن وُجد ودُمجت الحقول الجديدة (تحديث جزئي/patch)، false إن لم يوجد
    natives["updateDoc"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("updateDoc", a, 3, line);
        std::string container = asString(a[0], "updateDoc", line);
        std::string id = asString(a[1], "updateDoc", line);
        if (a[2].type != Value::Type::MAP) {
            throw RinError("'updateDoc' يتوقّع كائناً/قاموساً (map) كوسيط ثالث للحقول الجديدة", line);
        }
        auto it = docStore.find(container);
        if (it == docStore.end()) return Value::boolean_(false);
        for (auto& entry : it->second) {
            if (entry.first != id) continue;
            if (entry.second.type != Value::Type::MAP || !entry.second.map) {
                entry.second = Value::makeMap(std::make_shared<MapData>());
            }
            for (auto& kv : *a[2].map) {
                bool found = false;
                for (auto& existing : *entry.second.map) {
                    if (valuesEqual(existing.first, kv.first)) { existing.second = kv.second; found = true; break; }
                }
                if (!found) entry.second.map->push_back(kv);
            }
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
                if (vec[i].first == id) { vec.erase(vec.begin() + i); return Value::boolean_(true); }
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

    // ---- container.api: استدعاء نقاط API الوهمية المسجَّلة عبر route (حقيقي بالكامل، بلا شبكة) ----
    // call(method, path) -> يبحث داخل container.api الحالي (الذي نُنفَّذ بداخله الآن)
    natives["call"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("call", a, 2, line);
        std::string method = asString(a[0], "call", line);
        std::string path = asString(a[1], "call", line);
        std::string key = containerStack.empty() ? "" : containerStack.back();
        return performApiCall(key, method, path, line);
    };
    // callApi(apiContainerName, method, path) -> يستدعي أي container.api باسمه من أي مكان في البرنامج
    natives["callApi"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("callApi", a, 3, line);
        std::string key = asString(a[0], "callApi", line);
        std::string method = asString(a[1], "callApi", line);
        std::string path = asString(a[2], "callApi", line);
        return performApiCall(key, method, path, line);
    };

    // ---- مصفوفات وقواميس (arrays & maps) ----
    natives["push"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("push", a, 2, line);
        if (a[0].type != Value::Type::ARRAY) throw RinError("'push' expects an array", line);
        a[0].array->push_back(a[1]);
        return Value::num(static_cast<double>(a[0].array->size()));
    };
    natives["pop"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("pop", a, 1, line);
        if (a[0].type != Value::Type::ARRAY) throw RinError("'pop' expects an array", line);
        if (a[0].array->empty()) throw RinError("'pop': المصفوفة فارغة", line);
        Value v = a[0].array->back();
        a[0].array->pop_back();
        return v;
    };
    natives["sort"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("sort", a, 1, line);
        if (a[0].type != Value::Type::ARRAY) throw RinError("'sort' expects an array", line);
        std::sort(a[0].array->begin(), a[0].array->end(), [line](const Value& x, const Value& y) {
            if (x.type == Value::Type::NUMBER && y.type == Value::Type::NUMBER) return x.number < y.number;
            if (x.type == Value::Type::STRING && y.type == Value::Type::STRING) return x.str < y.str;
            throw RinError("'sort' يتطلب أن تكون كل العناصر أرقاماً أو نصوصاً فقط", line);
        });
        return a[0];
    };
    natives["keys"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("keys", a, 1, line);
        if (a[0].type != Value::Type::MAP) throw RinError("'keys' expects a map", line);
        auto result = std::make_shared<ArrayData>();
        for (auto& kv : *a[0].map) result->push_back(kv.first);
        return Value::makeArray(result);
    };
    natives["values"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("values", a, 1, line);
        if (a[0].type != Value::Type::MAP) throw RinError("'values' expects a map", line);
        auto result = std::make_shared<ArrayData>();
        for (auto& kv : *a[0].map) result->push_back(kv.second);
        return Value::makeArray(result);
    };
    natives["has"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("has", a, 2, line);
        if (a[0].type != Value::Type::MAP) throw RinError("'has' expects a map", line);
        for (auto& kv : *a[0].map) if (valuesEqual(kv.first, a[1])) return Value::boolean_(true);
        return Value::boolean_(false);
    };
    natives["remove"] = [](std::vector<Value>& a, int line) -> Value {
        expectArgs("remove", a, 2, line);
        if (a[0].type != Value::Type::MAP) throw RinError("'remove' expects a map", line);
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
        if (!out) throw RinError("appendFile: تعذّر فتح الملف '" + path + "' للإضافة إليه", line);
        out << content;
        return Value::boolean_(true);
    };
    natives["readFile"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("readFile", a, 1, line);
        std::string path = asString(a[0], "readFile", line);
        std::ifstream in(resolvePath(path, line), std::ios::binary);
        if (!in) throw RinError("readFile: تعذّر فتح الملف '" + path + "' للقراءة (غير موجود؟)", line);
        std::ostringstream buf;
        buf << in.rdbuf();
        return Value::string(buf.str());
    };
    natives["fileExists"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileExists", a, 1, line);
        std::string path = asString(a[0], "fileExists", line);
        struct stat st{};
        return Value::boolean_(::stat(resolvePath(path, line).c_str(), &st) == 0);
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
            throw RinError("loadInstalled('" + name + "'): خطأ داخل الملف المحمّل فعلياً من القرص (سطر " +
                            std::to_string(e.line) + "): " + e.message, line);
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
                throw RinError("مسار غير مسموح به (يحاول الخروج خارج مجلد المشروع المعزول): '" + rawPath + "'", line);
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
            if (::mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) {
                // تُترك بصمت: محاولة الكتابة اللاحقة (ofstream) سترمي خطأ واضحاً إن كان هذا هو السبب الفعلي للفشل.
            }
            partial += "/";
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
}

// ================= PNG خفيف الوزن (بلا اعتماديات خارجية) — لأجل table.save/png =================
// لا يوجد داخل هذا المفسّر محرّك خطوط، فلا يمكن رسم نص حقيقي داخل الصورة. لذا فإن table.save/png
// يرسم "خريطة فسيفسائية" (mosaic) حقيقية وصالحة تماماً كملف PNG: كل خلية من الجدول تُلوَّن بلون
// مُشتق ثابت من قيمتها (نفس القيمة => نفس اللون دائماً)، مفصولة بخطوط شبكة تتبع الثيم المختار
// عبر 'style' (مثال: "style://dark").
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

// لون ثابت مُشتق من نص الخلية (تجزئة FNV-1a بسيطة): نفس القيمة تُعطي دائماً نفس اللون.
static void cellColor(const std::string& text, unsigned char& r, unsigned char& g, unsigned char& b) {
    uint32_t h = 2166136261u;
    for (unsigned char c : text) { h ^= c; h *= 16777619u; }
    r = static_cast<unsigned char>(120 + (h & 0x7Fu));
    g = static_cast<unsigned char>(120 + ((h >> 8) & 0x7Fu));
    b = static_cast<unsigned char>(120 + ((h >> 16) & 0x7Fu));
}

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

// يرسم الجدول (صفوفه المسجَّلة عبر row + نمطه المسجَّل عبر style إن وُجد) كصورة PNG حقيقية.
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
    unsigned char gridShade = dark ? 235 : 45;

    const int cellW = 80, cellH = 32, gridPx = 2;
    int width = static_cast<int>(colCount) * cellW + gridPx;
    int height = static_cast<int>(rowCount) * cellH + gridPx;

    unsigned char bg = dark ? 25 : 250;
    std::vector<unsigned char> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, bg);

    auto setPixel = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        size_t idx = (static_cast<size_t>(y) * width + x) * 3;
        rgb[idx] = r; rgb[idx + 1] = g; rgb[idx + 2] = b;
    };

    for (size_t ry = 0; ry < rowCount; ry++) {
        const Value* rowVal = (rows && ry < rows->size()) ? &(*rows)[ry] : nullptr;
        for (size_t cx = 0; cx < colCount; cx++) {
            std::string text;
            if (rowVal && rowVal->type == Value::Type::ARRAY && rowVal->array && cx < rowVal->array->size()) {
                text = (*rowVal->array)[cx].toDisplayString();
            }
            unsigned char r, g, b;
            cellColor(text.empty() ? ("#" + std::to_string(ry) + "," + std::to_string(cx)) : text, r, g, b);
            int x0 = static_cast<int>(cx) * cellW + gridPx;
            int y0 = static_cast<int>(ry) * cellH + gridPx;
            for (int yy = y0; yy < y0 + cellH - gridPx; yy++)
                for (int xx = x0; xx < x0 + cellW - gridPx; xx++)
                    setPixel(xx, yy, r, g, b);
        }
    }
    for (size_t cx = 0; cx <= colCount; cx++) {
        int x0 = static_cast<int>(cx) * cellW;
        for (int yy = 0; yy < height; yy++)
            for (int t = 0; t < gridPx; t++) setPixel(x0 + t, yy, gridShade, gridShade, gridShade);
    }
    for (size_t ry = 0; ry <= rowCount; ry++) {
        int y0 = static_cast<int>(ry) * cellH;
        for (int xx = 0; xx < width; xx++)
            for (int t = 0; t < gridPx; t++) setPixel(xx, y0 + t, gridShade, gridShade, gridShade);
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
        throw RinError(who + ": تعذّر فتح/إنشاء الملف '" + relPath + "' للكتابة الفعلية على القرص", line);
    }
    out << content;
    if (!out.good()) {
        throw RinError(who + ": حدث خطأ أثناء الكتابة الفعلية إلى '" + relPath + "'", line);
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

std::string Interpreter::run(const std::vector<StmtPtr>& statements) {
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
            execute(s, globals);
        }
    } catch (RinError& e) {
        output << "\n[Error line " << e.line << "]: " << e.message << "\n";
    } catch (ReturnSignal&) {
        output << "\n[Error]: 'return' used outside of a function\n";
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
        std::string sep = " ";
        if (s->sep) {
            Value sepVal = evaluate(s->sep, env);
            if (sepVal.type != Value::Type::STRING) {
                throw RinError("'print': 'sep' يجب أن يكون نصاً (string)، لكن وُجد نوع " + sepVal.typeName(), stmt->line);
            }
            sep = sepVal.str;
        }
        std::string end = "\n";
        if (s->end) {
            Value endVal = evaluate(s->end, env);
            if (endVal.type != Value::Type::STRING) {
                throw RinError("'print': 'end' يجب أن يكون نصاً (string)، لكن وُجد نوع " + endVal.typeName(), stmt->line);
            }
            end = endVal.str;
        }
        for (size_t i = 0; i < s->exprs.size(); i++) {
            if (i > 0) output << sep;
            output << evaluate(s->exprs[i], env).toDisplayString();
        }
        output << end;
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
            execute(s->body, env);
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

    // ---- لغة الحاويات/البيانات ----

    if (auto s = std::dynamic_pointer_cast<TextStmt>(stmt)) {
        Value v = Value::nil();
        if (s->initializer) v = evaluate(s->initializer, env);
        if (v.type != Value::Type::STRING) {
            throw RinError("'" + s->name + "' من نوع text ويجب أن تكون قيمته نصاً (string)", s->line);
        }
        env->define(s->name, v);
        return;
    }

    if (auto s = std::dynamic_pointer_cast<ObjectStyleFieldStmt>(stmt)) {
        // txt/img/object.file/Fonts/background/css3: حقول ستايل حرة، مسموحة حصراً داخل
        // @container.object/@Object (بخلاف 'style value=...' المتاحة في object/portal/block/table).
        if (containerStack.empty() || containerKinds[containerStack.back()] != ContainerKind::OBJECT) {
            throw RinError("حقول الستايل (txt/img/object.file/Fonts/background/css3) مسموحة حصراً داخل "
                            "@container.object/@Object", s->line);
        }
        Value v = Value::nil();
        if (s->initializer) v = evaluate(s->initializer, env);
        static const std::unordered_map<ObjectStyleFieldKind, std::string> kindNames = {
            {ObjectStyleFieldKind::TXT, "txt"}, {ObjectStyleFieldKind::IMG, "img"},
            {ObjectStyleFieldKind::OBJECT_FILE, "object.file"}, {ObjectStyleFieldKind::FONTS, "Fonts"},
            {ObjectStyleFieldKind::BACKGROUND, "background"}, {ObjectStyleFieldKind::CSS3, "css3"}
        };
        static const std::unordered_map<ObjectStyleFieldKind, std::string> kindIcons = {
            {ObjectStyleFieldKind::TXT, "📝"}, {ObjectStyleFieldKind::IMG, "🖼️"},
            {ObjectStyleFieldKind::OBJECT_FILE, "📄"}, {ObjectStyleFieldKind::FONTS, "🔤"},
            {ObjectStyleFieldKind::BACKGROUND, "🎨"}, {ObjectStyleFieldKind::CSS3, "🧾"}
        };
        const std::string& kindName = kindNames.at(s->kind);
        if (v.type != Value::Type::STRING) {
            throw RinError("'" + s->name + "' من نوع " + kindName + " ويجب أن تكون قيمته نصاً (string)", s->line);
        }
        env->define(s->name, v);
        output << kindIcons.at(s->kind) << " " << kindName << " " << s->name << " -> " << v.str << "\n";
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
                throw RinError("container.import يتطلب 'file path=\"...\";' بداخله لتحديد الملف المطلوب استيراده", s->line);
            }
            std::ifstream in(resolvePath(currentFilePath), std::ios::binary);
            if (!in) {
                throw RinError("container.import: تعذّر فتح الملف '" + currentFilePath + "' للاستيراد", s->line);
            }
            std::ostringstream buf;
            buf << in.rdbuf();
            std::string importedSource = buf.str();
            try {
                Lexer importedLexer(importedSource);
                auto importedTokens = importedLexer.scanTokens();
                Parser importedParser(importedTokens);
                auto importedStatements = importedParser.parse();
                // تُنفَّذ عبارات الملف المستورد داخل بيئة حاوية الاستيراد نفسها؛ أي @container بداخله
                // يُسجَّل عالمياً (متاح لاحقاً عبر link/tying/merge)، وأي let/text أعلى المستوى فيه
                // يصبح متغيراً داخل حاوية container.import هذه.
                executeBlock(importedStatements, containerEnv);
            } catch (RinError& e) {
                throw RinError("container.import: خطأ داخل الملف المستورد \"" + currentFilePath +
                                "\" (سطر " + std::to_string(e.line) + "): " + e.message, s->line);
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
            throw RinError("@import يتطلب مساراً نصياً (string) لاسم/مسار المكتبة المطلوب استيرادها", s->line);
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
                throw RinError("@import: تعذّر إيجاد المكتبة \"" + rawPath + "\" (بُحث عنها باسم \"" +
                                libPath + "\" ضمن المكتبات المدمجة وضمن مجلد lib/ الخاص بالمشروع ولم "
                                "تُوجد. ارفعها أو أنشئها أولاً من قسم \"المكتبات\" في المحرر)", s->line);
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
            Lexer importedLexer(source);
            auto importedTokens = importedLexer.scanTokens();
            Parser importedParser(importedTokens);
            importedStatements = importedParser.parse();
        } catch (RinError& e) {
            throw RinError("@import: خطأ في تحليل المكتبة \"" + libPath + "\" (سطر " +
                            std::to_string(e.line) + "): " + e.message, s->line);
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
            throw RinError("@import: خطأ داخل المكتبة \"" + libPath + "\" (سطر " +
                            std::to_string(e.line) + "): " + e.message, s->line);
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

    if (auto s = std::dynamic_pointer_cast<LinkStmt>(stmt)) {
        bool isContainer = containers.count(s->target) > 0;
        bool isGroup = groupMembers.count(s->target) > 0;
        if (!isContainer && !isGroup) {
            throw RinError("لا يمكن تنفيذ link: '" + s->target + "' غير معرَّف كحاوية أو كمجموعة (Containers.Group)", s->line);
        }
        output << "🔗 link -> " << s->target << (isGroup ? " (Containers.Group)" : "") << "\n";
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
            throw RinError("'save' يجب أن تُستخدم داخل حاوية (container) حالية لحفظ متغيراتها فعلياً", s->line);
        }
        std::string key = containerStack.back();
        ContainerKind kind = containerKinds.count(key) ? containerKinds[key] : ContainerKind::PLAIN;

        // table.save/png : تصدير حقيقي لصورة PNG تمثّل الجدول، متاح فقط لحاويات الجدول
        // (@container.table أو @table)، ويعمل بغضّ النظر عن الشكل الذي فُتحت به.
        if (s->format == "png") {
            if (kind != ContainerKind::TABLE) {
                throw RinError("save format=png متاحة فقط لحاوية جدول (@container.table أو @table)، وليس لـ '" +
                                containerTagName(kind) + "'", s->line);
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
            throw RinError("عبارة 'route' يجب أن تُستخدم داخل @container.api", s->line);
        }
        Value methodVal = evaluate(s->method, env);
        if (methodVal.type != Value::Type::STRING) {
            throw RinError("route: قيمة 'method' يجب أن تكون نصاً (مثال: \"GET\")", s->line);
        }
        Value pathVal = evaluate(s->path, env);
        if (pathVal.type != Value::Type::STRING) {
            throw RinError("route: قيمة 'path' يجب أن تكون نصاً (مثال: \"/users/1\")", s->line);
        }
        Value statusVal = evaluate(s->status, env);
        if (statusVal.type != Value::Type::NUMBER) {
            throw RinError("route: قيمة 'status' يجب أن تكون رقماً (مثال: 200)", s->line);
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
            throw RinError("عبارة 'row' يجب أن تُستخدم داخل @container.table أو @table", s->line);
        }
        Value cells = evaluate(s->cells, env);
        if (cells.type != Value::Type::ARRAY) {
            throw RinError("row: قيمة 'cells' يجب أن تكون مصفوفة (مثال: row cells=[1, 2, 3];)", s->line);
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
                        currentKind == ContainerKind::PORTAL || currentKind == ContainerKind::BLOCK;
        if (containerStack.empty() || !allowed) {
            throw RinError("عبارة 'style' يجب أن تُستخدم داخل @container.table/@table أو "
                            "@container.object/@Object أو @container.portal/@portal أو @container.block/@block", s->line);
        }
        Value v = evaluate(s->value, env);
        if (v.type != Value::Type::STRING) {
            throw RinError("style: قيمة 'value' يجب أن تكون نصاً (مثال: style value=\"style://dark\";)", s->line);
        }
        containerStyles[containerStack.back()] = v.str;
        output << "🎨 style -> " << v.str << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<DocumentStmt>(stmt)) {
        if (containerStack.empty() || containerKinds[containerStack.back()] != ContainerKind::DOC) {
            throw RinError("عبارة 'document' يجب أن تُستخدم داخل @container.doc أو @doc", s->line);
        }
        Value idVal = evaluate(s->id, env);
        if (idVal.type != Value::Type::STRING) {
            throw RinError("document: قيمة 'id' يجب أن تكون نصاً (مثال: document id=\"u1\" fields={...};)", s->line);
        }
        Value fieldsVal = evaluate(s->fields, env);
        if (fieldsVal.type != Value::Type::MAP) {
            throw RinError("document: قيمة 'fields' يجب أن تكون كائناً/قاموساً (مثال: fields={ name: \"Ali\" };)", s->line);
        }
        auto& docs = docStore[containerStack.back()];
        bool updated = false;
        for (auto& entry : docs) {
            if (entry.first == idVal.str) { entry.second = fieldsVal; updated = true; break; }
        }
        if (!updated) docs.push_back({idVal.str, fieldsVal});
        output << (updated ? "🔄 document (تحديث) -> " : "🧾 document (إدراج) -> ")
               << idVal.str << " = " << fieldsVal.toDisplayString() << "\n";
        return;
    }
}

bool Interpreter::copyTargetIntoCurrentContainer(const std::string& target, int line) {
    bool isContainer = containers.count(target) > 0;
    bool isGroup = groupMembers.count(target) > 0;
    if (!isContainer && !isGroup) {
        throw RinError("لا يمكن تنفيذ tying/merge: '" + target + "' غير معرَّف كحاوية أو كمجموعة (Containers.Group)", line);
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

Value Interpreter::callFunction(const std::shared_ptr<Callable>& fn, std::vector<Value>& args, int line) {
    if (args.size() != fn->declaration->params.size()) {
        throw RinError("Expected " + std::to_string(fn->declaration->params.size()) +
                        " argument(s) but got " + std::to_string(args.size()), line);
    }
    // حارس عمق الاستدعاء: بلا هذا الفحص، دالة تتكرّر ذاتياً بلا حالة توقّف (نسيان شرط الإنهاء —
    // خطأ برمجي شائع جداً، وليس فقط سيناريو هجوم) تُسبِّب Stack Overflow حقيقياً في مكدّس C++ الأصلي
    // (segmentation fault لا يمكن لأي try/catch اعتراضه)، فيُسقِط التطبيق كاملاً. الآن يُحوَّل هذا إلى
    // RinError عادي وقابل للعرض في الكونسول، تماماً كأي خطأ Rin آخر.
    if (callDepth >= kMaxCallDepth) {
        throw RinError("تجاوز الحد الأقصى لعمق استدعاء الدوال (" + std::to_string(kMaxCallDepth) +
                        ") — على الأغلب تكرار ذاتي بلا حالة توقّف (missing base case)", line);
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
            throw RinError("Undefined variable '" + e->name + "'", e->line);
        }
        return v;
    }
    if (auto e = std::dynamic_pointer_cast<AssignExpr>(expr)) {
        Value v = evaluate(e->value, env);
        if (!env->assign(e->name, v)) {
            throw RinError("Undefined variable '" + e->name + "'", e->line);
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
                throw RinError("Operand must be a number", e->line);
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
                throw RinError("Operands must be numbers or strings", e->line);
            case TokenType::MINUS:
                requireNumbers(left, right, "-", e->line);
                return Value::num(left.number - right.number);
            case TokenType::STAR:
                requireNumbers(left, right, "*", e->line);
                return Value::num(left.number * right.number);
            case TokenType::SLASH:
                requireNumbers(left, right, "/", e->line);
                if (right.number == 0) throw RinError("Division by zero", e->line);
                return Value::num(left.number / right.number);
            case TokenType::PERCENT:
                requireNumbers(left, right, "%", e->line);
                if (right.number == 0) throw RinError("Division by zero", e->line);
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
        // دوال العمليات المدمجة بأسمائها الصريحة: Addition/Subtraction/Multiplication/Equal
        static const std::unordered_set<std::string> builtinOps = {
            "Addition", "Subtraction", "Multiplication", "Equal"
        };
        if (builtinOps.count(e->callee)) {
            if (e->args.size() != 2) {
                throw RinError("'" + e->callee + "' تحتاج إلى قيمتين (arguments) بالضبط", e->line);
            }
            Value a = evaluate(e->args[0], env);
            Value b = evaluate(e->args[1], env);
            if (e->callee == "Equal") {
                return Value::boolean_(valuesEqual(a, b));
            }
            if (e->callee == "Addition" && (a.type == Value::Type::STRING || b.type == Value::Type::STRING)) {
                return Value::string(a.toDisplayString() + b.toDisplayString());
            }
            if (a.type != Value::Type::NUMBER || b.type != Value::Type::NUMBER) {
                throw RinError("'" + e->callee + "' تحتاج إلى قيمتين رقميتين (numbers)", e->line);
            }
            if (e->callee == "Addition") return Value::num(a.number + b.number);
            if (e->callee == "Subtraction") return Value::num(a.number - b.number);
            if (e->callee == "Multiplication") return Value::num(a.number * b.number);
        }

        auto nativeIt = natives.find(e->callee);
        if (nativeIt != natives.end()) {
            std::vector<Value> args;
            for (auto& a : e->args) args.push_back(evaluate(a, env));
            return nativeIt->second(args, e->line);
        }

        Value calleeVal;
        if (!env->get(e->callee, calleeVal) || calleeVal.type != Value::Type::FUNCTION) {
            throw RinError("'" + e->callee + "' is not a function", e->line);
        }
        std::vector<Value> args;
        for (auto& a : e->args) args.push_back(evaluate(a, env));
        return callFunction(calleeVal.function, args, e->line);
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
                throw RinError("فهرس المصفوفة يجب أن يكون رقماً", e->line);
            }
            long i = static_cast<long>(idx.number);
            if (i < 0 || static_cast<size_t>(i) >= obj.array->size()) {
                throw RinError("فهرس خارج الحدود (index out of range): " + std::to_string(i), e->line);
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
                throw RinError("فهرس النص يجب أن يكون رقماً", e->line);
            }
            long i = static_cast<long>(idx.number);
            if (i < 0 || static_cast<size_t>(i) >= obj.str.size()) {
                throw RinError("فهرس خارج الحدود (index out of range): " + std::to_string(i), e->line);
            }
            return Value::string(std::string(1, obj.str[static_cast<size_t>(i)]));
        }
        throw RinError("لا يمكن فهرسة قيمة من نوع " + obj.typeName(), e->line);
    }
    if (auto e = std::dynamic_pointer_cast<IndexSetExpr>(expr)) {
        Value obj = evaluate(e->object, env);
        Value idx = evaluate(e->index, env);
        Value val = evaluate(e->value, env);
        if (obj.type == Value::Type::ARRAY) {
            if (idx.type != Value::Type::NUMBER) {
                throw RinError("فهرس المصفوفة يجب أن يكون رقماً", e->line);
            }
            long i = static_cast<long>(idx.number);
            if (i == static_cast<long>(obj.array->size())) {
                obj.array->push_back(val); // السماح بالتوسّع عبر arr[len(arr)] = value
            } else if (i < 0 || static_cast<size_t>(i) >= obj.array->size()) {
                throw RinError("فهرس خارج الحدود (index out of range): " + std::to_string(i), e->line);
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
        throw RinError("لا يمكن التعديل على قيمة من نوع " + obj.typeName() + " عبر []", e->line);
    }
    return Value::nil();
}

// يبحث عن أول route مطابق (method + path) مسجَّل داخل container.api صاحب المفتاح المُعطى، ويُعيد
// قيمة map حقيقية {status, ok, body}. إن لم يوجد تطابق، يُعيد {status: 404, ok: false, error: ...}
// دون رمي استثناء — تماماً كما يتصرف عميل HTTP حقيقي أمام رد 404.
Value Interpreter::performApiCall(const std::string& containerKey, const std::string& method,
                                   const std::string& path, int line) {
    (void)line;
    std::string wantMethod = toUpperAscii(method);
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

} // namespace rin
