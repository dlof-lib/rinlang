// ============================================================================
//  rin_extra_natives4.cpp — Rin 1.0: أدوات بناء المكتبات والحزم + JSON كامل + جسر C++
// ----------------------------------------------------------------------------
//  دفعة رابعة فوق rin_extra_natives.cpp / 2 / 3 دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra4() وتُضمَّن (#include) في نهاية rin_interpreter.cpp،
//  فتدخل كل أهداف البناء (APK / CLI / WASM / CI) تلقائياً بلا أي تعديل في ملفات البناء.
//
//  المجموعات:
//    1) json.*    — parse/tryParse/valid/stringify/pretty/minify/canonical/get/query/set/has/
//                   remove/merge/flatten/unflatten/equals/diff/patch/validate(schema)/type/
//                   readFile/writeFile
//    2) semver.*  — parse/valid/compare/gt/lt/eq/satisfies/maxSatisfying/sort/bump/diff
//    3) pkg.*     — parseManifest/manifest/validateManifest/depOrder/api/apiFile/checksum/
//                   scaffold  (كل ما تحتاجه لإنشاء حزمة/مكتبة Rin كاملة)
//    4) cpp.*     — جسر C++ مساعد: info/header/compile/run/eval/lib/load/call/callNum/unload/exec
//       * تنفيذ كود أصلي مقفل افتراضياً: لا يعمل إلا مع RIN_ALLOW_NATIVE=1 أو `rin --allow-native`
//         (قرار يتخذه من يشغّل البرنامج، لا يستطيع كود .rin مستورَد تفعيله بنفسه).
//       * على أندرويد/WASM: لا مترجم C++ → cpp.compile/run/eval تُرجع {ok:false, error:...}.
// ============================================================================
#include "rin_interpreter.h"
#include "rin_json.h"
#include "clc/sha256.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
  #define RIN_NATIVE_TOOLCHAIN 1
  #include <dlfcn.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace rin {
namespace extra4 {

using Args = std::vector<Value>;

static Value S4(const std::string& s) { return Value::string(s); }
static Value N4(double d) { return Value::num(d); }
static Value B4(bool b) { return Value::boolean_(b); }
static Value M4(std::vector<std::pair<std::string, Value>> kv) {
    MapData out;
    out.reserve(kv.size());
    for (auto& p : kv) out.push_back({Value::string(p.first), std::move(p.second)});
    return newMap(std::move(out));
}
static Value A4(std::vector<Value> v) { return newArray(std::move(v)); }
static const Value* o4get(const Value& opts, const std::string& key) {
    if (opts.type != Value::Type::MAP || !opts.map) return nullptr;
    for (const auto& kv : *opts.map)
        if (kv.first.type == Value::Type::STRING && kv.first.str == key) return &kv.second;
    return nullptr;
}
static std::string o4str(const Value& opts, const std::string& key, const std::string& dflt) {
    const Value* v = o4get(opts, key);
    return (v && v->type == Value::Type::STRING) ? v->str : dflt;
}
static double o4num(const Value& opts, const std::string& key, double dflt) {
    const Value* v = o4get(opts, key);
    return (v && v->type == Value::Type::NUMBER) ? v->number : dflt;
}
static bool o4bool(const Value& opts, const std::string& key, bool dflt) {
    const Value* v = o4get(opts, key);
    return v ? v->isTruthy() : dflt;
}
static std::vector<std::string> o4strs(const Value& opts, const std::string& key) {
    std::vector<std::string> out;
    const Value* v = o4get(opts, key);
    if (v && v->type == Value::Type::ARRAY && v->array)
        for (auto& x : *v->array) out.push_back(x.toDisplayString());
    return out;
}

// ============================================================================
//  JSON
// ============================================================================
static std::string numToJson(double d) {
    if (!std::isfinite(d)) return "null";
    char buf[40];
    if (d == std::floor(d) && std::fabs(d) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", d);
    else {
        std::snprintf(buf, sizeof buf, "%.15g", d);
        if (std::strtod(buf, nullptr) != d) std::snprintf(buf, sizeof buf, "%.17g", d);
    }
    return buf;
}

static MapData instanceAsMap(const Value& v) {
    MapData out;
    if (v.instance)
        for (auto& name : v.instance->fieldOrder) {
            auto it = v.instance->fields.find(name);
            if (it != v.instance->fields.end()) out.push_back({Value::string(name), it->second});
        }
    return out;
}

static void emitJson(const Value& v, std::string& o, int indent, int depth, bool sortKeys) {
    auto nl = [&](int d) {
        if (indent <= 0) return;
        o += '\n';
        o.append(static_cast<size_t>(d * indent), ' ');
    };
    if (depth > 400) { o += "null"; return; }
    switch (v.type) {
        case Value::Type::NIL: case Value::Type::FUNCTION: o += "null"; break;
        case Value::Type::BOOL: o += v.boolean ? "true" : "false"; break;
        case Value::Type::NUMBER: o += numToJson(v.number); break;
        case Value::Type::STRING: { std::ostringstream os; json::encodeEscaped(v.str, os); o += os.str(); break; }
        case Value::Type::ARRAY: {
            if (!v.array || v.array->empty()) { o += "[]"; break; }
            o += '[';
            for (size_t i = 0; i < v.array->size(); ++i) {
                if (i) o += ',';
                nl(depth + 1);
                emitJson((*v.array)[i], o, indent, depth + 1, sortKeys);
            }
            nl(depth);
            o += ']';
            break;
        }
        case Value::Type::MAP: case Value::Type::INSTANCE: {
            MapData tmp;
            const MapData* md = nullptr;
            if (v.type == Value::Type::INSTANCE) { tmp = instanceAsMap(v); md = &tmp; }
            else if (v.map) md = v.map.get();
            if (!md || md->empty()) { o += "{}"; break; }
            std::vector<size_t> order(md->size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            if (sortKeys)
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    return (*md)[a].first.toDisplayString() < (*md)[b].first.toDisplayString();
                });
            o += '{';
            bool first = true;
            for (size_t i : order) {
                if (!first) o += ',';
                first = false;
                nl(depth + 1);
                std::ostringstream os;
                json::encodeEscaped((*md)[i].first.toDisplayString(), os);
                o += os.str();
                o += indent > 0 ? ": " : ":";
                emitJson((*md)[i].second, o, indent, depth + 1, sortKeys);
            }
            nl(depth);
            o += '}';
            break;
        }
    }
}
static std::string stringifyJson(const Value& v, int indent, bool sortKeys) {
    std::string o;
    emitJson(v, o, indent, 0, sortKeys);
    return o;
}

static bool decodeStrict(const std::string& text, Value& out, std::string& err) {
    json::Decoder d(text);
    return d.parse(out, err);
}

static std::string jsonTypeName(const Value& v) {
    switch (v.type) {
        case Value::Type::NIL: case Value::Type::FUNCTION: return "null";
        case Value::Type::BOOL: return "boolean";
        case Value::Type::NUMBER: return "number";
        case Value::Type::STRING: return "string";
        case Value::Type::ARRAY: return "array";
        default: return "object";
    }
}

// ---- مسارات: a.b[0].c / items[*].name / ["a.b"] / a.0 ----
struct PTok { std::string key; bool numeric = false; long long idx = 0; bool wild = false; };

static bool allDigits(const std::string& s, bool allowNeg = false) {
    if (s.empty()) return false;
    size_t i = (allowNeg && s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}
static PTok mkTok(const std::string& seg) {
    PTok t; t.key = seg;
    if (seg == "*") t.wild = true;
    else if (allDigits(seg, true)) { t.numeric = true; t.idx = std::atoll(seg.c_str()); }
    return t;
}
static std::vector<PTok> parseJPath(const Value& p, const std::string& fn, int line) {
    std::vector<PTok> out;
    if (p.type == Value::Type::ARRAY && p.array) {
        for (auto& x : *p.array) {
            if (x.type == Value::Type::NUMBER) { PTok t; t.numeric = true; t.idx = static_cast<long long>(x.number); t.key = std::to_string(t.idx); out.push_back(t); }
            else out.push_back(mkTok(x.toDisplayString()));
        }
        return out;
    }
    if (p.type == Value::Type::NUMBER) { PTok t; t.numeric = true; t.idx = static_cast<long long>(p.number); t.key = std::to_string(t.idx); out.push_back(t); return out; }
    if (p.type != Value::Type::STRING)
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a path string or array but got " + p.typeName());
    const std::string& s = p.str;
    std::string cur;
    auto flush = [&]() { if (!cur.empty()) { out.push_back(mkTok(cur)); cur.clear(); } };
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '.') flush();
        else if (c == '[') {
            flush();
            size_t j = i + 1;
            if (j < s.size() && (s[j] == '"' || s[j] == '\'')) {
                char q = s[j++]; std::string k;
                while (j < s.size() && s[j] != q) k += s[j++];
                PTok t; t.key = k; out.push_back(t);
                while (j < s.size() && s[j] != ']') ++j;
            } else {
                std::string k;
                while (j < s.size() && s[j] != ']') k += s[j++];
                out.push_back(mkTok(k));
            }
            i = j;
        } else cur += c;
    }
    flush();
    return out;
}

static const Value* childOf(const Value& cur, const PTok& t) {
    if (cur.type == Value::Type::ARRAY && cur.array) {
        if (!t.numeric) return nullptr;
        long long i = t.idx, n = static_cast<long long>(cur.array->size());
        if (i < 0) i += n;
        return (i >= 0 && i < n) ? &(*cur.array)[static_cast<size_t>(i)] : nullptr;
    }
    if (cur.type == Value::Type::MAP && cur.map) {
        for (auto& kv : *cur.map) if (kv.first.toDisplayString() == t.key) return &kv.second;
    }
    if (cur.type == Value::Type::INSTANCE && cur.instance) {
        auto it = cur.instance->fields.find(t.key);
        if (it != cur.instance->fields.end()) return &it->second;
    }
    return nullptr;
}
static void queryIn(const Value& cur, const std::vector<PTok>& p, size_t i, std::vector<Value>& out, bool& multi) {
    if (i == p.size()) { out.push_back(cur); return; }
    if (p[i].wild) {
        multi = true;
        if (cur.type == Value::Type::ARRAY && cur.array) for (auto& x : *cur.array) queryIn(x, p, i + 1, out, multi);
        else if (cur.type == Value::Type::MAP && cur.map) for (auto& kv : *cur.map) queryIn(kv.second, p, i + 1, out, multi);
        return;
    }
    const Value* c = childOf(cur, p[i]);
    if (c) queryIn(*c, p, i + 1, out, multi);
}

static Value setIn(const Value& cur, const std::vector<PTok>& p, size_t i, const Value& v, int line) {
    if (i == p.size()) return deepCopyValue(v);
    const PTok& t = p[i];
    if (t.wild) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.set: '*' is not allowed in a set path");
    if (cur.type == Value::Type::ARRAY && cur.array && t.numeric) {
        auto out = std::make_shared<ArrayData>(*cur.array);
        long long idx = t.idx, n = static_cast<long long>(out->size());
        if (idx < 0) idx += n;
        if (idx < 0 || idx > 1000000) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.set: array index out of range");
        if (idx >= n) out->resize(static_cast<size_t>(idx) + 1);
        (*out)[static_cast<size_t>(idx)] = setIn((*out)[static_cast<size_t>(idx)], p, i + 1, v, line);
        return Value::makeArray(out);
    }
    if (cur.type == Value::Type::MAP && cur.map) {
        auto out = std::make_shared<MapData>(*cur.map);
        for (auto& kv : *out)
            if (kv.first.toDisplayString() == t.key) { kv.second = setIn(kv.second, p, i + 1, v, line); return Value::makeMap(out); }
        out->push_back({Value::string(t.key), setIn(Value::nil(), p, i + 1, v, line)});
        return Value::makeMap(out);
    }
    // لا حاوية هنا: أنشئ واحدة (مصفوفة إن كان المفتاح رقماً، وإلا قاموساً)
    if (t.numeric && t.idx >= 0) {
        auto out = std::make_shared<ArrayData>();
        if (t.idx > 1000000) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.set: array index out of range");
        out->resize(static_cast<size_t>(t.idx) + 1);
        (*out)[static_cast<size_t>(t.idx)] = setIn(Value::nil(), p, i + 1, v, line);
        return Value::makeArray(out);
    }
    auto out = std::make_shared<MapData>();
    out->push_back({Value::string(t.key), setIn(Value::nil(), p, i + 1, v, line)});
    return Value::makeMap(out);
}
static Value removeIn(const Value& cur, const std::vector<PTok>& p, size_t i) {
    if (i >= p.size()) return cur;
    const PTok& t = p[i];
    bool last = (i + 1 == p.size());
    if (cur.type == Value::Type::ARRAY && cur.array && t.numeric) {
        long long idx = t.idx, n = static_cast<long long>(cur.array->size());
        if (idx < 0) idx += n;
        if (idx < 0 || idx >= n) return cur;
        auto out = std::make_shared<ArrayData>(*cur.array);
        if (last) out->erase(out->begin() + idx);
        else (*out)[static_cast<size_t>(idx)] = removeIn((*out)[static_cast<size_t>(idx)], p, i + 1);
        return Value::makeArray(out);
    }
    if (cur.type == Value::Type::MAP && cur.map) {
        auto out = std::make_shared<MapData>(*cur.map);
        for (size_t k = 0; k < out->size(); ++k)
            if ((*out)[k].first.toDisplayString() == t.key) {
                if (last) out->erase(out->begin() + static_cast<long>(k));
                else (*out)[k].second = removeIn((*out)[k].second, p, i + 1);
                return Value::makeMap(out);
            }
    }
    return cur;
}

static void flattenInto(const Value& v, const std::string& prefix, const std::string& sep, MapData& out, int depth) {
    bool isArr = v.type == Value::Type::ARRAY && v.array && !v.array->empty();
    bool isMap = (v.type == Value::Type::MAP && v.map && !v.map->empty());
    if (depth > 100 || (!isArr && !isMap)) { out.push_back({Value::string(prefix), v}); return; }
    if (isArr) {
        for (size_t i = 0; i < v.array->size(); ++i)
            flattenInto((*v.array)[i], prefix.empty() ? std::to_string(i) : prefix + sep + std::to_string(i), sep, out, depth + 1);
    } else {
        for (auto& kv : *v.map) {
            std::string k = kv.first.toDisplayString();
            flattenInto(kv.second, prefix.empty() ? k : prefix + sep + k, sep, out, depth + 1);
        }
    }
}

// ---- JSON Pointer (RFC 6901) لأجل diff/patch ----
static std::string ptrEsc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '~') o += "~0"; else if (c == '/') o += "~1"; else o += c; }
    return o;
}
static std::string ptrUnesc(std::string s) {
    size_t p;
    while ((p = s.find("~1")) != std::string::npos) s.replace(p, 2, "/");
    while ((p = s.find("~0")) != std::string::npos) s.replace(p, 2, "~");
    return s;
}
static std::vector<std::string> ptrSplit(const std::string& ptr) {
    std::vector<std::string> out;
    if (ptr.empty()) return out;
    size_t i = (ptr[0] == '/') ? 1 : 0;
    std::string cur;
    for (; i <= ptr.size(); ++i) {
        if (i == ptr.size() || ptr[i] == '/') { out.push_back(ptrUnesc(cur)); cur.clear(); }
        else cur += ptr[i];
    }
    return out;
}
static void diffRec(const Value& a, const Value& b, const std::string& path, ArrayData& out) {
    auto op = [&](const char* name, const std::string& p, const Value* from, const Value* val) {
        std::vector<std::pair<std::string, Value>> kv{{"op", S4(name)}, {"path", S4(p)}};
        if (from) kv.push_back({"from", *from});
        if (val) kv.push_back({"value", *val});
        out.push_back(M4(std::move(kv)));
    };
    if (a.type == Value::Type::MAP && b.type == Value::Type::MAP && a.map && b.map) {
        for (auto& kv : *a.map) {
            std::string k = kv.first.toDisplayString();
            const Value* other = nullptr;
            for (auto& kv2 : *b.map) if (kv2.first.toDisplayString() == k) { other = &kv2.second; break; }
            if (!other) op("remove", path + "/" + ptrEsc(k), &kv.second, nullptr);
            else diffRec(kv.second, *other, path + "/" + ptrEsc(k), out);
        }
        for (auto& kv : *b.map) {
            std::string k = kv.first.toDisplayString();
            bool found = false;
            for (auto& kv2 : *a.map) if (kv2.first.toDisplayString() == k) { found = true; break; }
            if (!found) op("add", path + "/" + ptrEsc(k), nullptr, &kv.second);
        }
        return;
    }
    if (a.type == Value::Type::ARRAY && b.type == Value::Type::ARRAY && a.array && b.array) {
        size_t common = std::min(a.array->size(), b.array->size());
        for (size_t i = 0; i < common; ++i) diffRec((*a.array)[i], (*b.array)[i], path + "/" + std::to_string(i), out);
        for (size_t i = common; i < b.array->size(); ++i) op("add", path + "/" + std::to_string(i), nullptr, &(*b.array)[i]);
        for (size_t i = a.array->size(); i > common; --i) op("remove", path + "/" + std::to_string(i - 1), &(*a.array)[i - 1], nullptr);
        return;
    }
    if (!valuesEqual(a, b)) op("replace", path, &a, &b);
}
static Value patchOne(const Value& cur, const std::vector<std::string>& toks, size_t i, const std::string& op,
                      const Value& val, int line) {
    if (i == toks.size()) {   // مسار فارغ = الجذر نفسه
        if (op == "remove") return Value::nil();
        return deepCopyValue(val);
    }
    bool last = (i + 1 == toks.size());
    const std::string& k = toks[i];
    if (cur.type == Value::Type::ARRAY && cur.array) {
        auto out = std::make_shared<ArrayData>(*cur.array);
        long long n = static_cast<long long>(out->size()), idx;
        if (k == "-") idx = n;
        else if (allDigits(k)) idx = std::atoll(k.c_str());
        else throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: bad array index '" + k + "'");
        if (last) {
            if (op == "add") { if (idx > n) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: index out of range"); out->insert(out->begin() + idx, deepCopyValue(val)); }
            else if (op == "remove") { if (idx >= n) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: index out of range"); out->erase(out->begin() + idx); }
            else { if (idx >= n) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: index out of range"); (*out)[static_cast<size_t>(idx)] = deepCopyValue(val); }
        } else {
            if (idx >= n) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: path not found");
            (*out)[static_cast<size_t>(idx)] = patchOne((*out)[static_cast<size_t>(idx)], toks, i + 1, op, val, line);
        }
        return Value::makeArray(out);
    }
    if (cur.type == Value::Type::MAP && cur.map) {
        auto out = std::make_shared<MapData>(*cur.map);
        size_t pos = out->size();
        for (size_t x = 0; x < out->size(); ++x) if ((*out)[x].first.toDisplayString() == k) { pos = x; break; }
        if (last) {
            if (op == "remove") { if (pos == out->size()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: path not found: " + k); out->erase(out->begin() + static_cast<long>(pos)); }
            else if (pos == out->size()) { if (op == "replace") throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: path not found: " + k); out->push_back({Value::string(k), deepCopyValue(val)}); }
            else (*out)[pos].second = deepCopyValue(val);
        } else {
            if (pos == out->size()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: path not found: " + k);
            (*out)[pos].second = patchOne((*out)[pos].second, toks, i + 1, op, val, line);
        }
        return Value::makeMap(out);
    }
    throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: cannot descend into " + cur.typeName() + " at '" + k + "'");
}

// ---- مخطط JSON Schema مبسّط ----
static void schemaErr(ArrayData& errs, const std::string& path, const std::string& msg) {
    errs.push_back(M4({{"path", S4(path.empty() ? "$" : path)}, {"message", S4(msg)}}));
}
static bool typeMatches(const Value& v, const std::string& t) {
    if (t == "any") return true;
    if (t == "integer") return v.type == Value::Type::NUMBER && v.number == std::floor(v.number);
    return jsonTypeName(v) == t;
}
static void validateRec(const Value& v, const Value& schema, const std::string& path, ArrayData& errs, int depth) {
    if (schema.type != Value::Type::MAP || !schema.map || depth > 100) return;
    if (const Value* t = o4get(schema, "type")) {
        std::vector<std::string> types;
        if (t->type == Value::Type::STRING) types.push_back(t->str);
        else if (t->type == Value::Type::ARRAY && t->array) for (auto& x : *t->array) types.push_back(x.toDisplayString());
        bool ok = types.empty();
        for (auto& ty : types) if (typeMatches(v, ty)) { ok = true; break; }
        if (!ok) {
            std::string want;
            for (size_t i = 0; i < types.size(); ++i) want += (i ? "|" : "") + types[i];
            schemaErr(errs, path, "expected " + want + " but got " + jsonTypeName(v));
            return;
        }
    }
    if (const Value* c = o4get(schema, "const")) if (!valuesEqual(v, *c)) schemaErr(errs, path, "must equal " + stringifyJson(*c, 0, false));
    if (const Value* e = o4get(schema, "enum")) {
        if (e->type == Value::Type::ARRAY && e->array) {
            bool found = false;
            for (auto& x : *e->array) if (valuesEqual(x, v)) { found = true; break; }
            if (!found) schemaErr(errs, path, "must be one of " + stringifyJson(*e, 0, false));
        }
    }
    if (v.type == Value::Type::NUMBER) {
        if (const Value* m = o4get(schema, "minimum")) if (m->type == Value::Type::NUMBER && v.number < m->number) schemaErr(errs, path, "must be >= " + numToJson(m->number));
        if (const Value* m = o4get(schema, "maximum")) if (m->type == Value::Type::NUMBER && v.number > m->number) schemaErr(errs, path, "must be <= " + numToJson(m->number));
        if (const Value* m = o4get(schema, "exclusiveMinimum")) if (m->type == Value::Type::NUMBER && v.number <= m->number) schemaErr(errs, path, "must be > " + numToJson(m->number));
        if (const Value* m = o4get(schema, "exclusiveMaximum")) if (m->type == Value::Type::NUMBER && v.number >= m->number) schemaErr(errs, path, "must be < " + numToJson(m->number));
        if (const Value* m = o4get(schema, "multipleOf")) if (m->type == Value::Type::NUMBER && m->number != 0 && std::fmod(v.number, m->number) != 0) schemaErr(errs, path, "must be a multiple of " + numToJson(m->number));
    }
    if (v.type == Value::Type::STRING) {
        size_t cps = extra::codePoints(v.str).size();
        if (const Value* m = o4get(schema, "minLength")) if (m->type == Value::Type::NUMBER && cps < m->number) schemaErr(errs, path, "length must be >= " + numToJson(m->number));
        if (const Value* m = o4get(schema, "maxLength")) if (m->type == Value::Type::NUMBER && cps > m->number) schemaErr(errs, path, "length must be <= " + numToJson(m->number));
        if (const Value* m = o4get(schema, "pattern")) if (m->type == Value::Type::STRING) {
            try { if (!std::regex_search(v.str, std::regex(m->str))) schemaErr(errs, path, "must match pattern " + m->str); }
            catch (...) { schemaErr(errs, path, "invalid pattern in schema: " + m->str); }
        }
    }
    if (v.type == Value::Type::ARRAY && v.array) {
        if (const Value* m = o4get(schema, "minItems")) if (m->type == Value::Type::NUMBER && v.array->size() < m->number) schemaErr(errs, path, "must have >= " + numToJson(m->number) + " items");
        if (const Value* m = o4get(schema, "maxItems")) if (m->type == Value::Type::NUMBER && v.array->size() > m->number) schemaErr(errs, path, "must have <= " + numToJson(m->number) + " items");
        if (o4bool(schema, "uniqueItems", false))
            for (size_t i = 0; i < v.array->size(); ++i)
                for (size_t j = i + 1; j < v.array->size(); ++j)
                    if (valuesEqual((*v.array)[i], (*v.array)[j])) schemaErr(errs, path + "[" + std::to_string(j) + "]", "duplicate item");
        if (const Value* it = o4get(schema, "items"))
            for (size_t i = 0; i < v.array->size(); ++i) validateRec((*v.array)[i], *it, path + "[" + std::to_string(i) + "]", errs, depth + 1);
    }
    if (v.type == Value::Type::MAP && v.map) {
        if (const Value* r = o4get(schema, "required")) if (r->type == Value::Type::ARRAY && r->array)
            for (auto& name : *r->array) {
                bool found = false;
                for (auto& kv : *v.map) if (kv.first.toDisplayString() == name.toDisplayString()) { found = true; break; }
                if (!found) schemaErr(errs, path, "missing required property '" + name.toDisplayString() + "'");
            }
        const Value* props = o4get(schema, "properties");
        for (auto& kv : *v.map) {
            std::string k = kv.first.toDisplayString();
            const Value* ps = props ? o4get(*props, k) : nullptr;
            std::string sub = path.empty() ? k : path + "." + k;
            if (ps) validateRec(kv.second, *ps, sub, errs, depth + 1);
            else if (const Value* ap = o4get(schema, "additionalProperties")) {
                if (ap->type == Value::Type::BOOL && !ap->boolean) schemaErr(errs, sub, "additional property not allowed");
                else if (ap->type == Value::Type::MAP) validateRec(kv.second, *ap, sub, errs, depth + 1);
            }
        }
    }
}

static bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// ============================================================================
//  semver
// ============================================================================
struct SV {
    long long maj = 0, min = 0, pat = 0;
    std::vector<std::string> pre;
    std::string build;
};
static bool parseSV(std::string s, SV& out) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(s.begin());
    SV v;
    size_t plus = s.find('+');
    if (plus != std::string::npos) { v.build = s.substr(plus + 1); s = s.substr(0, plus); }
    size_t dash = s.find('-');
    std::string pre;
    if (dash != std::string::npos) { pre = s.substr(dash + 1); s = s.substr(0, dash); if (pre.empty()) return false; }
    std::vector<std::string> nums;
    { std::string cur; for (char c : s) { if (c == '.') { nums.push_back(cur); cur.clear(); } else cur += c; } nums.push_back(cur); }
    if (nums.size() != 3) return false;
    for (auto& n : nums) if (!allDigits(n) || (n.size() > 1 && n[0] == '0') || n.size() > 15) return false;
    v.maj = std::atoll(nums[0].c_str()); v.min = std::atoll(nums[1].c_str()); v.pat = std::atoll(nums[2].c_str());
    if (!pre.empty()) {
        std::string cur;
        for (size_t i = 0; i <= pre.size(); ++i) {
            if (i == pre.size() || pre[i] == '.') {
                if (cur.empty()) return false;
                for (char c : cur) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
                v.pre.push_back(cur); cur.clear();
            } else cur += pre[i];
        }
    }
    out = v;
    return true;
}
static std::string svStr(const SV& v) {
    std::string s = std::to_string(v.maj) + "." + std::to_string(v.min) + "." + std::to_string(v.pat);
    if (!v.pre.empty()) { s += "-"; for (size_t i = 0; i < v.pre.size(); ++i) s += (i ? "." : "") + v.pre[i]; }
    if (!v.build.empty()) s += "+" + v.build;
    return s;
}
static int svCmp(const SV& a, const SV& b) {
    if (a.maj != b.maj) return a.maj < b.maj ? -1 : 1;
    if (a.min != b.min) return a.min < b.min ? -1 : 1;
    if (a.pat != b.pat) return a.pat < b.pat ? -1 : 1;
    if (a.pre.empty() && b.pre.empty()) return 0;
    if (a.pre.empty()) return 1;
    if (b.pre.empty()) return -1;
    for (size_t i = 0; i < std::max(a.pre.size(), b.pre.size()); ++i) {
        if (i >= a.pre.size()) return -1;
        if (i >= b.pre.size()) return 1;
        bool an = allDigits(a.pre[i]), bn = allDigits(b.pre[i]);
        if (an && bn) { long long x = std::atoll(a.pre[i].c_str()), y = std::atoll(b.pre[i].c_str()); if (x != y) return x < y ? -1 : 1; }
        else if (an) return -1;
        else if (bn) return 1;
        else if (a.pre[i] != b.pre[i]) return a.pre[i] < b.pre[i] ? -1 : 1;
    }
    return 0;
}

struct Cmp { std::string op; SV v; };          // op: ">=", "<", "=", ...
using CmpSet = std::vector<Cmp>;               // AND
// يحوّل قيداً واحداً (^1.2 / ~1 / >=1.0.0 / 1.x / *) إلى مقارنات؛ false إن كان غير صالح.
static bool parseConstraintTok(std::string tok, CmpSet& out) {
    std::string op;
    size_t i = 0;
    while (i < tok.size() && (tok[i] == '^' || tok[i] == '~' || tok[i] == '>' || tok[i] == '<' || tok[i] == '=')) op += tok[i++];
    std::string rest = tok.substr(i);
    if (!rest.empty() && (rest[0] == 'v' || rest[0] == 'V')) rest.erase(rest.begin());
    if (rest.empty() || rest == "*" || rest == "x" || rest == "X") { if (!op.empty() && op != "=" ) return false; return true; }
    std::string pre;
    size_t dash = rest.find('-');
    if (dash != std::string::npos) { pre = rest.substr(dash); rest = rest.substr(0, dash); }
    std::vector<std::string> parts;
    { std::string cur; for (char c : rest) { if (c == '.') { parts.push_back(cur); cur.clear(); } else cur += c; } parts.push_back(cur); }
    if (parts.size() > 3) return false;
    long long n[3] = {0, 0, 0};
    int given = 0;
    bool wildSeen = false;
    for (size_t k = 0; k < parts.size(); ++k) {
        const std::string& p = parts[k];
        if (p == "x" || p == "X" || p == "*") { wildSeen = true; continue; }
        if (wildSeen || !allDigits(p)) return false;
        n[k] = std::atoll(p.c_str()); ++given;
    }
    if (given == 0) return true;
    auto mk = [&](long long a, long long b, long long c) { SV v; v.maj = a; v.min = b; v.pat = c; return v; };
    bool full = (given == 3);
    SV base = mk(n[0], n[1], n[2]);
    if (full && !pre.empty()) { SV tmp; if (!parseSV(std::to_string(n[0]) + "." + std::to_string(n[1]) + "." + std::to_string(n[2]) + pre, tmp)) return false; base = tmp; }
    if (op == "^") {
        out.push_back({">=", base});
        SV hi;
        if (n[0] > 0 || given == 1) hi = mk(n[0] + 1, 0, 0);
        else if (n[1] > 0 || given == 2) hi = mk(0, n[1] + 1, 0);
        else hi = mk(0, 0, n[2] + 1);
        out.push_back({"<", hi});
    } else if (op == "~") {
        out.push_back({">=", base});
        out.push_back({"<", given == 1 ? mk(n[0] + 1, 0, 0) : mk(n[0], n[1] + 1, 0)});
    } else if (op == ">=" || op == "<=" || op == ">" || op == "<") {
        if (full) out.push_back({op, base});
        else if (op == ">=") out.push_back({">=", base});
        else if (op == ">") out.push_back({">=", given == 1 ? mk(n[0] + 1, 0, 0) : mk(n[0], n[1] + 1, 0)});
        else if (op == "<") out.push_back({"<", base});
        else out.push_back({"<", given == 1 ? mk(n[0] + 1, 0, 0) : mk(n[0], n[1] + 1, 0)});
    } else if (op.empty() || op == "=") {
        if (full) out.push_back({"=", base});
        else {
            out.push_back({">=", base});
            out.push_back({"<", given == 1 ? mk(n[0] + 1, 0, 0) : mk(n[0], n[1] + 1, 0)});
        }
    } else return false;
    return true;
}
static bool parseConstraint(const std::string& c, std::vector<CmpSet>& sets) {
    std::string s = c;
    std::vector<std::string> alts;
    { size_t p; std::string cur = s; while ((p = cur.find("||")) != std::string::npos) { alts.push_back(cur.substr(0, p)); cur = cur.substr(p + 2); } alts.push_back(cur); }
    for (auto& alt : alts) {
        std::string norm;
        for (char ch : alt) norm += (ch == ',') ? ' ' : ch;
        std::vector<std::string> toks;
        { std::istringstream is(norm); std::string t; while (is >> t) toks.push_back(t); }
        // ">= 1.0.0" مكتوب بمسافة: ادمج العامل المنفرد مع ما بعده
        std::vector<std::string> merged;
        for (size_t i = 0; i < toks.size(); ++i) {
            bool onlyOp = !toks[i].empty() && toks[i].find_first_not_of("^~<>=") == std::string::npos;
            if (onlyOp && i + 1 < toks.size()) { merged.push_back(toks[i] + toks[i + 1]); ++i; }
            else merged.push_back(toks[i]);
        }
        CmpSet set;
        for (auto& t : merged) if (!parseConstraintTok(t, set)) return false;
        sets.push_back(set);
    }
    return true;
}
static bool satisfiesSet(const SV& v, const CmpSet& set) {
    for (auto& c : set) {
        int r = svCmp(v, c.v);
        bool ok = c.op == ">=" ? r >= 0 : c.op == "<=" ? r <= 0 : c.op == ">" ? r > 0 : c.op == "<" ? r < 0 : r == 0;
        if (!ok) return false;
    }
    if (!v.pre.empty()) {   // إصدار تجريبي لا يطابق إلا إن ذكر القيد نفس الثلاثية بإصدار تجريبي
        bool allowed = false;
        for (auto& c : set) if (!c.v.pre.empty() && c.v.maj == v.maj && c.v.min == v.min && c.v.pat == v.pat) allowed = true;
        if (!allowed) return false;
    }
    return true;
}
static bool satisfiesStr(const SV& v, const std::string& constraint) {
    std::vector<CmpSet> sets;
    if (!parseConstraint(constraint, sets)) return false;
    for (auto& s : sets) if (satisfiesSet(v, s)) return true;
    return false;
}
static std::string svDiff(const SV& a, const SV& b) {
    if (svCmp(a, b) == 0) return "none";
    if (a.maj != b.maj) return "major";
    if (a.min != b.min) return "minor";
    if (a.pat != b.pat) return "patch";
    return "prerelease";
}
static Value svToValue(const SV& v) {
    ArrayData pre;
    for (auto& p : v.pre) pre.push_back(allDigits(p) ? N4(static_cast<double>(std::atoll(p.c_str()))) : S4(p));
    return M4({{"major", N4(static_cast<double>(v.maj))}, {"minor", N4(static_cast<double>(v.min))}, {"patch", N4(static_cast<double>(v.pat))},
               {"prerelease", A4(std::move(pre))}, {"build", S4(v.build)}, {"version", S4(svStr(v))}});
}

// ============================================================================
//  TOML-lite (كافٍ لـ rin.toml): [a.b] sections, key = "str" | number | bool | [arrays]
// ============================================================================
static std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
static bool tomlScalar(const std::string& raw, Value& out, std::string& err);
static bool tomlArray(const std::string& raw, Value& out, std::string& err) {
    std::string s = trimStr(raw);
    if (s.size() < 2 || s.front() != '[' || s.back() != ']') { err = "bad array"; return false; }
    s = s.substr(1, s.size() - 2);
    ArrayData items;
    std::string cur; bool inStr = false; int depth = 0;
    auto flush = [&]() -> bool {
        std::string t = trimStr(cur); cur.clear();
        if (t.empty()) return true;
        Value v;
        if (!tomlScalar(t, v, err)) return false;
        items.push_back(v);
        return true;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { cur += c; if (c == '\\' && i + 1 < s.size()) cur += s[++i]; else if (c == '"') inStr = false; continue; }
        if (c == '"') { inStr = true; cur += c; }
        else if (c == '[') { ++depth; cur += c; }
        else if (c == ']') { --depth; cur += c; }
        else if (c == ',' && depth == 0) { if (!flush()) return false; }
        else cur += c;
    }
    if (!flush()) return false;
    out = A4(std::move(items));
    return true;
}
static bool tomlScalar(const std::string& raw, Value& out, std::string& err) {
    std::string s = trimStr(raw);
    if (s.empty()) { err = "empty value"; return false; }
    if (s[0] == '"') {
        std::string o;
        size_t i = 1;
        for (; i < s.size() && s[i] != '"'; ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) { case 'n': o += '\n'; break; case 't': o += '\t'; break; case '"': o += '"'; break; case '\\': o += '\\'; break; default: o += s[i]; }
            } else o += s[i];
        }
        if (i >= s.size()) { err = "unterminated string"; return false; }
        out = S4(o);
        return true;
    }
    if (s[0] == '\'') {
        size_t e = s.find('\'', 1);
        if (e == std::string::npos) { err = "unterminated string"; return false; }
        out = S4(s.substr(1, e - 1));
        return true;
    }
    if (s[0] == '[') return tomlArray(s, out, err);
    if (s == "true") { out = B4(true); return true; }
    if (s == "false") { out = B4(false); return true; }
    char* end = nullptr;
    std::string clean;
    for (char c : s) if (c != '_') clean += c;
    double d = std::strtod(clean.c_str(), &end);
    if (end && *end == '\0' && !clean.empty()) { out = N4(d); return true; }
    err = "unsupported value: " + s;
    return false;
}
static std::string stripTomlComment(const std::string& line) {
    bool inStr = false; char q = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inStr) { if (c == '\\' && q == '"') ++i; else if (c == q) inStr = false; }
        else if (c == '"' || c == '\'') { inStr = true; q = c; }
        else if (c == '#') return line.substr(0, i);
    }
    return line;
}
static void setNested(MapData& root, const std::vector<std::string>& path, size_t i, const std::string& key, const Value& v) {
    if (i == path.size()) {
        for (auto& kv : root) if (kv.first.toDisplayString() == key) { kv.second = v; return; }
        root.push_back({S4(key), v});
        return;
    }
    for (auto& kv : root)
        if (kv.first.toDisplayString() == path[i]) {
            if (kv.second.type != Value::Type::MAP || !kv.second.map) kv.second = newMap();
            kv.second.map = std::make_shared<MapData>(*kv.second.map);   // لا مشاركة مع قيم أخرى
            setNested(*kv.second.map, path, i + 1, key, v);
            return;
        }
    root.push_back({S4(path[i]), newMap()});
    setNested(*root.back().second.map, path, i + 1, key, v);
}
static bool parseToml(const std::string& text, Value& out, std::string& err, int& errLine) {
    MapData root;
    std::vector<std::string> section;
    std::istringstream is(text);
    std::string line;
    int ln = 0;
    while (std::getline(is, line)) {
        ++ln;
        std::string t = trimStr(stripTomlComment(line));
        if (t.empty()) continue;
        if (t[0] == '[') {
            if (t.size() < 3 || t.back() != ']') { err = "bad section header"; errLine = ln; return false; }
            std::string name = trimStr(t.substr(1, t.size() - 2));
            section.clear();
            std::string cur;
            for (char c : name) { if (c == '.') { section.push_back(trimStr(cur)); cur.clear(); } else cur += c; }
            section.push_back(trimStr(cur));
            for (auto& s : section) if (s.empty()) { err = "empty section name"; errLine = ln; return false; }
            // تأكد من وجود الجدول حتى لو كان فارغاً
            { MapData* m = &root; for (auto& seg : section) { bool f = false; for (auto& kv : *m) if (kv.first.toDisplayString() == seg) { if (kv.second.type != Value::Type::MAP) { err = "section conflicts with value"; errLine = ln; return false; } kv.second.map = std::make_shared<MapData>(*kv.second.map); m = kv.second.map.get(); f = true; break; } if (!f) { m->push_back({S4(seg), newMap()}); m = m->back().second.map.get(); } } }
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) { err = "expected key = value"; errLine = ln; return false; }
        std::string key = trimStr(t.substr(0, eq));
        std::string val = trimStr(t.substr(eq + 1));
        if (key.size() >= 2 && key.front() == '"' && key.back() == '"') key = key.substr(1, key.size() - 2);
        if (key.empty()) { err = "empty key"; errLine = ln; return false; }
        // مصفوفة متعددة الأسطر
        if (!val.empty() && val[0] == '[') {
            auto balanced = [](const std::string& s) { int d = 0; bool in = false; for (size_t i = 0; i < s.size(); ++i) { char c = s[i]; if (in) { if (c == '\\') ++i; else if (c == '"') in = false; } else if (c == '"') in = true; else if (c == '[') ++d; else if (c == ']') --d; } return d <= 0; };
            while (!balanced(val) && std::getline(is, line)) { ++ln; val += " " + trimStr(stripTomlComment(line)); }
        }
        Value v;
        std::string e2;
        if (!tomlScalar(val, v, e2)) { err = e2; errLine = ln; return false; }
        setNested(root, section, 0, key, v);
    }
    out = newMap(std::move(root));
    return true;
}
static std::string tomlKey(const std::string& k) {
    bool bare = !k.empty();
    for (char c : k) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) bare = false;
    if (bare) return k;
    std::ostringstream os; json::encodeEscaped(k, os); return os.str();
}
static std::string tomlVal(const Value& v) {
    switch (v.type) {
        case Value::Type::STRING: { std::ostringstream os; json::encodeEscaped(v.str, os); return os.str(); }
        case Value::Type::NUMBER: return numToJson(v.number);
        case Value::Type::BOOL: return v.boolean ? "true" : "false";
        case Value::Type::ARRAY: {
            std::string s = "[";
            if (v.array) for (size_t i = 0; i < v.array->size(); ++i) s += (i ? ", " : "") + tomlVal((*v.array)[i]);
            return s + "]";
        }
        default: return "\"\"";
    }
}
static void writeToml(const MapData& m, const std::string& prefix, std::string& out) {
    bool wroteScalar = false;
    for (auto& kv : m) {
        if (kv.second.type == Value::Type::MAP) continue;
        out += tomlKey(kv.first.toDisplayString()) + " = " + tomlVal(kv.second) + "\n";
        wroteScalar = true;
    }
    for (auto& kv : m) {
        if (kv.second.type != Value::Type::MAP || !kv.second.map) continue;
        std::string name = prefix.empty() ? tomlKey(kv.first.toDisplayString()) : prefix + "." + tomlKey(kv.first.toDisplayString());
        if (wroteScalar || !out.empty()) out += "\n";
        out += "[" + name + "]\n";
        writeToml(*kv.second.map, name, out);
        wroteScalar = false;
    }
}

// ============================================================================
//  API scan: استخراج الدوال/الأصناف المعرّفة في مصدر Rin مع تعليقاتها الوثائقية
// ============================================================================
static Value scanApi(const std::string& src) {
    static const std::regex reFun(R"(^\s*(export\s+)?fun\s+([^\s(]+)\s*\(([^)]*)\))");
    static const std::regex reTyp(R"(^\s*(export\s+)?(class|struct|enum)\s+([^\s{(:]+))");
    static const std::regex reLet(R"(^(export\s+)(let|const)\s+([A-Za-z_][\w]*))");
    ArrayData out;
    std::istringstream is(src);
    std::string line;
    std::vector<std::string> doc;
    int ln = 0;
    while (std::getline(is, line)) {
        ++ln;
        std::string t = trimStr(line);
        if (t.rfind("//", 0) == 0) {
            size_t k = 2;
            while (k < t.size() && t[k] == '/') ++k;
            doc.push_back(trimStr(t.substr(k)));
            continue;
        }
        std::smatch m;
        std::string kind, name, params;
        bool exported = false;
        if (std::regex_search(line, m, reFun)) { kind = "function"; name = m[2]; params = trimStr(m[3]); exported = m[1].matched; }
        else if (std::regex_search(line, m, reTyp)) { kind = m[2]; name = m[3]; exported = m[1].matched; }
        else if (std::regex_search(line, m, reLet)) { kind = "value"; name = m[3]; exported = true; }
        if (!kind.empty()) {
            std::string d;
            for (size_t i = 0; i < doc.size(); ++i) d += (i ? "\n" : "") + doc[i];
            ArrayData plist;
            if (!params.empty()) { std::string cur; for (char c : params + ",") { if (c == ',') { std::string p = trimStr(cur); if (!p.empty()) plist.push_back(S4(p)); cur.clear(); } else cur += c; } }
            out.push_back(M4({{"kind", S4(kind)}, {"name", S4(name)}, {"params", A4(std::move(plist))}, {"doc", S4(d)},
                              {"line", N4(ln)}, {"exported", B4(exported)}}));
        }
        if (!t.empty()) doc.clear();
        else doc.clear();
    }
    return A4(std::move(out));
}

// ============================================================================
//  ملفات: اشتقاق/كتابة (داخل العزل عبر resolvePath)
// ============================================================================
static bool writeTextFile(const std::string& full, const std::string& content) {
    std::ofstream f(full, std::ios::binary);
    if (!f) return false;
    f << content;
    return static_cast<bool>(f);
}

// ============================================================================
//  cpp.* — مساعدات الجسر الأصلي
// ============================================================================
static const char* kAbiHeader = R"RINABI(// rin_abi.h — ترويسة مساعدة لكتابة دوال C++ تُستدعى من Rin (cpp.call / cpp.exec).
// ---------------------------------------------------------------------------
//  نمط 1 (الأسهل): دوال JSON — تستقبل وسائط Rin كمصفوفة JSON وتُرجع أي قيمة:
//
//      #include "rin_abi.h"
//      RIN_FN(sumAll) {                       // args = مصفوفة الوسائط كما مرّرها Rin
//          double t = 0;
//          for (auto& v : args.arr) t += v.num;
//          return rin::Json::number(t);
//      }
//
//      من Rin:   let h = cpp.lib(src).handle;  print cpp.call(h, "sumAll", [1,2,3]).value;
//
//  نمط 2 (الأسرع): دوال رقمية C عادية، كلها double أو كلها int64:
//
//      extern "C" double hyp(double a, double b) { return std::sqrt(a*a + b*b); }
//      من Rin:   cpp.callNum(h, "hyp", [3, 4])   // => 5
// ---------------------------------------------------------------------------
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace rin {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;   // يحفظ ترتيب الإدخال

    static Json null() { return Json(); }
    static Json boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
    static Json number(double v) { Json j; j.type = Num; j.num = v; return j; }
    static Json string(const std::string& v) { Json j; j.type = Str; j.str = v; return j; }
    static Json array() { Json j; j.type = Arr; return j; }
    static Json object() { Json j; j.type = Obj; return j; }

    Json& push(const Json& v) { if (type != Arr) { type = Arr; arr.clear(); } arr.push_back(v); return *this; }
    Json& set(const std::string& k, const Json& v) {
        if (type != Obj) { type = Obj; obj.clear(); }
        for (auto& kv : obj) if (kv.first == k) { kv.second = v; return *this; }
        obj.push_back({k, v});
        return *this;
    }
    const Json* get(const std::string& k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    double numOr(const std::string& k, double d) const { auto* p = get(k); return p && p->type == Num ? p->num : d; }
    std::string strOr(const std::string& k, const std::string& d) const { auto* p = get(k); return p && p->type == Str ? p->str : d; }

    // ------------------------------------------------------------ dump
    static void esc(const std::string& s, std::string& o) {
        o += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); o += buf; }
                    else o += (char)c;
            }
        }
        o += '"';
    }
    std::string dump() const { std::string o; dumpTo(o); return o; }
    void dumpTo(std::string& o) const {
        switch (type) {
            case Null: o += "null"; break;
            case Bool: o += b ? "true" : "false"; break;
            case Num: {
                if (!std::isfinite(num)) { o += "null"; break; }
                char buf[40];
                if (num == std::floor(num) && std::fabs(num) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", num);
                else std::snprintf(buf, sizeof buf, "%.17g", num);
                o += buf;
                break;
            }
            case Str: esc(str, o); break;
            case Arr:
                o += '[';
                for (size_t i = 0; i < arr.size(); ++i) { if (i) o += ','; arr[i].dumpTo(o); }
                o += ']';
                break;
            case Obj:
                o += '{';
                for (size_t i = 0; i < obj.size(); ++i) { if (i) o += ','; esc(obj[i].first, o); o += ':'; obj[i].second.dumpTo(o); }
                o += '}';
                break;
        }
    }

    // ------------------------------------------------------------ parse
    static Json parse(const std::string& s) { size_t p = 0; Json j = val(s, p); return j; }

private:
    static void ws(const std::string& s, size_t& p) { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\t' || s[p] == '\r')) ++p; }
    static void utf8(unsigned cp, std::string& o) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    static std::string strLit(const std::string& s, size_t& p) {
        std::string o; ++p;   // "
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                ++p;
                switch (s[p]) {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                    case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                    case 'u': {
                        unsigned cp = (unsigned)std::strtoul(s.substr(p + 1, 4).c_str(), nullptr, 16); p += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && s.compare(p + 1, 2, "\\u") == 0) {
                            unsigned lo = (unsigned)std::strtoul(s.substr(p + 3, 4).c_str(), nullptr, 16);
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6;
                        }
                        utf8(cp, o); break;
                    }
                    default: o += s[p];
                }
                ++p;
            } else o += s[p++];
        }
        ++p;   // "
        return o;
    }
    static Json val(const std::string& s, size_t& p) {
        ws(s, p);
        if (p >= s.size()) return Json();
        char c = s[p];
        if (c == '{') {
            Json j = object(); ++p; ws(s, p);
            if (p < s.size() && s[p] == '}') { ++p; return j; }
            while (p < s.size()) {
                ws(s, p); std::string k = strLit(s, p); ws(s, p);
                if (p < s.size() && s[p] == ':') ++p;
                j.obj.push_back({k, val(s, p)}); ws(s, p);
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                break;
            }
            if (p < s.size() && s[p] == '}') ++p;
            return j;
        }
        if (c == '[') {
            Json j = array(); ++p; ws(s, p);
            if (p < s.size() && s[p] == ']') { ++p; return j; }
            while (p < s.size()) {
                j.arr.push_back(val(s, p)); ws(s, p);
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                break;
            }
            if (p < s.size() && s[p] == ']') ++p;
            return j;
        }
        if (c == '"') return string(strLit(s, p));
        if (s.compare(p, 4, "true") == 0) { p += 4; return boolean(true); }
        if (s.compare(p, 5, "false") == 0) { p += 5; return boolean(false); }
        if (s.compare(p, 4, "null") == 0) { p += 4; return Json(); }
        char* end = nullptr;
        double d = std::strtod(s.c_str() + p, &end);
        p = (size_t)(end - s.c_str());
        return number(d);
    }
};

// مخزن مؤقت لنص النتيجة (صالح حتى الاستدعاء التالي في نفس الخيط).
inline const char* keep(const std::string& s) {
    static thread_local std::string buf;
    buf = s;
    return buf.c_str();
}

}  // namespace rin

// يعرّف دالة JSON مُصدَّرة:  RIN_FN(name) { ... return rin::Json::...; }
// المتغيّر args (من نوع rin::Json، مصفوفة) متاح داخل الجسم.
#define RIN_FN(name)                                                                     \
    static rin::Json name##_impl(const rin::Json& args);                                 \
    extern "C" __attribute__((visibility("default"))) const char* name(const char* a) { \
        return rin::keep(name##_impl(rin::Json::parse(a ? a : "[]")).dump());            \
    }                                                                                    \
    static rin::Json name##_impl(const rin::Json& args)
)RINABI";

static bool nativeAllowed() {
    const char* e = std::getenv("RIN_ALLOW_NATIVE");
    return e && (std::string(e) == "1" || std::string(e) == "true" || std::string(e) == "yes");
}
static const char* kNativeOffMsg =
    "native code is disabled: run the program with RIN_ALLOW_NATIVE=1 (or `rin --allow-native ...`) to enable cpp.*";

#ifdef RIN_NATIVE_TOOLCHAIN
static std::string shq(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}
static std::string findInPath(const std::string& exe) {
    const char* p = std::getenv("PATH");
    if (!p) return "";
    std::string path = p, cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == ':') {
            if (!cur.empty()) { std::string full = cur + "/" + exe; if (::access(full.c_str(), X_OK) == 0) return full; }
            cur.clear();
        } else cur += path[i];
    }
    return "";
}
static std::string findCompiler(const std::string& pref) {
    if (!pref.empty()) return findInPath(pref);
    if (const char* cxx = std::getenv("CXX")) { std::string r = findInPath(cxx); if (!r.empty()) return r; }
    for (const char* c : {"g++", "clang++", "c++"}) { std::string r = findInPath(c); if (!r.empty()) return r; }
    return "";
}
static void mkdirp(const std::string& dir) {
    std::string part;
    for (size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') { if (!part.empty()) ::mkdir(part.c_str(), 0700); }
        if (i < dir.size()) part += dir[i];
    }
}
static std::string cacheDir() {
    std::string d = std::string("/tmp/rin-cpp-") + std::to_string(static_cast<long>(::getuid()));
    mkdirp(d);
    return d;
}
static std::string fnv128(const std::string& s) {
    uint64_t h1 = 1469598103934665603ULL, h2 = 0x9E3779B97F4A7C15ULL;
    for (unsigned char c : s) { h1 = (h1 ^ c) * 1099511628211ULL; h2 = (h2 + c) * 0xff51afd7ed558ccdULL; h2 ^= h2 >> 29; }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", (unsigned long long)h1, (unsigned long long)h2);
    return buf;
}
static double nowMs4() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct ExecResult { int exitCode = -1; std::string out, err; double ms = 0; bool timedOut = false; };
static ExecResult runShell(const std::string& cmd, const std::string& stdinText, int timeoutSec) {
    ExecResult r;
    std::string base = cacheDir() + "/run-" + std::to_string(::getpid()) + "-" + std::to_string(static_cast<long long>(nowMs4() * 1000));
    std::string fin = base + ".in", fout = base + ".out", ferr = base + ".err";
    writeTextFile(fin, stdinText);
    std::string tmo = findInPath("timeout").empty() ? "" : ("timeout " + std::to_string(timeoutSec) + " ");
    double t0 = nowMs4();
    int rc = std::system((tmo + cmd + " < " + shq(fin) + " > " + shq(fout) + " 2> " + shq(ferr)).c_str());
    r.ms = nowMs4() - t0;
    r.exitCode = (rc == -1) ? -1 : (WIFEXITED(rc) ? WEXITSTATUS(rc) : 128 + (WIFSIGNALED(rc) ? WTERMSIG(rc) : 0));
    r.timedOut = (r.exitCode == 124 && !tmo.empty());
    readWholeFile(fout, r.out);
    readWholeFile(ferr, r.err);
    ::remove(fin.c_str()); ::remove(fout.c_str()); ::remove(ferr.c_str());
    return r;
}
// يبني (أو يجلب من الكاش) ملف تنفيذي/مكتبة مشتركة؛ يُرجع قاموس النتيجة.
static Value compileSource(const std::string& src, const Value& opts, bool shared, const std::string& explicitOut) {
    std::string compiler = findCompiler(o4str(opts, "compiler", ""));
    if (compiler.empty())
        return M4({{"ok", B4(false)}, {"error", S4("no C++ compiler found (install g++ or clang++, or set CXX)")}});
    std::string dir = cacheDir();
    { std::ofstream h(dir + "/rin_abi.h", std::ios::binary); h << kAbiHeader; }
    std::string std_ = o4str(opts, "std", "c++17");
    std::string opt = o4str(opts, "opt", "-O2");
    std::string flags = "-std=" + std_ + " " + opt + " -fPIC";
    if (shared) flags += " -shared";
    for (auto& f : o4strs(opts, "flags")) flags += " " + shq(f);
    for (auto& d : o4strs(opts, "includes")) flags += " -I" + shq(d);
    std::string libs;
    for (auto& l : o4strs(opts, "libs")) libs += " -l" + shq(l);
    std::string key = fnv128(compiler + "|" + flags + "|" + libs + "|" + src + "|" + fnv128(kAbiHeader));
    std::string ext = shared ? ".so" : "";
#ifdef __APPLE__
    if (shared) ext = ".dylib";
#endif
    std::string outPath = explicitOut.empty() ? (dir + "/" + key + ext) : explicitOut;
    bool force = o4bool(opts, "force", false);
    struct stat st;
    if (!force && ::stat(outPath.c_str(), &st) == 0 && explicitOut.empty())
        return M4({{"ok", B4(true)}, {"path", S4(outPath)}, {"cached", B4(true)}, {"compiler", S4(compiler)}, {"stderr", S4("")}, {"ms", N4(0)}});
    std::string srcPath = dir + "/" + key + ".cpp";
    writeTextFile(srcPath, src);
    std::string cmd = shq(compiler) + " " + flags + " -I" + shq(dir) + " " + shq(srcPath) + " -o " + shq(outPath) + libs;
    ExecResult r = runShell(cmd, "", static_cast<int>(std::max(5.0, std::min(300.0, o4num(opts, "timeout", 120)))));
    bool ok = (r.exitCode == 0);
    if (!ok) ::remove(outPath.c_str());
    return M4({{"ok", B4(ok)}, {"path", S4(ok ? outPath : "")}, {"cached", B4(false)}, {"compiler", S4(compiler)},
               {"stderr", S4(r.err)}, {"ms", N4(std::floor(r.ms))}, {"exitCode", N4(r.exitCode)},
               {"error", S4(ok ? "" : (r.timedOut ? "compilation timed out" : "compilation failed"))}});
}
static std::map<int, void*>& libTable() { static std::map<int, void*> t; return t; }
static int& libCounter() { static int c = 0; return c; }
#endif  // RIN_NATIVE_TOOLCHAIN

} // namespace extra4

// ============================================================================
void Interpreter::registerNativesExtra4() {
    using namespace extra4;

    // ================================================================ 0) lang.* — فحص صياغة مصدر Rin
    // lang.check(src) -> {ok, line, message}: يمرّر المصدر على نفس Lexer/Parser الحقيقيين دون تنفيذه.
    natives["lang.check"] = [](Args& a, int line) -> Value {
        need("lang.check", a, 1, 1, line);
        std::string src = str(a[0], "lang.check", line);
        try {
            Lexer lexer(src, "<lang.check>");
            auto tokens = lexer.scanTokens();
            Parser parser(tokens, "<lang.check>");
            parser.parse();
            return M4({{"ok", B4(true)}, {"line", N4(0)}, {"message", S4("")}});
        } catch (RinError& e) {
            std::string msg = e.diagnostic ? e.diagnostic->message : e.message;
            return M4({{"ok", B4(false)}, {"line", N4(e.line)}, {"message", S4(msg)}});
        } catch (std::exception& e) {
            return M4({{"ok", B4(false)}, {"line", N4(0)}, {"message", S4(e.what())}});
        }
    };

    // lang.isBuiltin(name) -> هل الاسم دالة مدمجة في المحرك (تعريف fun بنفس الاسم يرفضه المحرك E0002)
    natives["lang.isBuiltin"] = [this](Args& a, int line) -> Value {
        need("lang.isBuiltin", a, 1, 1, line);
        return B4(natives.find(str(a[0], "lang.isBuiltin", line)) != natives.end());
    };

    // ================================================================ 1) json.*
    natives["json.parse"] = [](Args& a, int line) -> Value {
        need("json.parse", a, 1, 1, line);
        Value out; std::string err;
        if (!decodeStrict(str(a[0], "json.parse", line), out, err))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "json.parse: invalid JSON — " + err);
        return out;
    };
    natives["json.tryParse"] = [](Args& a, int line) -> Value {
        need("json.tryParse", a, 1, 1, line);
        Value out; std::string err;
        bool ok = decodeStrict(str(a[0], "json.tryParse", line), out, err);
        return M4({{"ok", B4(ok)}, {"value", ok ? out : Value::nil()}, {"error", S4(ok ? "" : err)}});
    };
    natives["json.valid"] = [](Args& a, int line) -> Value {
        need("json.valid", a, 1, 1, line);
        if (a[0].type != Value::Type::STRING) return B4(false);
        Value out; std::string err;
        return B4(decodeStrict(a[0].str, out, err));
    };
    natives["json.stringify"] = [](Args& a, int line) -> Value {
        need("json.stringify", a, 1, 3, line);
        int indent = a.size() > 1 && a[1].type == Value::Type::NUMBER ? std::max(0, std::min(16, static_cast<int>(a[1].number))) : 0;
        bool sortKeys = a.size() > 2 && a[2].isTruthy();
        return S4(stringifyJson(a[0], indent, sortKeys));
    };
    natives["json.pretty"] = [](Args& a, int line) -> Value {
        need("json.pretty", a, 1, 2, line);
        int indent = a.size() > 1 && a[1].type == Value::Type::NUMBER ? std::max(1, std::min(16, static_cast<int>(a[1].number))) : 2;
        return S4(stringifyJson(a[0], indent, false));
    };
    natives["json.canonical"] = [](Args& a, int line) -> Value {   // مفاتيح مرتبة + بلا فراغات: ثابت لأجل التجزئة/التوقيع
        need("json.canonical", a, 1, 1, line);
        return S4(stringifyJson(a[0], 0, true));
    };
    natives["json.minify"] = [](Args& a, int line) -> Value {
        need("json.minify", a, 1, 1, line);
        Value out; std::string err;
        if (!decodeStrict(str(a[0], "json.minify", line), out, err))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "json.minify: invalid JSON — " + err);
        return S4(stringifyJson(out, 0, false));
    };
    natives["json.type"] = [](Args& a, int line) -> Value { need("json.type", a, 1, 1, line); return S4(jsonTypeName(a[0])); };
    natives["json.equals"] = [](Args& a, int line) -> Value { need("json.equals", a, 2, 2, line); return B4(valuesEqual(a[0], a[1])); };
    natives["json.get"] = [](Args& a, int line) -> Value {
        need("json.get", a, 2, 3, line);
        auto p = parseJPath(a[1], "json.get", line);
        std::vector<Value> out; bool multi = false;
        queryIn(a[0], p, 0, out, multi);
        if (multi) return A4(std::move(out));
        if (out.empty()) return a.size() > 2 ? a[2] : Value::nil();
        return out[0];
    };
    natives["json.query"] = [](Args& a, int line) -> Value {   // دائماً مصفوفة
        need("json.query", a, 2, 2, line);
        auto p = parseJPath(a[1], "json.query", line);
        std::vector<Value> out; bool multi = false;
        queryIn(a[0], p, 0, out, multi);
        return A4(std::move(out));
    };
    natives["json.has"] = [](Args& a, int line) -> Value {
        need("json.has", a, 2, 2, line);
        auto p = parseJPath(a[1], "json.has", line);
        std::vector<Value> out; bool multi = false;
        queryIn(a[0], p, 0, out, multi);
        return B4(!out.empty());
    };
    natives["json.set"] = [](Args& a, int line) -> Value {   // لا يعدّل الأصل: يُرجع نسخة جديدة
        need("json.set", a, 3, 3, line);
        return setIn(a[0], parseJPath(a[1], "json.set", line), 0, a[2], line);
    };
    natives["json.remove"] = [](Args& a, int line) -> Value {
        need("json.remove", a, 2, 2, line);
        return removeIn(a[0], parseJPath(a[1], "json.remove", line), 0);
    };
    natives["json.mergeDeep"] = [](Args& a, int line) -> Value {
        need("json.mergeDeep", a, 2, 8, line);
        Value cur = a[0];
        for (size_t i = 1; i < a.size(); ++i) cur = deepMergeValue(cur, a[i]);
        return cur;
    };
    natives["json.flatten"] = [](Args& a, int line) -> Value {
        need("json.flatten", a, 1, 2, line);
        std::string sep = a.size() > 1 ? str(a[1], "json.flatten", line) : ".";
        MapData out;
        flattenInto(a[0], "", sep, out, 0);
        return newMap(std::move(out));
    };
    natives["json.unflatten"] = [](Args& a, int line) -> Value {
        need("json.unflatten", a, 1, 2, line);
        std::string sep = a.size() > 1 ? str(a[1], "json.unflatten", line) : ".";
        const MapData& m = mp(a[0], "json.unflatten", line);
        Value root = newMap();
        for (auto& kv : m) {
            std::string k = kv.first.toDisplayString();
            std::vector<PTok> toks;
            size_t pos = 0;
            while (true) {
                size_t nx = sep.empty() ? std::string::npos : k.find(sep, pos);
                toks.push_back(mkTok(k.substr(pos, nx == std::string::npos ? std::string::npos : nx - pos)));
                if (nx == std::string::npos) break;
                pos = nx + sep.size();
            }
            root = setIn(root, toks, 0, kv.second, line);
        }
        return root;
    };
    natives["json.diff"] = [](Args& a, int line) -> Value {   // عمليات RFC 6902 (add/remove/replace)
        need("json.diff", a, 2, 2, line);
        ArrayData out;
        diffRec(a[0], a[1], "", out);
        return A4(std::move(out));
    };
    natives["json.patch"] = [](Args& a, int line) -> Value {
        need("json.patch", a, 2, 2, line);
        const ArrayData& ops = arr(a[1], "json.patch", line);
        Value cur = a[0];
        for (auto& op : ops) {
            std::string name = o4str(op, "op", "");
            std::string path = o4str(op, "path", "");
            if (name != "add" && name != "remove" && name != "replace")
                throw diagErr(diag::Code::E0007_InvalidArguments, line, "json.patch: unsupported op '" + name + "' (add/remove/replace)");
            const Value* v = o4get(op, "value");
            cur = patchOne(cur, ptrSplit(path), 0, name, v ? *v : Value::nil(), line);
        }
        return cur;
    };
    natives["json.validate"] = [](Args& a, int line) -> Value {
        need("json.validate", a, 2, 2, line);
        ArrayData errs;
        validateRec(a[0], a[1], "", errs, 0);
        bool ok = errs.empty();
        return M4({{"valid", B4(ok)}, {"errors", A4(std::move(errs))}});
    };
    natives["json.readFile"] = [this](Args& a, int line) -> Value {
        need("json.readFile", a, 1, 1, line);
        std::string content;
        if (!readWholeFile(resolvePath(str(a[0], "json.readFile", line), line), content))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "json.readFile: cannot open '" + a[0].str + "'");
        Value out; std::string err;
        if (!decodeStrict(content, out, err))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "json.readFile: invalid JSON in '" + a[0].str + "' — " + err);
        return out;
    };
    natives["json.writeFile"] = [this](Args& a, int line) -> Value {
        need("json.writeFile", a, 2, 3, line);
        std::string full = resolvePath(str(a[0], "json.writeFile", line), line);
        ensureParentDir(full);
        int indent = a.size() > 2 && a[2].type == Value::Type::NUMBER ? std::max(0, std::min(16, static_cast<int>(a[2].number))) : 2;
        return B4(writeTextFile(full, stringifyJson(a[1], indent, false) + "\n"));
    };

    // ================================================================ 2) semver.*
    natives["semver.parse"] = [](Args& a, int line) -> Value {
        need("semver.parse", a, 1, 1, line);
        SV v;
        if (a[0].type != Value::Type::STRING || !parseSV(a[0].str, v)) return Value::nil();
        return svToValue(v);
    };
    natives["semver.valid"] = [](Args& a, int line) -> Value {
        need("semver.valid", a, 1, 1, line);
        SV v;
        return B4(a[0].type == Value::Type::STRING && parseSV(a[0].str, v));
    };
    auto svPair = [](const std::string& fn, Args& a, int line, SV& x, SV& y) {
        if (a[0].type != Value::Type::STRING || !parseSV(a[0].str, x))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "': invalid version " + a[0].toDisplayString());
        if (a[1].type != Value::Type::STRING || !parseSV(a[1].str, y))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "': invalid version " + a[1].toDisplayString());
    };
    natives["semver.compare"] = [svPair](Args& a, int line) -> Value { need("semver.compare", a, 2, 2, line); SV x, y; svPair("semver.compare", a, line, x, y); return N4(svCmp(x, y)); };
    natives["semver.gt"] = [svPair](Args& a, int line) -> Value { need("semver.gt", a, 2, 2, line); SV x, y; svPair("semver.gt", a, line, x, y); return B4(svCmp(x, y) > 0); };
    natives["semver.lt"] = [svPair](Args& a, int line) -> Value { need("semver.lt", a, 2, 2, line); SV x, y; svPair("semver.lt", a, line, x, y); return B4(svCmp(x, y) < 0); };
    natives["semver.eq"] = [svPair](Args& a, int line) -> Value { need("semver.eq", a, 2, 2, line); SV x, y; svPair("semver.eq", a, line, x, y); return B4(svCmp(x, y) == 0); };
    natives["semver.diff"] = [svPair](Args& a, int line) -> Value { need("semver.diff", a, 2, 2, line); SV x, y; svPair("semver.diff", a, line, x, y); return S4(svDiff(x, y)); };
    natives["semver.validRange"] = [](Args& a, int line) -> Value {
        need("semver.validRange", a, 1, 1, line);
        std::vector<CmpSet> sets;
        return B4(a[0].type == Value::Type::STRING && parseConstraint(a[0].str, sets));
    };
    natives["semver.satisfies"] = [](Args& a, int line) -> Value {
        need("semver.satisfies", a, 2, 2, line);
        SV v;
        if (a[0].type != Value::Type::STRING || !parseSV(a[0].str, v)) return B4(false);
        return B4(satisfiesStr(v, str(a[1], "semver.satisfies", line)));
    };
    natives["semver.sort"] = [](Args& a, int line) -> Value {
        need("semver.sort", a, 1, 2, line);
        bool desc = a.size() > 1 && a[1].isTruthy();
        std::vector<std::pair<SV, std::string>> items;
        for (auto& x : arr(a[0], "semver.sort", line)) {
            SV v;
            if (x.type != Value::Type::STRING || !parseSV(x.str, v))
                throw diagErr(diag::Code::E0007_InvalidArguments, line, "'semver.sort': invalid version " + x.toDisplayString());
            items.push_back({v, x.str});
        }
        std::stable_sort(items.begin(), items.end(), [&](const auto& p, const auto& q) { int c = svCmp(p.first, q.first); return desc ? c > 0 : c < 0; });
        ArrayData out;
        for (auto& it : items) out.push_back(S4(it.second));
        return A4(std::move(out));
    };
    natives["semver.maxSatisfying"] = [](Args& a, int line) -> Value {
        need("semver.maxSatisfying", a, 2, 2, line);
        std::string cons = str(a[1], "semver.maxSatisfying", line);
        bool found = false; SV best; std::string bestStr;
        for (auto& x : arr(a[0], "semver.maxSatisfying", line)) {
            SV v;
            if (x.type != Value::Type::STRING || !parseSV(x.str, v) || !satisfiesStr(v, cons)) continue;
            if (!found || svCmp(v, best) > 0) { best = v; bestStr = x.str; found = true; }
        }
        return found ? S4(bestStr) : Value::nil();
    };
    natives["semver.bump"] = [](Args& a, int line) -> Value {
        need("semver.bump", a, 2, 3, line);
        SV v;
        if (a[0].type != Value::Type::STRING || !parseSV(a[0].str, v))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'semver.bump': invalid version " + a[0].toDisplayString());
        std::string kind = str(a[1], "semver.bump", line);
        std::string id = a.size() > 2 ? str(a[2], "semver.bump", line) : "rc";
        bool hadPre = !v.pre.empty();
        if (kind == "major") { if (!(hadPre && v.min == 0 && v.pat == 0)) ++v.maj; v.min = 0; v.pat = 0; v.pre.clear(); }
        else if (kind == "minor") { if (!(hadPre && v.pat == 0)) ++v.min; v.pat = 0; v.pre.clear(); }
        else if (kind == "patch") { if (!hadPre) ++v.pat; v.pre.clear(); }
        else if (kind == "prerelease") {
            if (!hadPre) { ++v.pat; v.pre = {id, "0"}; }
            else if (allDigits(v.pre.back())) v.pre.back() = std::to_string(std::atoll(v.pre.back().c_str()) + 1);
            else v.pre.push_back("0");
        } else throw diagErr(diag::Code::E0007_InvalidArguments, line, "'semver.bump': kind must be major|minor|patch|prerelease");
        v.build.clear();
        return S4(svStr(v));
    };

    // ================================================================ 3) pkg.*
    natives["pkg.parseManifest"] = [](Args& a, int line) -> Value {
        need("pkg.parseManifest", a, 1, 1, line);
        Value out; std::string err; int el = 0;
        if (!parseToml(str(a[0], "pkg.parseManifest", line), out, err, el))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "pkg.parseManifest: line " + std::to_string(el) + ": " + err);
        return out;
    };
    natives["pkg.readManifest"] = [this](Args& a, int line) -> Value {
        need("pkg.readManifest", a, 0, 1, line);
        std::string p = a.empty() ? "rin.toml" : str(a[0], "pkg.readManifest", line);
        std::string content;
        if (!readWholeFile(resolvePath(p, line), content)) return Value::nil();
        Value out; std::string err; int el = 0;
        if (!parseToml(content, out, err, el))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "pkg.readManifest: " + p + ":" + std::to_string(el) + ": " + err);
        return out;
    };
    natives["pkg.manifest"] = [](Args& a, int line) -> Value {
        need("pkg.manifest", a, 1, 1, line);
        std::string out;
        writeToml(mp(a[0], "pkg.manifest", line), "", out);
        return S4(out);
    };
    natives["pkg.validateManifest"] = [](Args& a, int line) -> Value {
        need("pkg.validateManifest", a, 1, 1, line);
        ArrayData errors, warnings;
        Value m = a[0];
        std::vector<Value> r; bool multi = false;
        auto get = [&](const char* path) -> Value { r.clear(); queryIn(m, parseJPath(S4(path), "pkg.validateManifest", line), 0, r, multi); return r.empty() ? Value::nil() : r[0]; };
        Value name = get("package.name"), ver = get("package.version");
        if (name.type != Value::Type::STRING || name.str.empty()) errors.push_back(S4("package.name is required"));
        else if (!std::regex_match(name.str, std::regex("^[a-z][a-z0-9_-]{1,63}$")))
            errors.push_back(S4("package.name must match ^[a-z][a-z0-9_-]{1,63}$ (lowercase, digits, - and _)"));
        SV sv;
        if (ver.type != Value::Type::STRING || ver.str.empty()) errors.push_back(S4("package.version is required"));
        else if (!parseSV(ver.str, sv)) errors.push_back(S4("package.version '" + ver.str + "' is not valid semver (MAJOR.MINOR.PATCH)"));
        for (const char* sec : {"dependencies", "dev-dependencies"}) {
            Value deps = get(sec);
            if (deps.type == Value::Type::MAP && deps.map)
                for (auto& kv : *deps.map) {
                    std::vector<CmpSet> sets;
                    if (kv.second.type != Value::Type::STRING || !parseConstraint(kv.second.str, sets))
                        errors.push_back(S4(std::string(sec) + "." + kv.first.toDisplayString() + ": invalid version constraint " + kv.second.toDisplayString()));
                    if (name.type == Value::Type::STRING && kv.first.toDisplayString() == name.str)
                        errors.push_back(S4("package depends on itself: " + name.str));
                }
        }
        if (get("package.description").type != Value::Type::STRING) warnings.push_back(S4("package.description is missing"));
        if (get("package.license").type != Value::Type::STRING) warnings.push_back(S4("package.license is missing"));
        bool ok = errors.empty();
        return M4({{"valid", B4(ok)}, {"errors", A4(std::move(errors))}, {"warnings", A4(std::move(warnings))}});
    };
    natives["pkg.depOrder"] = [](Args& a, int line) -> Value {
        need("pkg.depOrder", a, 1, 1, line);
        const MapData& g = mp(a[0], "pkg.depOrder", line);
        std::map<std::string, std::vector<std::string>> deps;
        std::set<std::string> missing;
        for (auto& kv : g) {
            std::string n = kv.first.toDisplayString();
            auto& d = deps[n];
            if (kv.second.type == Value::Type::ARRAY && kv.second.array) for (auto& x : *kv.second.array) d.push_back(x.toDisplayString());
            else if (kv.second.type == Value::Type::MAP && kv.second.map) for (auto& x : *kv.second.map) d.push_back(x.first.toDisplayString());
        }
        for (auto& kv : deps) for (auto& d : kv.second) if (!deps.count(d)) missing.insert(d);
        // Kahn: تبعيات أولاً، وبترتيب أبجدي ثابت بين المتساوين
        std::map<std::string, int> indeg;
        for (auto& kv : deps) { int c = 0; for (auto& d : kv.second) if (deps.count(d)) ++c; indeg[kv.first] = c; }
        std::set<std::string> ready;
        for (auto& kv : indeg) if (kv.second == 0) ready.insert(kv.first);
        ArrayData order;
        while (!ready.empty()) {
            std::string n = *ready.begin(); ready.erase(ready.begin());
            order.push_back(S4(n));
            for (auto& kv : deps) for (auto& d : kv.second) if (d == n && --indeg[kv.first] == 0) ready.insert(kv.first);
        }
        ArrayData cycle, miss;
        for (auto& m : missing) miss.push_back(S4(m));
        bool ok = order.size() == deps.size();
        if (!ok) {   // استخرج دورة واحدة صريحة
            std::set<std::string> done;
            for (auto& v : order) done.insert(v.str);
            std::string start;
            for (auto& kv : deps) if (!done.count(kv.first)) { start = kv.first; break; }
            std::vector<std::string> path; std::map<std::string, size_t> seen;
            std::string cur = start;
            while (!seen.count(cur)) {
                seen[cur] = path.size(); path.push_back(cur);
                std::string nxt;
                for (auto& d : deps[cur]) if (deps.count(d) && !done.count(d)) { nxt = d; break; }
                if (nxt.empty()) break;
                cur = nxt;
            }
            for (size_t i = seen.count(cur) ? seen[cur] : 0; i < path.size(); ++i) cycle.push_back(S4(path[i]));
            if (!cycle.empty()) cycle.push_back(cycle[0]);
        }
        return M4({{"ok", B4(ok)}, {"order", A4(std::move(order))}, {"cycle", A4(std::move(cycle))}, {"missing", A4(std::move(miss))}});
    };
    natives["pkg.api"] = [](Args& a, int line) -> Value { need("pkg.api", a, 1, 1, line); return scanApi(str(a[0], "pkg.api", line)); };
    natives["pkg.apiFile"] = [this](Args& a, int line) -> Value {
        need("pkg.apiFile", a, 1, 1, line);
        std::string content;
        if (!readWholeFile(resolvePath(str(a[0], "pkg.apiFile", line), line), content)) return Value::nil();
        return scanApi(content);
    };
    natives["pkg.checksum"] = [this](Args& a, int line) -> Value {
        need("pkg.checksum", a, 1, 1, line);
        std::string dir = str(a[0], "pkg.checksum", line);
        auto it = natives.find("fs.scanDir");
        if (it == natives.end()) throw diagErr(diag::Code::E0035_RuntimeError, line, "pkg.checksum needs fs.scanDir");
        std::vector<Value> sargs{S4(dir), B4(true)};
        Value listing = it->second(sargs, line);
        std::vector<std::pair<std::string, std::string>> files;   // (relative path, absolute-in-sandbox path)
        if (listing.type == Value::Type::ARRAY && listing.array)
            for (auto& e : *listing.array) {
                if (o4get(e, "isDir") && o4get(e, "isDir")->isTruthy()) continue;
                std::string p = o4str(e, "path", "");
                std::string rel = p;
                if (rel.rfind(dir, 0) == 0) { rel = rel.substr(dir.size()); while (!rel.empty() && rel[0] == '/') rel.erase(rel.begin()); }
                files.push_back({rel, p});
            }
        std::sort(files.begin(), files.end());
        std::string acc;
        for (auto& f : files) {
            std::string content;
            if (!readWholeFile(resolvePath(f.second, line), content)) continue;
            acc += f.first + "\n" + clc::sha256_hex(clc::sha256(content)) + "\n";
        }
        return M4({{"sha256", S4(clc::sha256_hex(clc::sha256(acc)))}, {"files", N4(static_cast<double>(files.size()))}});
    };
    natives["pkg.scaffold"] = [this](Args& a, int line) -> Value {
        need("pkg.scaffold", a, 2, 3, line);
        std::string dir = str(a[0], "pkg.scaffold", line), name = str(a[1], "pkg.scaffold", line);
        Value opts = a.size() > 2 ? a[2] : Value::nil();
        if (!std::regex_match(name, std::regex("^[a-z][a-z0-9_-]{1,63}$")))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "pkg.scaffold: name must match ^[a-z][a-z0-9_-]{1,63}$");
        std::string kind = o4str(opts, "kind", "lib");
        if (kind != "lib" && kind != "app")
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "pkg.scaffold: kind must be 'lib' or 'app'");
        std::string version = o4str(opts, "version", "0.1.0"), author = o4str(opts, "author", ""), license = o4str(opts, "license", "MIT");
        std::string desc = o4str(opts, "description", kind == "lib" ? "مكتبة Rin" : "تطبيق Rin");
        bool cppHelper = o4bool(opts, "cpp", false);
        std::string manifestPath = resolvePath(dir + "/rin.toml", line);
        { std::ifstream ex(manifestPath); if (ex && !o4bool(opts, "force", false)) return M4({{"ok", B4(false)}, {"error", S4("rin.toml already exists (pass {force:true} to overwrite)")}}); }
        std::string ident = name;
        for (auto& c : ident) if (c == '-') c = '_';
        MapData pkgTable;
        pkgTable.push_back({S4("name"), S4(name)});
        pkgTable.push_back({S4("version"), S4(version)});
        pkgTable.push_back({S4("description"), S4(desc)});
        if (!author.empty()) pkgTable.push_back({S4("authors"), A4({S4(author)})});
        pkgTable.push_back({S4("license"), S4(license)});
        MapData root;
        root.push_back({S4("package"), newMap(std::move(pkgTable))});
        root.push_back({S4("dependencies"), newMap()});
        std::string toml;
        writeToml(root, "", toml);
        std::vector<std::pair<std::string, std::string>> files;
        files.push_back({"rin.toml", toml});
        if (kind == "lib") {
            files.push_back({"src/lib.rin",
                "// " + name + " — " + desc + "\n"
                "// نقطة دخول الحزمة: كل ما يُعرَّف هنا يصل إلى من يكتب @import \"" + name + "\";\n\n"
                "/// يُرجع تحية باسم المستخدم.\n"
                "fun " + ident + "_hello(who) {\n    return \"Hello, \" + who + \"!\";\n}\n\n"
                "/// رقم إصدار الحزمة.\n"
                "fun " + ident + "_version() {\n    return \"" + version + "\";\n}\n"});
            files.push_back({"tests/" + ident + "_test.rin",
                "// اختبار الحزمة: rin test\n"
                "@import \"rintest\";\n"
                "@import \"src/lib.rin\";\n\n"
                "rt_test(\"hello يُحيّي الاسم\", fun() {\n    rt_expect(" + ident + "_hello(\"Rin\")).toBe(\"Hello, Rin!\");\n});\n\n"
                "rt_test(\"version صالح\", fun() {\n    rt_expect(semver.valid(" + ident + "_version())).toBeTrue();\n});\n\n"
                "rt_done();\n"});
        } else {
            files.push_back({"src/main.rin", "// نقطة الدخول - " + name + "\nprint \"Hello from " + name + "!\";\n"});
            files.push_back({"tests/basic.rin", "@import \"rintest\";\nrt_test(\"2+2\", fun() { rt_expect(2 + 2).toBe(4); });\nrt_done();\n"});
        }
        if (cppHelper) {
            files.push_back({"native/" + ident + ".cpp",
                "// مساعد C++ للحزمة " + name + " — يُبنى ويُحمَّل من Rin عبر cpp.lib (يتطلب RIN_ALLOW_NATIVE=1)\n"
                "#include \"rin_abi.h\"\n\n"
                "RIN_FN(" + ident + "_sum) {\n    double t = 0;\n    for (auto& v : args.arr) t += v.num;\n    return rin::Json::number(t);\n}\n"});
        }
        files.push_back({"README.md", "# " + name + "\n\n" + desc + "\n\n```rin\n@import \"" + name + "\";\n```\n\n## الاختبار\n\n```\nrin test\n```\n"});
        files.push_back({".gitignore", "build/\ndist/\n*.rcl\n"});
        ArrayData created;
        for (auto& f : files) {
            std::string full = resolvePath(dir + "/" + f.first, line);
            ensureParentDir(full);
            if (!writeTextFile(full, f.second)) return M4({{"ok", B4(false)}, {"error", S4("cannot write " + f.first)}});
            created.push_back(S4(dir + "/" + f.first));
        }
        return M4({{"ok", B4(true)}, {"dir", S4(dir)}, {"name", S4(name)}, {"kind", S4(kind)}, {"files", A4(std::move(created))}});
    };

    // ================================================================ 4) cpp.*
    natives["cpp.enabled"] = [](Args& a, int line) -> Value { need("cpp.enabled", a, 0, 0, line); return B4(nativeAllowed()); };
    natives["cpp.header"] = [](Args& a, int line) -> Value { need("cpp.header", a, 0, 0, line); return S4(kAbiHeader); };
    natives["cpp.info"] = [](Args& a, int line) -> Value {
        need("cpp.info", a, 0, 0, line);
#ifdef RIN_NATIVE_TOOLCHAIN
        std::string cc = findCompiler("");
        bool supported = true;
#else
        std::string cc;
        bool supported = false;
#endif
        return M4({{"supported", B4(supported)}, {"allowed", B4(nativeAllowed())}, {"compiler", cc.empty() ? Value::nil() : S4(cc)},
                   {"abi", S4("json-v1")}});
    };
#ifdef RIN_NATIVE_TOOLCHAIN
    auto offErr = []() { return M4({{"ok", B4(false)}, {"error", S4(kNativeOffMsg)}}); };
    natives["cpp.compile"] = [this, offErr](Args& a, int line) -> Value {
        need("cpp.compile", a, 1, 2, line);
        if (!nativeAllowed()) return offErr();
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        std::string out = o4str(opts, "output", "");
        if (!out.empty()) { out = resolvePath(out, line); ensureParentDir(out); }
        return compileSource(str(a[0], "cpp.compile", line), opts, o4bool(opts, "shared", true), out);
    };
    natives["cpp.run"] = [offErr](Args& a, int line) -> Value {
        need("cpp.run", a, 1, 2, line);
        if (!nativeAllowed()) return offErr();
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        Value c = compileSource(str(a[0], "cpp.run", line), opts, false, "");
        if (!o4bool(c, "ok", false)) return c;
        std::string cmd = shq(o4str(c, "path", ""));
        for (auto& arg : o4strs(opts, "args")) cmd += " " + shq(arg);
        int tmo = static_cast<int>(std::max(1.0, std::min(300.0, o4num(opts, "timeout", 10))));
        ExecResult r = runShell(cmd, o4str(opts, "stdin", ""), tmo);
        return M4({{"ok", B4(r.exitCode == 0)}, {"exitCode", N4(r.exitCode)}, {"stdout", S4(r.out)}, {"stderr", S4(r.err)},
                   {"ms", N4(std::floor(r.ms))}, {"timedOut", B4(r.timedOut)}, {"cached", o4get(c, "cached") ? *o4get(c, "cached") : B4(false)}});
    };
    natives["cpp.eval"] = [offErr](Args& a, int line) -> Value {
        need("cpp.eval", a, 1, 2, line);
        if (!nativeAllowed()) return offErr();
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        std::string expr = str(a[0], "cpp.eval", line);
        std::string src = "#include <iostream>\n#include <iomanip>\n#include <cmath>\n#include <string>\n#include <vector>\n#include <map>\n"
                          "#include <algorithm>\n#include <numeric>\n#include <sstream>\n#include <cstdint>\n";
        for (auto& h : o4strs(opts, "headers")) src += "#include <" + h + ">\n";
        src += "using namespace std;\nint main() {\n    auto rin_result = (" + expr + ");\n    cout << setprecision(17) << rin_result;\n    return 0;\n}\n";
        Value c = compileSource(src, opts, false, "");
        if (!o4bool(c, "ok", false)) return c;
        ExecResult r = runShell(shq(o4str(c, "path", "")), "", 10);
        Value val = S4(r.out);
        char* end = nullptr;
        double d = std::strtod(r.out.c_str(), &end);
        if (!r.out.empty() && end && *end == '\0') val = N4(d);
        return M4({{"ok", B4(r.exitCode == 0)}, {"value", val}, {"output", S4(r.out)}, {"stderr", S4(r.err)}});
    };
    natives["cpp.load"] = [offErr](Args& a, int line) -> Value {
        need("cpp.load", a, 1, 1, line);
        if (!nativeAllowed()) return offErr();
        void* h = ::dlopen(str(a[0], "cpp.load", line).c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) { const char* e = ::dlerror(); return M4({{"ok", B4(false)}, {"error", S4(e ? e : "dlopen failed")}}); }
        int id = ++libCounter();
        libTable()[id] = h;
        return M4({{"ok", B4(true)}, {"handle", N4(id)}});
    };
    natives["cpp.lib"] = [this, offErr](Args& a, int line) -> Value {   // compile + load
        need("cpp.lib", a, 1, 2, line);
        if (!nativeAllowed()) return offErr();
        Value opts = a.size() > 1 ? a[1] : Value::nil();
        Value c = compileSource(str(a[0], "cpp.lib", line), opts, true, "");
        if (!o4bool(c, "ok", false)) return c;
        std::vector<Value> la{S4(o4str(c, "path", ""))};
        Value l = natives["cpp.load"](la, line);
        if (!o4bool(l, "ok", false)) return l;
        return M4({{"ok", B4(true)}, {"handle", *o4get(l, "handle")}, {"path", S4(o4str(c, "path", ""))},
                   {"cached", o4get(c, "cached") ? *o4get(c, "cached") : B4(false)}, {"ms", o4get(c, "ms") ? *o4get(c, "ms") : N4(0)}});
    };
    natives["cpp.has"] = [](Args& a, int line) -> Value {
        need("cpp.has", a, 2, 2, line);
        auto it = libTable().find(static_cast<int>(num(a[0], "cpp.has", line)));
        if (it == libTable().end()) return B4(false);
        return B4(::dlsym(it->second, str(a[1], "cpp.has", line).c_str()) != nullptr);
    };
    natives["cpp.call"] = [offErr](Args& a, int line) -> Value {   // دالة JSON ABI: const char* f(const char* argsJson)
        need("cpp.call", a, 2, 3, line);
        if (!nativeAllowed()) return offErr();
        auto it = libTable().find(static_cast<int>(num(a[0], "cpp.call", line)));
        if (it == libTable().end()) return M4({{"ok", B4(false)}, {"error", S4("invalid library handle")}});
        std::string fname = str(a[1], "cpp.call", line);
        void* sym = ::dlsym(it->second, fname.c_str());
        if (!sym) return M4({{"ok", B4(false)}, {"error", S4("symbol not found: " + fname)}});
        Value args = a.size() > 2 ? a[2] : newArray();
        if (args.type != Value::Type::ARRAY) args = A4({args});
        std::string in = stringifyJson(args, 0, false);
        const char* res = reinterpret_cast<const char* (*)(const char*)>(sym)(in.c_str());
        std::string raw = res ? res : "null";
        Value v; std::string err;
        if (!decodeStrict(raw, v, err)) v = S4(raw);
        return M4({{"ok", B4(true)}, {"value", v}, {"raw", S4(raw)}});
    };
    natives["cpp.callNum"] = [offErr](Args& a, int line) -> Value {   // double f(double,...) حتى 6 وسائط؛ kind:"int" ← int64
        need("cpp.callNum", a, 3, 4, line);
        if (!nativeAllowed()) return offErr();
        auto it = libTable().find(static_cast<int>(num(a[0], "cpp.callNum", line)));
        if (it == libTable().end()) return M4({{"ok", B4(false)}, {"error", S4("invalid library handle")}});
        std::string fname = str(a[1], "cpp.callNum", line);
        void* sym = ::dlsym(it->second, fname.c_str());
        if (!sym) return M4({{"ok", B4(false)}, {"error", S4("symbol not found: " + fname)}});
        const ArrayData& xs = arr(a[2], "cpp.callNum", line);
        if (xs.size() > 6) throw diagErr(diag::Code::E0007_InvalidArguments, line, "cpp.callNum supports up to 6 arguments");
        bool isInt = a.size() > 3 && a[3].type == Value::Type::STRING && a[3].str == "int";
        double d[6] = {0, 0, 0, 0, 0, 0};
        long long n[6] = {0, 0, 0, 0, 0, 0};
        for (size_t i = 0; i < xs.size(); ++i) { double v = num(xs[i], "cpp.callNum", line); d[i] = v; n[i] = static_cast<long long>(v); }
        if (isInt) {
            using L = long long;
            L r = 0;
            switch (xs.size()) {
                case 0: r = reinterpret_cast<L (*)()>(sym)(); break;
                case 1: r = reinterpret_cast<L (*)(L)>(sym)(n[0]); break;
                case 2: r = reinterpret_cast<L (*)(L, L)>(sym)(n[0], n[1]); break;
                case 3: r = reinterpret_cast<L (*)(L, L, L)>(sym)(n[0], n[1], n[2]); break;
                case 4: r = reinterpret_cast<L (*)(L, L, L, L)>(sym)(n[0], n[1], n[2], n[3]); break;
                case 5: r = reinterpret_cast<L (*)(L, L, L, L, L)>(sym)(n[0], n[1], n[2], n[3], n[4]); break;
                default: r = reinterpret_cast<L (*)(L, L, L, L, L, L)>(sym)(n[0], n[1], n[2], n[3], n[4], n[5]); break;
            }
            return M4({{"ok", B4(true)}, {"value", N4(static_cast<double>(r))}});
        }
        double r = 0;
        switch (xs.size()) {
            case 0: r = reinterpret_cast<double (*)()>(sym)(); break;
            case 1: r = reinterpret_cast<double (*)(double)>(sym)(d[0]); break;
            case 2: r = reinterpret_cast<double (*)(double, double)>(sym)(d[0], d[1]); break;
            case 3: r = reinterpret_cast<double (*)(double, double, double)>(sym)(d[0], d[1], d[2]); break;
            case 4: r = reinterpret_cast<double (*)(double, double, double, double)>(sym)(d[0], d[1], d[2], d[3]); break;
            case 5: r = reinterpret_cast<double (*)(double, double, double, double, double)>(sym)(d[0], d[1], d[2], d[3], d[4]); break;
            default: r = reinterpret_cast<double (*)(double, double, double, double, double, double)>(sym)(d[0], d[1], d[2], d[3], d[4], d[5]); break;
        }
        return M4({{"ok", B4(true)}, {"value", N4(r)}});
    };
    natives["cpp.unload"] = [](Args& a, int line) -> Value {
        need("cpp.unload", a, 1, 1, line);
        int id = static_cast<int>(num(a[0], "cpp.unload", line));
        auto it = libTable().find(id);
        if (it == libTable().end()) return B4(false);
        ::dlclose(it->second);
        libTable().erase(it);
        return B4(true);
    };
    natives["cpp.exec"] = [this](Args& a, int line) -> Value {   // compile + load + call + unload في خطوة واحدة
        need("cpp.exec", a, 2, 4, line);
        Value opts = a.size() > 3 ? a[3] : Value::nil();
        std::vector<Value> la{a[0], opts};
        Value l = natives["cpp.lib"](la, line);
        if (!o4bool(l, "ok", false)) return l;
        std::vector<Value> ca{*o4get(l, "handle"), a[1], a.size() > 2 ? a[2] : newArray()};
        Value r = natives["cpp.call"](ca, line);
        std::vector<Value> ua{*o4get(l, "handle")};
        natives["cpp.unload"](ua, line);
        return r;
    };
#else
    auto unsupported = [](const char* fn) {
        std::string f = fn;
        return [f](Args&, int) -> Value { return M4({{"ok", B4(false)}, {"error", S4(f + ": C++ bridge is not available on this platform (no compiler/dlopen)")}}); };
    };
    for (const char* fn : {"cpp.compile", "cpp.run", "cpp.eval", "cpp.load", "cpp.lib", "cpp.call", "cpp.callNum", "cpp.exec"})
        natives[fn] = unsupported(fn);
    natives["cpp.has"] = [](Args&, int) -> Value { return B4(false); };
    natives["cpp.unload"] = [](Args&, int) -> Value { return B4(false); };
#endif
}

} // namespace rin
