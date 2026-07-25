#include "rin_interpreter.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include <cmath>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cstdlib>

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
        default: return "container";
    }
}

static std::string containerIcon(ContainerKind k) {
    switch (k) {
        case ContainerKind::PIPE: return "🧵";
        case ContainerKind::DATA: return "🗂️";
        case ContainerKind::API: return "🌐";
        case ContainerKind::IMPORT: return "📦⬅️";
        default: return "📦";
    }
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
}

std::string Interpreter::run(const std::vector<StmtPtr>& statements) {
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
        Value v = evaluate(s->expr, env);
        output << v.toDisplayString() << "\n";
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

    if (auto s = std::dynamic_pointer_cast<ContainerStmt>(stmt)) {
        auto containerEnv = std::make_shared<Environment>(env);
        std::string tag = containerTagName(s->kind);
        std::string icon = containerIcon(s->kind);
        output << icon << " " << tag << (s->name.empty() ? "" : (" = " + s->name)) << "\n";
        std::string containerKey = s->name.empty() ? ("#" + std::to_string(containers.size())) : s->name;
        containers[containerKey] = containerEnv;
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
            std::ifstream in(currentFilePath, std::ios::binary);
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
        output << "⚙️ installation" << (s->simplified ? " (simplified)" : "") << ": " << s->target << "\n";
        return;
    }

    if (auto s = std::dynamic_pointer_cast<SaveStmt>(stmt)) {
        std::string p;
        if (s->path) p = evaluate(s->path, env).toDisplayString();
        output << "💾 save" << (s->simplified ? " (simplified)" : "");
        if (!p.empty()) output << " -> " << p;
        else if (!currentFilePath.empty()) output << " -> " << currentFilePath;
        output << "\n";
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
                return Value::num(left.number - right.number);
            case TokenType::STAR:
                return Value::num(left.number * right.number);
            case TokenType::SLASH:
                if (right.number == 0) throw RinError("Division by zero", e->line);
                return Value::num(left.number / right.number);
            case TokenType::PERCENT:
                if (right.number == 0) throw RinError("Division by zero", e->line);
                return Value::num(std::fmod(left.number, right.number));
            case TokenType::GREATER: return Value::boolean_(left.number > right.number);
            case TokenType::GREATER_EQUAL: return Value::boolean_(left.number >= right.number);
            case TokenType::LESS: return Value::boolean_(left.number < right.number);
            case TokenType::LESS_EQUAL: return Value::boolean_(left.number <= right.number);
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
