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
//
//  ── حرّاس التأكيد (Guard) = دمج  dialog  مع  Intent/Navigation ─────────────────
//  Intent.guard(c, action|"*", dialog [, {confirm, when}])   كل Intent.send لهذا الإجراء يمرّ بحوار تأكيد
//  Intent.confirm(intent, dialog [, {confirm, when}])         تأكيد لمرة واحدة ثم تنفيذ
//  Intent.unguard(c [, action]) · Intent.guards(c)            إزالة / استعلام (مع عدّادات asked/confirmed/declined)
//  Navigation.guard(c, dialog [, {confirm, when, on}])        كل re/replace/back/reload تمرّ بحوار («تغييرات غير محفوظة»)
//  Navigation.confirm(c, route, dialog [, {confirm, when, params}]) · Navigation.unguard(c) · Navigation.guardInfo(c)
//  Intent.pause/resume(c [, action]) · Navigation.pause/resume(c)   تعليق الحارس مؤقتاً / استئنافه
//
//  ── جبر النيّة (Intent algebra): عمليات على extras تعيد نيّة جديدة (الأصل لا يتغيّر) ──
//  Intent.add(i,k,v) (يضيف مفتاحاً جديداً) · Intent.remove/delete(i,k) · Intent.hide(i,k) / Intent.unhide|reveal(i [,k]) / Intent.hidden(i)
//  Intent.sum/sub/mul/div/mod/pow/min/max(i,k,n [,default]) · Intent.clamp(i,k,lo,hi [,default]) · Intent.total(i [,keys])
//  الشرح: docs/intent_navigation.md و docs/guarded_actions.md و docs/intent_algebra.md
// ============================================================================
#include "rin_interpreter.h"
#include "indsin/rin_indsin_navigation.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

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

// ---- حرّاس التأكيد: حوار `dialog` معلن داخل الحاوية يقرّر هل يُنفَّذ الإجراء/الانتقال ----
struct Guard {
    std::string dialog;            // اسم الحوار (حقل حيّ في الحاوية نفسها)
    Value confirm = Value::num(1); // زر التأكيد: رقم (1..) أو نصه؛ أي اختيار آخر (أو التجاهل) = رفض
    std::string when;              // اختياري: حقل أو دالة في الحاوية؛ الحارس لا يسأل إلا إن كانت قيمتها صادقة
    std::set<std::string> on;      // للتنقّل: أي العمليات محروسة (re/replace/back/reload)؛ فارغ = الكل
    bool paused = false;           // معلَّق: يبقى مسجَّلاً لكنه لا يسأل (Intent.pause / Navigation.pause)
    long asked = 0, confirmed = 0, declined = 0;
};
static std::map<std::string, std::map<std::string, Guard>>& intentGuards() { static std::map<std::string, std::map<std::string, Guard>> g; return g; }
static std::map<std::string, Guard>& navGuards() { static std::map<std::string, Guard> g; return g; }
static std::set<std::string>& guardBusy() { static std::set<std::string> b; return b; }   // يمنع حلقة: reply يستدعي ما هو محروس

struct Verdict { bool asked = false, proceed = true; long choice = 0; Value answer; };

// ---- جبر النيّة: extras المخفيّة تبقى في النيّة لكنها لا تُمرَّر للإجراء ولا تظهر في pending ----
static std::vector<std::string> hiddenKeys(const Value& intent) {
    std::vector<std::string> out;
    Value h = mapGet(intent, "hidden");
    if (h.type == Value::Type::ARRAY && h.array) for (auto& e : *h.array) out.push_back(e.toDisplayString());
    return out;
}
static Value visibleExtras(const Value& intent) {
    Value ex = mapGet(intent, "extras");
    auto hid = hiddenKeys(intent);
    if (hid.empty() || ex.type != Value::Type::MAP || !ex.map) return ex;
    auto m = std::make_shared<MapData>();
    for (auto& kv : *ex.map) if (std::find(hid.begin(), hid.end(), kv.first.toDisplayString()) == hid.end()) m->push_back(kv);
    return Value::makeMap(m);
}

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

    // ======================= حرّاس التأكيد: dialog + Intent/Navigation =======================
    // حل زر التأكيد (رقم أو نص) إلى رقم زر فعلي في الحوار؛ يرمي إن لم يوجد
    auto resolveConfirm = [this](const Value& cf, const std::string& c, const std::string& dlg, int line) -> long {
        Environment* o = nullptr; std::shared_ptr<LiveMeta> m;
        dialogLocateIn(c, dlg, line, o, m);
        DialogSpec sp = dialogSpec(dlg, m, line);
        long n = 0;
        if (cf.type == Value::Type::NUMBER) { n = static_cast<long>(cf.number); if (cf.number != static_cast<double>(n)) n = 0; }
        else if (cf.type == Value::Type::STRING) { for (size_t i = 0; i < sp.buttons.size(); ++i) if (sp.buttons[i] == cf.str) { n = static_cast<long>(i + 1); break; } }
        if (n < 1 || n > static_cast<long>(sp.buttons.size())) {
            std::string j; for (auto& b : sp.buttons) j += (j.empty() ? "" : " | ") + b;
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "guard: confirm button " + cf.toDisplayString() + " does not exist in dialog '" + dlg + "'",
                                "dialog '" + dlg + "' has the buttons: " + j);
        }
        return n;
    };
    // يقرأ {confirm, when, on, params} ويتحقّق منها ويبني Guard
    auto parseGuard = [this, resolveConfirm](const std::string& fn, const std::string& c, const Value& dlg, const Value& opts,
                                             std::vector<std::string> allowed, int line) -> Guard {
        Guard g;
        g.dialog = needStr(dlg, fn, line);
        Environment* o = nullptr; std::shared_ptr<LiveMeta> m;
        dialogLocateIn(c, g.dialog, line, o, m);                       // E0046 إن لم يكن حواراً في هذه الحاوية
        if (opts.type != Value::Type::NIL) {
            if (opts.type != Value::Type::MAP || !opts.map) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": options must be a map but got " + opts.typeName());
            for (auto& kv : *opts.map) {
                std::string k = kv.first.toDisplayString();
                if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) {
                    std::string j; for (auto& x : allowed) j += (j.empty() ? "" : ", ") + x;
                    throw errWithReason(diag::Code::E0007_InvalidArguments, line, fn + ": unknown option '" + k + "'", "allowed options: " + j);
                }
            }
            if (const Value* v = opts.map->findValue(Value::string("confirm"))) g.confirm = *v;
            if (const Value* v = opts.map->findValue(Value::string("when"))) {
                g.when = needStr(*v, fn + " (when)", line);
                if (!containers[c]->values.count(g.when) && !containers[c]->live.count(g.when))
                    throw errWithReason(diag::Code::E0007_InvalidArguments, line, fn + ": 'when' names '" + g.when + "' which is not a field or function of container '" + c + "'",
                                        "declare it inside the container (a `let` field or a `fun`)");
            }
            if (const Value* v = opts.map->findValue(Value::string("on"))) {
                if (v->type != Value::Type::ARRAY || !v->array) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": 'on' must be an array of operations but got " + v->typeName());
                for (auto& e : *v->array) {
                    std::string op = e.toDisplayString();
                    if (op != "re" && op != "replace" && op != "back" && op != "reload")
                        throw errWithReason(diag::Code::E0007_InvalidArguments, line, fn + ": unknown navigation operation '" + op + "'", "use: re, replace, back, reload");
                    g.on.insert(op);
                }
            }
        }
        resolveConfirm(g.confirm, c, g.dialog, line);                  // تحقّق مبكر: الزر موجود
        return g;
    };
    // يسأل المستخدم عبر الحوار. relookup يعيد الحارس المسجَّل (لتحديث العدّادات) أو null لحارس لمرة واحدة.
    auto consult = [this, resolveConfirm](const std::string& c, const Guard& g, const std::string& busyKey, const char* kind, const char* op,
                                          const std::string& what, const Value& payload, std::function<Guard*()> relookup, int line) -> Verdict {
        Verdict v;
        if (guardBusy().count(busyKey)) return v;                      // داخل reply/دالة محروسة: لا نكرّر السؤال
        if (!g.when.empty()) {
            Args ga{Value::string(c), Value::string(g.when)};
            Value w = natives["getField"](ga, line);
            if (w.type == Value::Type::FUNCTION && w.function) { Args none; w = callFunction(w.function, none, line); }
            if (!w.isTruthy()) return v;                               // الشرط كاذب: لا حاجة للتأكيد
        }
        Environment* o = nullptr; std::shared_ptr<LiveMeta> m;
        dialogLocateIn(c, g.dialog, line, o, m);
        long want = resolveConfirm(g.confirm, c, g.dialog, line);
        // إن عرّفت الحاوية حقلاً باسم pending فسيحمل وصف ما ينتظر التأكيد؛ فتتحدّث رسالة الحوار (val) به تلقائياً
        bool hasPending = containers[c]->values.count("pending") > 0;
        if (hasPending) {
            Args pa{Value::string(c), Value::string("pending"),
                    mkMap({{"kind", Value::string(kind)}, {"op", Value::string(op)}, {"target", Value::string(what)}, {"data", payload}, {"dialog", Value::string(g.dialog)}})};
            natives["setState"](pa, line);
        }
        auto clearPending = [&]() { if (hasPending) { try { Args pa{Value::string(c), Value::string("pending"), Value::nil()}; natives["setState"](pa, line); } catch (...) {} } };
        guardBusy().insert(busyKey);
        Value ans;
        try { ans = dialogDoAsk(o, g.dialog, m, line); }
        catch (...) { guardBusy().erase(busyKey); clearPending(); throw; }
        guardBusy().erase(busyKey);
        clearPending();
        v.asked = true; v.choice = m->dChoice; v.answer = ans; v.proceed = (v.choice == want);   // التجاهل (0) = رفض: الفشل مغلق
        if (relookup) if (Guard* rg = relookup()) { rg->asked++; (v.proceed ? rg->confirmed : rg->declined)++; }
        return v;
    };
    auto blockedMap = [](const char* kind, const std::string& c, const std::string& what, const Verdict& v) -> Value {
        return mkMap({{"ok", Value::boolean_(false)}, {"blocked", Value::boolean_(true)}, {"kind", Value::string(kind)},
                      {"container", Value::string(c)}, {"target", Value::string(what)},
                      {"choice", Value::num(static_cast<double>(v.choice))}, {"answer", v.answer}});
    };
    auto guardInfo = [](const std::string& key, const Guard& g) -> Value {
        auto on = std::make_shared<ArrayData>(); for (auto& o : g.on) on->push_back(Value::string(o));
        return mkMap({{"target", Value::string(key)}, {"dialog", Value::string(g.dialog)}, {"confirm", g.confirm},
                      {"when", g.when.empty() ? Value::nil() : Value::string(g.when)}, {"on", Value::makeArray(on)},
                      {"asked", Value::num(static_cast<double>(g.asked))}, {"confirmed", Value::num(static_cast<double>(g.confirmed))},
                      {"declined", Value::num(static_cast<double>(g.declined))}, {"paused", Value::boolean_(g.paused)}});
    };
    // بوّابة التنقّل: true = مُنع الانتقال (v يحمل الحكم)
    auto navBlocked = [consult](const std::string& c, const char* op, const std::string& target, const Value& params, Verdict& v, int line) -> bool {
        auto it = navGuards().find(c);
        if (it == navGuards().end()) return false;
        if (it->second.paused || (!it->second.on.empty() && !it->second.on.count(op))) return false;
        Guard g = it->second;                                          // نسخة: قد يُزيل reply الحارس أثناء السؤال
        v = consult(c, g, "n|" + c, "navigation", op, target, params,
                    [c]() -> Guard* { auto x = navGuards().find(c); return x == navGuards().end() ? nullptr : &x->second; }, line);
        return v.asked && !v.proceed;
    };
    auto withConfirmed = [](Value r) -> Value {
        if (r.type == Value::Type::MAP && r.map) { auto m = std::make_shared<MapData>(*r.map); m->emplace_back(Value::string("confirmed"), Value::boolean_(true)); return Value::makeMap(m); }
        return r;
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
    // sendCore: يتحقّق من الإجراء ثم (اختيارياً) يمرّ بالبوّابة ثم ينفّذ. gate يعيد Verdict.
    auto sendCore = [this, needContainer, blockedMap, withConfirmed](const Value& intent, int line, const std::function<Verdict(const std::string&, const std::string&)>& gate) -> Value {
        Value cv = mapGet(intent, "container"), av = mapGet(intent, "action");
        if (mapGet(intent, "kind").type != Value::Type::STRING || cv.type != Value::Type::STRING || av.type != Value::Type::STRING)
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
        auto fn = own->second.function;
        Verdict v;
        if (gate) { v = gate(c, av.str); if (v.asked && !v.proceed) return blockedMap("intent", c, av.str, v); }
        Args args;
        if (fn->declaration && !fn->declaration->params.empty()) args.push_back(visibleExtras(intent));
        Value r = callFunction(fn, args, line);
        Value out = mkMap({{"ok", Value::boolean_(true)}, {"container", cv}, {"action", av}, {"result", r}});
        return v.asked ? withConfirmed(out) : out;
    };
    natives["Intent.send"] = [this, sendCore, consult](Args& a, int line) -> Value {
        argc("Intent.send", a, 1, 2, line);
        bool force = false;
        if (a.size() > 1 && a[1].type != Value::Type::NIL) {
            if (a[1].type != Value::Type::MAP || !a[1].map) throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.send: options must be a map but got " + a[1].typeName());
            for (auto& kv : *a[1].map) if (kv.first.toDisplayString() != "force")
                throw errWithReason(diag::Code::E0007_InvalidArguments, line, "Intent.send: unknown option '" + kv.first.toDisplayString() + "'", "allowed options: force");
            force = mapGet(a[1], "force").isTruthy();                  // {force: true} = يتجاوز الحارس (استدعاء نظام لا مستخدم)
        }
        Value extras = visibleExtras(a[0]);
        std::function<Verdict(const std::string&, const std::string&)> gate;
        if (!force) gate = [consult, extras, line](const std::string& c, const std::string& act) -> Verdict {
            auto ci = intentGuards().find(c);
            if (ci == intentGuards().end()) return Verdict{};
            std::string key = ci->second.count(act) ? act : (ci->second.count("*") ? "*" : "");
            if (key.empty()) return Verdict{};
            Guard g = ci->second[key];
            if (g.paused) return Verdict{};
            return consult(c, g, "i|" + c + "|" + act, "intent", "send", act, extras,
                           [c, key]() -> Guard* { auto x = intentGuards().find(c); if (x == intentGuards().end()) return nullptr; auto y = x->second.find(key); return y == x->second.end() ? nullptr : &y->second; }, line);
        };
        return sendCore(a[0], line, gate);
    };
    // ---- Intent.confirm: تأكيد لمرة واحدة ثم تنفيذ (يحلّ محل أي حارس دائم لهذا الاستدعاء) ----
    natives["Intent.confirm"] = [this, needContainer, parseGuard, consult, sendCore](Args& a, int line) -> Value {
        argc("Intent.confirm", a, 2, 3, line);
        Value cv = mapGet(a[0], "container");
        if (mapGet(a[0], "kind").type != Value::Type::STRING || cv.type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.confirm: first argument is not an intent (build it with Intent.core)");
        std::string c = needContainer(cv, "Intent.confirm", line);
        Guard g = parseGuard("Intent.confirm", c, a[1], a.size() > 2 ? a[2] : Value::nil(), {"confirm", "when"}, line);
        Value extras = visibleExtras(a[0]);
        return sendCore(a[0], line, [consult, g, extras, line](const std::string& cc, const std::string& act) -> Verdict {
            return consult(cc, g, "i|" + cc + "|" + act, "intent", "confirm", act, extras, nullptr, line);
        });
    };
    natives["Intent.guard"] = [this, needContainer, parseGuard](Args& a, int line) -> Value {
        argc("Intent.guard", a, 3, 4, line);
        std::string c = needContainer(a[0], "Intent.guard", line);
        std::string act = needStr(a[1], "Intent.guard", line);
        if (act.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Intent.guard: action must not be empty (use \"*\" for every action)");
        if (act != "*") {
            auto own = containers[c]->values.find(act);
            if (own == containers[c]->values.end() || own->second.type != Value::Type::FUNCTION)
                throw errWithReason(diag::Code::E0006_UnknownFunction, line, "Intent.guard: '" + c + "' has no action '" + act + "'", "use Intent.actions(\"" + c + "\") to list them, or \"*\" for all");
        }
        intentGuards()[c][act] = parseGuard("Intent.guard", c, a[2], a.size() > 3 ? a[3] : Value::nil(), {"confirm", "when"}, line);
        return Value::boolean_(true);
    };
    natives["Intent.unguard"] = [needContainer](Args& a, int line) -> Value {
        argc("Intent.unguard", a, 1, 2, line);
        std::string c = needContainer(a[0], "Intent.unguard", line);
        auto ci = intentGuards().find(c);
        if (ci == intentGuards().end()) return Value::boolean_(false);
        if (a.size() == 1) { intentGuards().erase(ci); return Value::boolean_(true); }
        bool had = ci->second.erase(needStr(a[1], "Intent.unguard", line)) > 0;
        if (ci->second.empty()) intentGuards().erase(ci);
        return Value::boolean_(had);
    };
    natives["Intent.guards"] = [needContainer, guardInfo](Args& a, int line) -> Value {
        argc("Intent.guards", a, 1, 1, line);
        auto arr = std::make_shared<ArrayData>();
        auto ci = intentGuards().find(needContainer(a[0], "Intent.guards", line));
        if (ci != intentGuards().end()) for (auto& kv : ci->second) arr->push_back(guardInfo(kv.first, kv.second));
        return Value::makeArray(arr);
    };


    // ---- تعليق الحرّاس واستئنافها ----
    natives["Intent.pause"] = [needContainer](Args& a, int line) -> Value {
        argc("Intent.pause", a, 1, 2, line);
        auto ci = intentGuards().find(needContainer(a[0], "Intent.pause", line));
        if (ci == intentGuards().end()) return Value::num(0);
        double n = 0;
        for (auto& kv : ci->second) if (a.size() < 2 || kv.first == needStr(a[1], "Intent.pause", line)) { if (!kv.second.paused) n++; kv.second.paused = true; }
        return Value::num(n);                                          // عدد الحرّاس التي عُلِّقت الآن
    };
    natives["Intent.resume"] = [needContainer](Args& a, int line) -> Value {
        argc("Intent.resume", a, 1, 2, line);
        auto ci = intentGuards().find(needContainer(a[0], "Intent.resume", line));
        if (ci == intentGuards().end()) return Value::num(0);
        double n = 0;
        for (auto& kv : ci->second) if (a.size() < 2 || kv.first == needStr(a[1], "Intent.resume", line)) { if (kv.second.paused) n++; kv.second.paused = false; }
        return Value::num(n);
    };
    natives["Navigation.pause"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.pause", a, 1, 1, line);
        auto it = navGuards().find(needContainer(a[0], "Navigation.pause", line));
        if (it == navGuards().end()) return Value::boolean_(false);
        it->second.paused = true; return Value::boolean_(true);
    };
    natives["Navigation.resume"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.resume", a, 1, 1, line);
        auto it = navGuards().find(needContainer(a[0], "Navigation.resume", line));
        if (it == navGuards().end()) return Value::boolean_(false);
        it->second.paused = false; return Value::boolean_(true);
    };

    // ======================= جبر النيّة: عمليات على extras (تعيد نيّة جديدة) =======================
    // يبني نسخة من النيّة بعد تعديل extras (ex) و hidden (hid) عبر f
    auto editIntent = [this](const std::string& fn, const Value& intent, int line,
                             const std::function<void(MapData& ex, std::vector<std::string>& hid)>& f) -> Value {
        if (intent.type != Value::Type::MAP || !intent.map || mapGet(intent, "kind").type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": first argument is not an intent (build it with Intent.core)");
        auto ex = std::make_shared<MapData>();
        Value old = mapGet(intent, "extras");
        if (old.type == Value::Type::MAP && old.map) *ex = *old.map;
        std::vector<std::string> hid = hiddenKeys(intent);
        f(*ex, hid);
        auto out = std::make_shared<MapData>(*intent.map);
        auto setKey = [&](const char* k, Value v) { Value* sl = out->findValue(Value::string(k)); if (sl) *sl = v; else out->emplace_back(Value::string(k), v); };
        setKey("extras", Value::makeMap(ex));
        if (!hid.empty() || mapGet(intent, "hidden").type != Value::Type::NIL) {
            auto arr = std::make_shared<ArrayData>(); for (auto& h : hid) arr->push_back(Value::string(h));
            setKey("hidden", Value::makeArray(arr));
        }
        return Value::makeMap(out);
    };
    natives["Intent.add"] = [this, editIntent](Args& a, int line) -> Value {
        argc("Intent.add", a, 3, 3, line);
        std::string key = needStr(a[1], "Intent.add", line);
        return editIntent("Intent.add", a[0], line, [&](MapData& ex, std::vector<std::string>&) {
            if (ex.findValue(Value::string(key)))
                throw errWithReason(diag::Code::E0007_InvalidArguments, line, "Intent.add: extra '" + key + "' already exists", "use Intent.with to overwrite it, or Intent.remove first");
            ex.emplace_back(Value::string(key), a[2]);
        });
    };
    auto removeFn = [this, editIntent](Args& a, int line) -> Value {
        argc("Intent.remove", a, 2, 2, line);
        std::string key = needStr(a[1], "Intent.remove", line);
        return editIntent("Intent.remove", a[0], line, [&](MapData& ex, std::vector<std::string>& hid) {
            size_t i = ex.findIndex(Value::string(key));
            if (i == MapData::npos) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "Intent.remove: no extra named '" + key + "'", "nothing to remove");
            ex.erase(ex.begin() + static_cast<std::ptrdiff_t>(i));
            hid.erase(std::remove(hid.begin(), hid.end(), key), hid.end());
        });
    };
    natives["Intent.remove"] = removeFn;
    natives["Intent.delete"] = removeFn;
    natives["Intent.hide"] = [this, editIntent](Args& a, int line) -> Value {
        argc("Intent.hide", a, 2, 2, line);
        std::string key = needStr(a[1], "Intent.hide", line);
        return editIntent("Intent.hide", a[0], line, [&](MapData& ex, std::vector<std::string>& hid) {
            if (!ex.findValue(Value::string(key))) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "Intent.hide: no extra named '" + key + "'", "nothing to hide");
            if (std::find(hid.begin(), hid.end(), key) == hid.end()) hid.push_back(key);
        });
    };
    auto unhideFn = [this, editIntent](Args& a, int line) -> Value {
        argc("Intent.unhide", a, 1, 2, line);
        return editIntent("Intent.unhide", a[0], line, [&](MapData&, std::vector<std::string>& hid) {
            if (a.size() == 1) hid.clear();                            // بلا مفتاح: أظهر الكل
            else hid.erase(std::remove(hid.begin(), hid.end(), needStr(a[1], "Intent.unhide", line)), hid.end());
        });
    };
    natives["Intent.unhide"] = unhideFn;      // «show» كلمة محجوزة في Rin فلا تصحّ بعد النقطة
    natives["Intent.reveal"] = unhideFn;
    natives["Intent.hidden"] = [](Args& a, int line) -> Value {
        argc("Intent.hidden", a, 1, 1, line);
        return strArray(hiddenKeys(a[0]));
    };
    // عمليات حسابية: Intent.OP(i, key, n [, default]) — القيمة الحالية OP n؛ المفتاح المفقود يتطلب default
    auto arith = [this, editIntent](const std::string& op, std::function<double(double, double, int)> f) {
        std::string fn = "Intent." + op;
        natives[fn] = [this, editIntent, fn, f](Args& a, int line) -> Value {
            argc(fn, a, 3, 4, line);
            std::string key = needStr(a[1], fn, line);
            if (a[2].type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": the operand must be a number but got " + a[2].typeName());
            if (a.size() > 3 && a[3].type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": the default must be a number but got " + a[3].typeName());
            return editIntent(fn, a[0], line, [&](MapData& ex, std::vector<std::string>&) {
                Value* cur = ex.findValue(Value::string(key));
                double base;
                if (cur) {
                    if (cur->type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": extra '" + key + "' is " + cur->typeName() + ", not a number");
                    base = cur->number;
                } else if (a.size() > 3) base = a[3].number;
                else throw errWithReason(diag::Code::E0007_InvalidArguments, line, fn + ": no extra named '" + key + "'", "pass a default as the 4th argument, e.g. " + fn + "(i, \"" + key + "\", n, 0)");
                double r = f(base, a[2].number, line);
                if (cur) *cur = Value::num(r); else ex.emplace_back(Value::string(key), Value::num(r));
            });
        };
    };
    arith("sum", [](double x, double n, int) { return x + n; });
    arith("sub", [](double x, double n, int) { return x - n; });
    arith("mul", [](double x, double n, int) { return x * n; });
    arith("min", [](double x, double n, int) { return std::min(x, n); });
    arith("max", [](double x, double n, int) { return std::max(x, n); });
    arith("pow", [](double x, double n, int) { return std::pow(x, n); });
    arith("div", [this](double x, double n, int line) -> double {
        if (n == 0) throw errWithReason(diag::Code::E0035_RuntimeError, line, "division by zero", "Intent.div: the divisor is 0");
        return x / n; });
    arith("mod", [this](double x, double n, int line) -> double {
        if (n == 0) throw errWithReason(diag::Code::E0035_RuntimeError, line, "division by zero", "Intent.mod: the divisor is 0");
        return std::fmod(x, n); });
    // Intent.clamp(i, key, lo, hi [, default])
    natives["Intent.clamp"] = [this, editIntent](Args& a, int line) -> Value {
        argc("Intent.clamp", a, 4, 5, line);
        std::string key = needStr(a[1], "Intent.clamp", line);
        if (a[2].type != Value::Type::NUMBER || a[3].type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.clamp: lo and hi must be numbers");
        if (a[2].number > a[3].number) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Intent.clamp: lo must not exceed hi");
        return editIntent("Intent.clamp", a[0], line, [&](MapData& ex, std::vector<std::string>&) {
            Value* cur = ex.findValue(Value::string(key));
            double base;
            if (cur) { if (cur->type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, "Intent.clamp: extra '" + key + "' is " + cur->typeName() + ", not a number"); base = cur->number; }
            else if (a.size() > 4 && a[4].type == Value::Type::NUMBER) base = a[4].number;
            else throw errWithReason(diag::Code::E0007_InvalidArguments, line, "Intent.clamp: no extra named '" + key + "'", "pass a numeric default as the 5th argument");
            double r = std::min(std::max(base, a[2].number), a[3].number);
            if (cur) *cur = Value::num(r); else ex.emplace_back(Value::string(key), Value::num(r));
        });
    };
    // Intent.total(i [, keys]) — مجموع extras الرقمية الظاهرة (أو المفاتيح المسمّاة فقط)
    natives["Intent.total"] = [](Args& a, int line) -> Value {
        argc("Intent.total", a, 1, 2, line);
        Value ex = visibleExtras(a[0]);
        std::vector<std::string> only; bool filter = a.size() > 1 && a[1].type == Value::Type::ARRAY && a[1].array;
        if (filter) for (auto& e : *a[1].array) only.push_back(e.toDisplayString());
        double t = 0;
        if (ex.type == Value::Type::MAP && ex.map)
            for (auto& kv : *ex.map) if (kv.second.type == Value::Type::NUMBER && (!filter || std::find(only.begin(), only.end(), kv.first.toDisplayString()) != only.end())) t += kv.second.number;
        return Value::num(t);
    };

    // ---------------- Navigation ----------------
    natives["Navigation.re"] = [needContainer, setRoute, navBlocked, blockedMap, withConfirmed](Args& a, int line) -> Value {
        argc("Navigation.re", a, 2, 3, line);
        std::string c = needContainer(a[0], "Navigation.re", line);
        std::string route = needStr(a[1], "Navigation.re", line);
        if (route.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Navigation.re: route must not be empty");
        Value p = a.size() > 2 ? a[2] : Value::nil();
        if (p.type != Value::Type::NIL && p.type != Value::Type::MAP)
            throw diagErr(diag::Code::E0004_InvalidType, line, "Navigation.re: params must be a map but got " + p.typeName());
        Verdict gv;
        if (navBlocked(c, "re", route, p, gv, line)) return blockedMap("navigation", c, route, gv);
        NavState& st = navStates()[c];
        // navigate() يقصّ فرع «الأمام»؛ نقصّ params بالتوازي ليبقى الطولان متطابقين
        if (st.mgr.index + 1 < (int)st.mgr.history.size()) st.params.resize((size_t)st.mgr.index + 1);
        st.mgr.navigate(route);
        st.params.push_back(p.type == Value::Type::MAP ? p : emptyMap());
        setRoute(c, route, line);
        Value res = mkMap({{"ok", Value::boolean_(true)}, {"route", Value::string(route)}, {"index", Value::num((double)st.mgr.index)},
                           {"depth", Value::num((double)st.mgr.history.size())}, {"canBack", Value::boolean_(st.mgr.canGoBack())}});
        return gv.asked ? withConfirmed(res) : res;
    };
    natives["Navigation.replace"] = [needContainer, setRoute, navBlocked](Args& a, int line) -> Value {
        argc("Navigation.replace", a, 2, 3, line);
        std::string c = needContainer(a[0], "Navigation.replace", line);
        std::string route = needStr(a[1], "Navigation.replace", line);
        if (route.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "Navigation.replace: route must not be empty");
        Value p = (a.size() > 2 && a[2].type == Value::Type::MAP) ? a[2] : emptyMap();
        Verdict gv;
        if (navBlocked(c, "replace", route, p, gv, line)) return Value::nil();    // مُنع: لا تغيير
        NavState& st = navStates()[c];
        bool wasEmpty = st.mgr.index < 0;
        st.mgr.replace(route);
        if (wasEmpty) st.params.push_back(p); else st.params[(size_t)st.mgr.index] = p;
        setRoute(c, route, line);
        return Value::string(route);
    };
    natives["Navigation.back"] = [needContainer, setRoute, navBlocked](Args& a, int line) -> Value {
        argc("Navigation.back", a, 1, 1, line);
        std::string c = needContainer(a[0], "Navigation.back", line);
        NavState& st = navStates()[c];
        if (!st.mgr.canGoBack()) return Value::boolean_(false);   // بداية السجل: لا شيء يحدث (ولا سؤال)
        { Verdict gv; if (navBlocked(c, "back", st.mgr.history[(size_t)st.mgr.index - 1], emptyMap(), gv, line)) return Value::boolean_(false); }
        if (!st.mgr.back()) return Value::boolean_(false);
        setRoute(c, st.mgr.current(), line);
        return Value::boolean_(true);
    };
    natives["Navigation.reload"] = [needContainer, setRoute, navBlocked](Args& a, int line) -> Value {
        argc("Navigation.reload", a, 1, 1, line);
        std::string c = needContainer(a[0], "Navigation.reload", line);
        std::string now = navStates()[c].mgr.current();
        if (now.empty()) return Value::nil();
        { Verdict gv; if (navBlocked(c, "reload", now, emptyMap(), gv, line)) return Value::nil(); }
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

    // ---- حرّاس التنقّل ----
    natives["Navigation.guard"] = [needContainer, parseGuard](Args& a, int line) -> Value {
        argc("Navigation.guard", a, 2, 3, line);
        std::string c = needContainer(a[0], "Navigation.guard", line);
        navGuards()[c] = parseGuard("Navigation.guard", c, a[1], a.size() > 2 ? a[2] : Value::nil(), {"confirm", "when", "on"}, line);
        return Value::boolean_(true);
    };
    natives["Navigation.unguard"] = [needContainer](Args& a, int line) -> Value {
        argc("Navigation.unguard", a, 1, 1, line);
        return Value::boolean_(navGuards().erase(needContainer(a[0], "Navigation.unguard", line)) > 0);
    };
    natives["Navigation.guardInfo"] = [needContainer, guardInfo](Args& a, int line) -> Value {
        argc("Navigation.guardInfo", a, 1, 1, line);
        auto it = navGuards().find(needContainer(a[0], "Navigation.guardInfo", line));
        return it == navGuards().end() ? Value::nil() : guardInfo("navigation", it->second);
    };
    // Navigation.confirm(c, route, dialog [, {confirm, when, params}]): تأكيد لمرة واحدة ثم Navigation.re
    natives["Navigation.confirm"] = [this, needContainer, parseGuard, consult, blockedMap](Args& a, int line) -> Value {
        argc("Navigation.confirm", a, 3, 4, line);
        std::string c = needContainer(a[0], "Navigation.confirm", line);
        std::string route = needStr(a[1], "Navigation.confirm", line);
        Value opts = a.size() > 3 ? a[3] : Value::nil();
        Guard g = parseGuard("Navigation.confirm", c, a[2], opts, {"confirm", "when", "params"}, line);
        Value params = mapGet(opts, "params");
        Verdict v = consult(c, g, "n|" + c, "navigation", "re", route, params, nullptr, line);
        if (v.asked && !v.proceed) return blockedMap("navigation", c, route, v);
        bool mine = guardBusy().insert("n|" + c).second;                // الحارس الدائم لا يسأل مرة ثانية عن الانتقال نفسه
        Args ra{Value::string(c), Value::string(route)};
        if (params.type == Value::Type::MAP) ra.push_back(params);
        Value r;
        try { r = natives["Navigation.re"](ra, line); } catch (...) { if (mine) guardBusy().erase("n|" + c); throw; }
        if (mine) guardBusy().erase("n|" + c);
        if (v.asked && r.type == Value::Type::MAP && r.map) { auto m = std::make_shared<MapData>(*r.map); m->emplace_back(Value::string("confirmed"), Value::boolean_(true)); r = Value::makeMap(m); }
        return r;
    };
}

}  // namespace rin
