// ============================================================================
//  rin_container_services.cpp — Services: حاوية الخدمات (Service Container): تسجيل الخدمات وبناؤها كسولاً وحقن تبعياتها
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp بعد rin_container_blend.cpp ويُسجَّل عبر
//  Interpreter::registerNativesServices(). التوثيق: docs/container_services.md
//
//  تكمّل مفهوم container (انظر .changelog/CHANGELOG_container_services.md).
//  الحاوية في Rin تحمل «سلوكاً وحالة»؛ وحاوية الخدمات تحمل «طريقة بناء
//  الأشياء وربطها ببعض». هنا الاثنان يتكاملان: خدمة يمكن أن تكون لقطة حيّة من أي @container.
//
//    Services.bind(name, factory)           خدمة عابرة: يُنفَّذ المصنع عند كل get/make
//    Services.singleton(name, factory)      خدمة مشتركة: تُبنى كسولاً مرة واحدة ثم تُخزَّن
//    Services.instance(name, value)         قيمة جاهزة
//    Services.alias(alias, name)            اسم بديل (سلاسل بدائل مسموحة، والدوران مرفوض)
//    Services.fromContainer(name, container?)   خدمة = لقطة (snapshot) حيّة من @container
//    Services.has(name)                     هل الخدمة مسجَّلة؟ (تتبع البدائل)
//    Services.get(name)                     يبني الخدمة أو يعيد المخزَّنة (يرمي خطأً واضحاً + اقتراح اسم قريب إن غابت)
//    Services.make(name, params?)           مثل get مع قيم صريحة تغلب الحقن التلقائي
//    Services.call(fn, params?)             ينادي أي دالة ويحقن معاملاتها بأسمائها من الخدمات
//    Services.extend(name, fn)              مُزخرِف (decorator) يعدّل القيمة بعد بنائها
//    Services.tag(tag, names) / tagged(tag) / tags()
//    Services.forget(name) / reset(name) / flush()
//    Services.names() / count() / info(name)
//
//  الحقن التلقائي (autowiring): معاملات المصنع تُحلّ بأسمائها — أولاً من params الصريحة ثم من الخدمة
//  المسجَّلة بنفس الاسم:   Services.singleton("db", fun(config) { ... });   // config خدمة أخرى
//  الدوران (a ← b ← a) يُكشف برسالة تعرض السلسلة كاملة، بدل Stack Overflow.
//
//  حالة كل مفسّر مستقلة (shared_ptr تلتقطه الـ natives) — فلا تعارض بين جلسات المضيف المتعددة.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace services {

using Args = std::vector<Value>;

enum class Kind { Bind, Singleton, Instance, Alias, Container };

static const char* kindName(Kind k) {
    switch (k) {
        case Kind::Bind: return "bind";
        case Kind::Singleton: return "singleton";
        case Kind::Instance: return "instance";
        case Kind::Alias: return "alias";
        case Kind::Container: return "container";
    }
    return "bind";
}

struct Entry {
    Kind kind = Kind::Bind;
    Value factory;               // Bind / Singleton: دالة المصنع
    Value instance;              // Singleton (بعد أول بناء) / Instance
    bool resolved = false;
    std::string target;          // Alias: الاسم الأصلي · Container: اسم @container
    std::vector<Value> extenders;
};

struct State {
    std::map<std::string, Entry> entries;                      // مرتّبة بالاسم → names() حتمية
    std::map<std::string, std::vector<std::string>> tags;      // tag -> أسماء بترتيب الإضافة (بلا تكرار)
    std::vector<std::string> stack;                            // سلسلة الحل الجارية (لكشف الدوران)
    std::function<Value(const std::string&, const Value*, int)> resolve;
    std::function<Value(const Value&, const Value*, const Value*, int, const std::string&)> callWired;
};

static Value arrOf(ArrayData items) { return Value::makeArray(std::make_shared<ArrayData>(std::move(items))); }
static Value mapOf(MapData items) { return Value::makeMap(std::make_shared<MapData>(std::move(items))); }

static std::string joinChain(const std::vector<std::string>& chain, const std::string& last) {
    std::string out;
    for (const auto& c : chain) out += c + " \xE2\x86\x92 ";   // ←→ بسهم → (U+2192)
    return out + last;
}

} // namespace services

void Interpreter::registerNativesServices() {
    using namespace services;
    auto st = std::make_shared<State>();
    State* S = st.get();   // الـ std::function داخل State تلتقط المؤشر الخام (لا دورة shared_ptr)

    auto nameArg = [](const Value& v, const char* fn, int line) -> std::string {
        if (v.type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "' يتوقع اسم الخدمة نصاً لكن وُجد نوع " + v.typeName());
        if (v.str.empty())
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': اسم الخدمة لا يكون فارغاً");
        return v.str;
    };
    auto fnArg = [](const Value& v, const char* fn, const char* what, int line) {
        if (v.type != Value::Type::FUNCTION || !v.function)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': " + what + " يجب أن يكون دالة لكن وُجد نوع " + v.typeName());
    };
    auto paramsArg = [](const Args& a, size_t idx, const char* fn, int line) -> const Value* {
        if (a.size() <= idx || a[idx].type == Value::Type::NIL) return nullptr;
        if (a[idx].type != Value::Type::MAP || !a[idx].map)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "': params يجب أن يكون قاموساً {اسم: قيمة} لكن وُجد نوع " + a[idx].typeName());
        return &a[idx];
    };
    auto need = [](const char* fn, const Args& a, size_t lo, size_t hi, int line) {
        if (a.size() < lo || a.size() > hi)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "' expects " +
                          (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) + " argument(s) but got " + std::to_string(a.size()));
    };

    // ---- اسم أصلي بعد تتبّع البدائل؛ فارغ إن لم يوجد
    auto realName = [S](std::string n) -> std::string {
        for (int hop = 0; hop < 64; hop++) {
            auto it = S->entries.find(n);
            if (it == S->entries.end()) return "";
            if (it->second.kind != Kind::Alias) return n;
            n = it->second.target;
        }
        return "";
    };
    auto notFound = [S](const std::string& n, int line) {
        std::vector<std::string> cands;
        for (const auto& kv : S->entries) cands.push_back(kv.first);
        auto m = diag::nearestMatches(n, cands, 2, 1);
        std::string msg = "الخدمة '" + n + "' غير مسجّلة";
        if (!m.empty()) msg += " — هل تقصد '" + m.front() + "'؟";
        return diagErr(diag::Code::E0015_UnknownContainer, line, msg);
    };

    // ---- نداء دالة مع الحقن التلقائي: first (اختياري) يُسند للمعامل الأول؛ البقية: params ثم الخدمات
    S->callWired = [this, S](const Value& fn, const Value* params, const Value* first, int line, const std::string& what) -> Value {
        Args args;
        if (fn.type == Value::Type::FUNCTION && fn.function && fn.function->declaration) {
            const auto& ps = fn.function->declaration->params;
            for (size_t i = 0; i < ps.size(); i++) {
                if (i == 0 && first) { args.push_back(*first); continue; }
                const Value* given = params && params->map ? params->map->findValue(Value::string(ps[i])) : nullptr;
                if (given) { args.push_back(*given); continue; }
                // خدمة بنفس الاسم (أو بديل لها)؟
                std::string n = ps[i];
                bool isService = false;
                for (int hop = 0; hop < 64; hop++) {
                    auto it = S->entries.find(n);
                    if (it == S->entries.end()) break;
                    if (it->second.kind != Kind::Alias) { isService = true; break; }
                    n = it->second.target;
                }
                if (!isService)
                    throw diagErr(diag::Code::E0035_RuntimeError, line,
                                  what + ": تعذّر حقن المعامل '" + ps[i] + "' — لا قيمة له في params ولا خدمة مسجّلة بهذا الاسم");
                args.push_back(S->resolve(ps[i], nullptr, line));
            }
        } else if (first) {
            args.push_back(*first);
        }
        return callValue(fn, args, line);
    };

    // ---- الحل: يبني الخدمة (مع الدوران، المشاركة، والمُزخرِفات)
    S->resolve = [this, S, realName, notFound](const std::string& name, const Value* params, int line) -> Value {
        std::string n = realName(name);
        if (n.empty()) throw notFound(name, line);
        if (std::find(S->stack.begin(), S->stack.end(), n) != S->stack.end()) {
            throw diagErr(diag::Code::E0030_CircularDependency, line,
                          "تبعية دائرية أثناء حل الخدمة '" + n + "': " + joinChain(S->stack, n));
        }
        // نسخة من الحقول اللازمة: قد يستدعي المصنع forget/bind ويُبطل أي مرجع للمُدخل
        Kind kind; Value factory, inst; bool resolved; std::string target; std::vector<Value> extenders;
        {
            const Entry& e = S->entries[n];
            kind = e.kind; factory = e.factory; inst = e.instance; resolved = e.resolved; target = e.target; extenders = e.extenders;
        }
        if (kind == Kind::Instance) return inst;
        if (kind == Kind::Singleton && resolved) return inst;

        S->stack.push_back(n);
        struct Pop { std::vector<std::string>& s; ~Pop() { s.pop_back(); } } pop{S->stack};

        Value v;
        if (kind == Kind::Container) {
            Args sa{Value::string(target)};
            v = natives["container.snapshot"](sa, line);
            if (v.type == Value::Type::NIL)
                throw diagErr(diag::Code::E0015_UnknownContainer, line, "الخدمة '" + n + "' مربوطة بالحاوية '" + target + "' وهي غير موجودة");
        } else {
            v = S->callWired(factory, params, nullptr, line, "الخدمة '" + n + "'");
        }
        for (const auto& ex : extenders) {
            Value r = S->callWired(ex, nullptr, &v, line, "مُزخرِف الخدمة '" + n + "'");
            if (r.type != Value::Type::NIL) v = r;   // nil = عدّل بالمرجع ولا تستبدل
        }
        if (kind == Kind::Singleton) {
            auto it = S->entries.find(n);
            if (it != S->entries.end() && it->second.kind == Kind::Singleton) { it->second.instance = v; it->second.resolved = true; }
        }
        return v;
    };

    auto addTag = [S](const std::string& tag, const std::string& name) {
        auto& v = S->tags[tag];
        if (std::find(v.begin(), v.end(), name) == v.end()) v.push_back(name);
    };

    // ---------------------------------------------------------------- التسجيل
    natives["Services.bind"] = [=](Args& a, int line) -> Value {
        need("Services.bind", a, 2, 2, line);
        std::string n = nameArg(a[0], "Services.bind", line); fnArg(a[1], "Services.bind", "المصنع", line);
        Entry e; e.kind = Kind::Bind; e.factory = a[1];
        S->entries[n] = std::move(e);
        return Value::boolean_(true);
    };
    natives["Services.singleton"] = [=](Args& a, int line) -> Value {
        need("Services.singleton", a, 2, 2, line);
        std::string n = nameArg(a[0], "Services.singleton", line); fnArg(a[1], "Services.singleton", "المصنع", line);
        Entry e; e.kind = Kind::Singleton; e.factory = a[1];
        S->entries[n] = std::move(e);
        return Value::boolean_(true);
    };
    natives["Services.instance"] = [=](Args& a, int line) -> Value {
        need("Services.instance", a, 2, 2, line);
        std::string n = nameArg(a[0], "Services.instance", line);
        Entry e; e.kind = Kind::Instance; e.instance = a[1]; e.resolved = true;
        S->entries[n] = std::move(e);
        return a[1];
    };
    natives["Services.fromContainer"] = [=](Args& a, int line) -> Value {
        need("Services.fromContainer", a, 1, 2, line);
        std::string n = nameArg(a[0], "Services.fromContainer", line);
        std::string c = a.size() > 1 ? nameArg(a[1], "Services.fromContainer", line) : n;
        Entry e; e.kind = Kind::Container; e.target = c;
        S->entries[n] = std::move(e);
        return Value::boolean_(true);
    };
    natives["Services.alias"] = [=](Args& a, int line) -> Value {
        need("Services.alias", a, 2, 2, line);
        std::string al = nameArg(a[0], "Services.alias", line), target = nameArg(a[1], "Services.alias", line);
        if (al == target) throw diagErr(diag::Code::E0030_CircularDependency, line, "Services.alias: البديل '" + al + "' يشير إلى نفسه");
        // الدوران: اتبع سلسلة البدائل من الهدف؛ إن عادت إلى al فهي حلقة
        std::string cur = target;
        for (int hop = 0; hop < 64; hop++) {
            if (cur == al) throw diagErr(diag::Code::E0030_CircularDependency, line, "Services.alias: بديل دائري '" + al + "' ← '" + target + "'");
            auto it = S->entries.find(cur);
            if (it == S->entries.end() || it->second.kind != Kind::Alias) break;
            cur = it->second.target;
        }
        Entry e; e.kind = Kind::Alias; e.target = target;
        S->entries[al] = std::move(e);
        return Value::boolean_(true);
    };

    // ---------------------------------------------------------------- الحل
    natives["Services.has"] = [=](Args& a, int line) -> Value {
        need("Services.has", a, 1, 1, line);
        return Value::boolean_(!realName(nameArg(a[0], "Services.has", line)).empty());
    };
    natives["Services.get"] = [=](Args& a, int line) -> Value {
        need("Services.get", a, 1, 1, line);
        return S->resolve(nameArg(a[0], "Services.get", line), nullptr, line);
    };
    natives["Services.make"] = [=](Args& a, int line) -> Value {
        need("Services.make", a, 1, 2, line);
        std::string n = nameArg(a[0], "Services.make", line);
        return S->resolve(n, paramsArg(a, 1, "Services.make", line), line);
    };
    natives["Services.call"] = [=](Args& a, int line) -> Value {
        need("Services.call", a, 1, 2, line);
        fnArg(a[0], "Services.call", "الوسيط الأول", line);
        return S->callWired(a[0], paramsArg(a, 1, "Services.call", line), nullptr, line, "Services.call");
    };

    // ---------------------------------------------------------------- مُزخرِف
    natives["Services.extend"] = [=](Args& a, int line) -> Value {
        need("Services.extend", a, 2, 2, line);
        std::string req = nameArg(a[0], "Services.extend", line); fnArg(a[1], "Services.extend", "المُزخرِف", line);
        std::string n = realName(req);
        if (n.empty()) throw notFound(req, line);
        Entry& e = S->entries[n];
        if (e.kind == Kind::Instance || (e.kind == Kind::Singleton && e.resolved)) {
            // الكائن موجود فعلاً: يُطبَّق المُزخرِف فوراً ولا يُحفظ لإعادة البناء
            Value cur = e.instance;
            Value r = S->callWired(a[1], nullptr, &cur, line, "مُزخرِف الخدمة '" + n + "'");
            auto it = S->entries.find(n);
            if (it != S->entries.end()) {
                it->second.instance = r.type == Value::Type::NIL ? cur : r;
                if (it->second.kind == Kind::Singleton) it->second.extenders.push_back(a[1]);
            }
        } else {
            e.extenders.push_back(a[1]);
        }
        return Value::boolean_(true);
    };

    // ---------------------------------------------------------------- وسوم
    natives["Services.tag"] = [=](Args& a, int line) -> Value {
        need("Services.tag", a, 2, 2, line);
        std::string tag = nameArg(a[0], "Services.tag", line);
        if (a[1].type == Value::Type::STRING) addTag(tag, nameArg(a[1], "Services.tag", line));
        else if (a[1].type == Value::Type::ARRAY && a[1].array) { for (const auto& x : *a[1].array) addTag(tag, nameArg(x, "Services.tag", line)); }
        else throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'Services.tag': الأسماء نص أو مصفوفة نصوص لكن وُجد نوع ") + a[1].typeName());
        return Value::num(static_cast<double>(S->tags[tag].size()));
    };
    natives["Services.tagged"] = [=](Args& a, int line) -> Value {
        need("Services.tagged", a, 1, 1, line);
        std::string tag = nameArg(a[0], "Services.tagged", line);
        ArrayData out;
        auto it = S->tags.find(tag);
        if (it != S->tags.end()) {
            std::vector<std::string> names = it->second;   // نسخة: قد يعدّل المصنع الوسوم
            for (const auto& n : names) out.push_back(S->resolve(n, nullptr, line));
        }
        return arrOf(std::move(out));
    };
    natives["Services.tags"] = [=](Args& a, int line) -> Value {
        need("Services.tags", a, 0, 0, line);
        ArrayData out;
        for (const auto& kv : S->tags) if (!kv.second.empty()) out.push_back(Value::string(kv.first));
        return arrOf(std::move(out));
    };

    // ---------------------------------------------------------------- الإدارة
    natives["Services.forget"] = [=](Args& a, int line) -> Value {
        need("Services.forget", a, 1, 1, line);
        std::string n = nameArg(a[0], "Services.forget", line);
        bool had = S->entries.erase(n) > 0;
        if (had) for (auto& kv : S->tags) kv.second.erase(std::remove(kv.second.begin(), kv.second.end(), n), kv.second.end());
        return Value::boolean_(had);
    };
    natives["Services.reset"] = [=](Args& a, int line) -> Value {   // يُسقط النسخة المخزَّنة فقط ويُبقي التعريف
        need("Services.reset", a, 1, 1, line);
        std::string n = realName(nameArg(a[0], "Services.reset", line));
        if (n.empty()) return Value::boolean_(false);
        Entry& e = S->entries[n];
        if (e.kind != Kind::Singleton || !e.resolved) return Value::boolean_(false);
        e.resolved = false; e.instance = Value::nil();
        return Value::boolean_(true);
    };
    natives["Services.flush"] = [=](Args& a, int line) -> Value {
        need("Services.flush", a, 0, 0, line);
        size_t n = S->entries.size();
        S->entries.clear(); S->tags.clear();
        return Value::num(static_cast<double>(n));
    };
    natives["Services.names"] = [=](Args& a, int line) -> Value {
        need("Services.names", a, 0, 0, line);
        ArrayData out;
        for (const auto& kv : S->entries) out.push_back(Value::string(kv.first));
        return arrOf(std::move(out));
    };
    natives["Services.count"] = [=](Args& a, int line) -> Value {
        need("Services.count", a, 0, 0, line);
        return Value::num(static_cast<double>(S->entries.size()));
    };
    natives["Services.info"] = [=](Args& a, int line) -> Value {
        need("Services.info", a, 1, 1, line);
        std::string n = nameArg(a[0], "Services.info", line);
        auto it = S->entries.find(n);
        if (it == S->entries.end()) return Value::nil();
        const Entry& e = it->second;
        MapData m;
        m.push_back({Value::string("name"), Value::string(n)});
        m.push_back({Value::string("kind"), Value::string(kindName(e.kind))});
        m.push_back({Value::string("shared"), Value::boolean_(e.kind == Kind::Singleton || e.kind == Kind::Instance)});
        m.push_back({Value::string("resolved"), Value::boolean_(e.kind == Kind::Instance || (e.kind == Kind::Singleton && e.resolved))});
        m.push_back({Value::string("target"), e.target.empty() ? Value::nil() : Value::string(e.target)});
        ArrayData tg;
        for (const auto& kv : S->tags)
            if (std::find(kv.second.begin(), kv.second.end(), n) != kv.second.end()) tg.push_back(Value::string(kv.first));
        m.push_back({Value::string("tags"), arrOf(std::move(tg))});
        m.push_back({Value::string("extenders"), Value::num(static_cast<double>(e.extenders.size()))});
        return mapOf(std::move(m));
    };

    servicesState_ = st;   // يبقي الحالة حيّة مع المفسّر (الـ natives تلتقط نسخة أيضاً)
}

} // namespace rin
