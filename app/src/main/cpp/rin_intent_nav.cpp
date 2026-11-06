// ============================================================================
//  rin_intent_nav.cpp — Intent.core و Navigation.re: مساحتا أسماء دوال للحاويات و indsin
// ----------------------------------------------------------------------------
//  Intent.core(container, action [, extras])  نيّة: خريطة {kind:"intent", container, action, extras, id}
//  Intent.send(intent)                        ينفّذ دالة الحاوية المسمّاة action (extras تُمرَّر إن كانت للدالة معاملات)
//  Intent.extra(intent, key [, default]) · Intent.with(intent, key, value) · Intent.valid(x) · Intent.actions(container)
//
//  Navigation.re(container, route [, params]) ينتقل إلى مسار (push) ويكتب الحقل الحيّ route في الحاوية
//  Navigation.back / replace / reload / current / history / canBack / params / clear   (container[, ...])
//  سجل التنقل لكل حاوية هو indsin::NavigationManager نفسه (rin_indsin_navigation.h)
//  فعند كتابة route تتحدّث أي خاصية في @view تقرؤه. تُضمَّن (#include) في نهاية rin_interpreter.cpp.
//  الشرح: docs/intent_navigation.md
// ============================================================================
#include "rin_interpreter.h"
#include "indsin/rin_indsin_navigation.h"
#include <algorithm>
#include <map>

namespace rin {
namespace intentnav {

using Args = std::vector<Value>;

static void argc(const std::string& fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi)
        throw diagErr(diag::Code::E0007_InvalidArguments, line,
                      "'" + fn + "' expects " + std::to_string(lo) + (hi != lo ? " to " + std::to_string(hi) : "") +
                      " argument(s) but got " + std::to_string(a.size()));
}
static std::string needStr(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::STRING)
        throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": expected a string but got " + v.typeName());
    return v.str;
}
static Value mapGet(const Value& m, const char* key) {
    if (m.type != Value::Type::MAP || !m.map) return Value::nil();
    const Value* v = m.map->findValue(Value::string(key));
    return v ? *v : Value::nil();
}
static Value mkMap(std::initializer_list<std::pair<const char*, Value>> kv) {
    auto m = std::make_shared<MapData>();
    for (auto& p : kv) m->emplace_back(Value::string(p.first), p.second);
    return Value::makeMap(m);
}
static Value strArray(const std::vector<std::string>& v) {
    auto arr = std::make_shared<ArrayData>();
    for (auto& s : v) arr->push_back(Value::string(s));
    return Value::makeArray(arr);
}
static Value emptyMap() { return Value::makeMap(std::make_shared<MapData>()); }

struct NavState { indsin::NavigationManager mgr; std::vector<Value> params; };
static std::map<std::string, NavState>& navStates() { static std::map<std::string, NavState> s; return s; }
static int& intentCounter() { static int n = 0; return n; }

}  // namespace intentnav

void Interpreter::registerNativesIntentNav() {
    using namespace intentnav;

    // اسم الحاوية الداخلي أو خطأ واضح إن لم توجد
    auto needContainer = [this](const Value& v, const std::string& fn, int line) -> std::string {
        std::string name = canonContainer(needStr(v, fn, line));
        if (containers.find(name) == containers.end())
            throw errWithReason(diag::Code::E0014_InvalidContainer, line, fn + ": '" + name + "' is not a known container",
                                "create it first via `@container=" + name + "`");
        return name;
    };
    auto setRoute = [this](const std::string& c, const std::string& route, int line) {
        Args a{Value::string(c), Value::string("route"), Value::string(route)};
        natives["setState"](a, line);
    };

    // ---------------- Intent ----------------
    natives["Intent.core"] = [needContainer](Args& a, int line) -> Value {
        argc("Intent.core", a, 2, 3, line);
        std::string c = needContainer(a[0], "Intent.core", line);
        std::string act = needStr(a[1], "Intent.core", line);
        if (act.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Intent.core: action must not be empty");
        Value extras = emptyMap();
        if (a.size() > 2 && a[2].type != Value::Type::NIL) {
            if (a[2].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.core: extras must be a map but got " + a[2].typeName());
            extras = a[2];
        }
        return mkMap({{"kind", Value::string("intent")}, {"id", Value::num((double)++intentCounter())},
                      {"container", Value::string(c)}, {"action", Value::string(act)}, {"extras", extras}});
    };
    natives["Intent.valid"] = [this](Args& a, int line) -> Value {
        argc("Intent.valid", a, 1, 1, line);
        Value k = mapGet(a[0], "kind"), c = mapGet(a[0], "container"), ac = mapGet(a[0], "action");
        bool ok = k.type == Value::Type::STRING && k.str == "intent" && c.type == Value::Type::STRING &&
                  ac.type == Value::Type::STRING && containers.count(c.str) > 0;
        return Value::boolean_(ok);
    };
    natives["Intent.extra"] = [](Args& a, int line) -> Value {
        argc("Intent.extra", a, 2, 3, line);
        Value ex = mapGet(a[0], "extras");
        const Value* v = (ex.type == Value::Type::MAP && ex.map) ? ex.map->findValue(Value::string(needStr(a[1], "Intent.extra", line))) : nullptr;
        if (v) return *v;
        return a.size() > 2 ? a[2] : Value::nil();
    };
    natives["Intent.with"] = [](Args& a, int line) -> Value {
        argc("Intent.with", a, 3, 3, line);
        if (mapGet(a[0], "kind").type != Value::Type::STRING) throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.with: first argument is not an intent");
        auto ex = std::make_shared<MapData>();
        Value old = mapGet(a[0], "extras");
        if (old.type == Value::Type::MAP && old.map) *ex = *old.map;
        std::string key = needStr(a[1], "Intent.with", line);
        Value* slot = ex->findValue(Value::string(key));
        if (slot) *slot = a[2]; else ex->emplace_back(Value::string(key), a[2]);
        auto out = std::make_shared<MapData>(*a[0].map);
        Value* es = out->findValue(Value::string("extras"));
        if (es) *es = Value::makeMap(ex); else out->emplace_back(Value::string("extras"), Value::makeMap(ex));
        return Value::makeMap(out);
    };
    natives["Intent.actions"] = [this, needContainer](Args& a, int line) -> Value {
        argc("Intent.actions", a, 1, 1, line);
        std::string c = needContainer(a[0], "Intent.actions", line);
        std::vector<std::string> names;
        for (auto& kv : containers[c]->values) if (kv.second.type == Value::Type::FUNCTION) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return strArray(names);
    };
    natives["Intent.send"] = [this, needContainer](Args& a, int line) -> Value {
        argc("Intent.send", a, 1, 1, line);
        Value cv = mapGet(a[0], "container"), av = mapGet(a[0], "action");
        if (mapGet(a[0], "kind").type != Value::Type::STRING || cv.type != Value::Type::STRING || av.type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.send: argument is not an intent (build it with Intent.core)");
        std::string c = needContainer(cv, "Intent.send", line);
        auto own = containers[c]->values.find(av.str);
        if (own == containers[c]->values.end() || own->second.type != Value::Type::FUNCTION || !own->second.function) {
            std::vector<std::string> names;
            for (auto& kv : containers[c]->values) if (kv.second.type == Value::Type::FUNCTION) names.push_back(kv.first);
            std::sort(names.begin(), names.end());
            std::string list; for (auto& n : names) list += (list.empty() ? "" : ", ") + n;
            throw errWithReason(diag::Code::E0006_UnknownFunction, line, "Intent.send: '" + c + "' has no action '" + av.str + "'",
                                names.empty() ? "the container declares no functions" : "available actions: " + list);
        }
        Args args;
        if (own->second.function->declaration && !own->second.function->declaration->params.empty())
            args.push_back(mapGet(a[0], "extras"));
        Value r = callFunction(own->second.function, args, line);
        return mkMap({{"ok", Value::boolean_(true)}, {"container", cv}, {"action", av}, {"result", r}});
    };

    // ---------------- Navigation ----------------
    natives["Navigation.re"] = [needContainer, setRoute](Args& a, int line) -> Value {
        argc("Navigation.re", a, 2, 3, line);
        std::string c = needContainer(a[0], "Navigation.re", line);
        std::string route = needStr(a[1], "Navigation.re", line);
        if (route.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Navigation.re: route must not be empty");
        Value p = a.size() > 2 ? a[2] : Value::nil();
        if (p.type != Value::Type::NIL && p.type != Value::Type::MAP)
            throw diagErr(diag::Code::E0004_InvalidType, line, "Navigation.re: params must be a map but got " + p.typeName());
        NavState& st = navStates()[c];
        // navigate() يقصّ فرع «الأمام»؛ نقصّ params بالتوازي ليبقى الطولان متطابقين
        if (st.mgr.index + 1 < (int)st.mgr.history.size()) st.params.resize((size_t)st.mgr.index + 1);
        st.mgr.navigate(route);
        st.params.push_back(p.type == Value::Type::MAP ? p : emptyMap());
        setRoute(c, route, line);
        return mkMap({{"ok", Value::boolean_(true)}, {"route", Value::string(route)}, {"index", Value::num((double)st.mgr.index)},
                      {"depth", Value::num((double)st.mgr.history.size())}, {"canBack", Value::boolean_(st.mgr.canGoBack())}});
    };
    natives["Navigation.replace"] = [needContainer, setRoute](Args& a, int line) -> Value {
        argc("Navigation.replace", a, 2, 3, line);
        std::string c = needContainer(a[0], "Navigation.replace", line);
        std::string route = needStr(a[1], "Navigation.replace", line);
        if (route.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Navigation.replace: route must not be empty");
        NavState& st = navStates()[c];
        Value p = (a.size() > 2 && a[2].type == Value::Type::MAP) ? a[2] : emptyMap();
        bool wasEmpty = st.mgr.index < 0;
        st.mgr.replace(route);
        if (wasEmpty) st.params.push_back(p); else st.params[(size_t)st.mgr.index] = p;
        setRoute(c, route, line);
        return Value::string(route);
    };
    natives["Navigation.back"] = [needContainer, setRoute](Args& a, int line) -> Value {
        argc("Navigation.back", a, 1, 1, line);
        std::string c = needContainer(a[0], "Navigation.back", line);
        NavState& st = navStates()[c];
        if (!st.mgr.back()) return Value::boolean_(false);   // بداية السجل: لا شيء يحدث
        setRoute(c, st.mgr.current(), line);
        return Value::boolean_(true);
    };
    natives["Navigation.reload"] = [needContainer, setRoute](Args& a, int line) -> Value {
        argc("Navigation.reload", a, 1, 1, line);
        std::string c = needContainer(a[0], "Navigation.reload", line);
        std::string cur = navStates()[c].mgr.reload();
        if (cur.empty()) return Value::nil();
        setRoute(c, cur, line);
        return Value::string(cur);
    };
    natives["Navigation.current"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.current", a, 1, 1, line);
        std::string cur = navStates()[needContainer(a[0], "Navigation.current", line)].mgr.current();
        return cur.empty() ? Value::nil() : Value::string(cur);
    };
    natives["Navigation.history"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.history", a, 1, 1, line);
        return strArray(navStates()[needContainer(a[0], "Navigation.history", line)].mgr.history);
    };
    natives["Navigation.canBack"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.canBack", a, 1, 1, line);
        return Value::boolean_(navStates()[needContainer(a[0], "Navigation.canBack", line)].mgr.canGoBack());
    };
    natives["Navigation.params"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.params", a, 1, 1, line);
        NavState& st = navStates()[needContainer(a[0], "Navigation.params", line)];
        if (st.mgr.index < 0 || (size_t)st.mgr.index >= st.params.size()) return emptyMap();
        return st.params[(size_t)st.mgr.index];
    };
    natives["Navigation.clear"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.clear", a, 1, 1, line);
        navStates().erase(needContainer(a[0], "Navigation.clear", line));
        return Value::boolean_(true);
    };
}

}  // namespace rin
