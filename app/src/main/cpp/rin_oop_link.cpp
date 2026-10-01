// ============================================================================
//  rin_oop_link.cpp — Rin 1.0: ربط كائنات OOP بالحاويات (container) وبخلايا Warp الخاصة بـ Indsin
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp بعد rin_oop_bind.cpp، ويُسجَّل من
//  Interpreter::registerNativesOopLink() (تُستدعى في نهاية registerNativesOopBind).
//
//  الفكرة: حقل كائن  <->  هدف خارجي، وكل كتابة على أحد الطرفين تنتقل إلى الآخر تلقائياً.
//
//  الحاويات
//    oop.bindContainer(obj, "field", "Container", "key", opts?)         الكائن -> الحاوية
//    oop.bindContainerFrom(obj, "field", "Container", "key", opts?)     الحاوية -> الكائن
//    oop.bindContainerTwoWay(obj, "field", "Container", "key", opts?)   اتجاهان
//    oop.watchContainer("Container", "key"|"*", fn(new, old, key, container)) -> id
//    oop.toContainer(obj, "Container", [fields]?)    نسخ لمرة واحدة (حقول الكائن العامة -> الحاوية)
//    oop.fromContainer(obj, "Container", [fields]?)  نسخ لمرة واحدة (حقول الحاوية -> الكائن)
//
//  Indsin (خلايا Warp = المتغيّرات العامة التي ينقلها Indsin من/إلى المعالجات عند كل tap)
//    oop.bindWarp(obj, "field", "cell", opts?)         الكائن -> الخلية (تظهر على الشاشة)
//    oop.bindWarpFrom(obj, "field", "cell", opts?)     الخلية -> الكائن
//    oop.bindWarpTwoWay(obj, "field", "cell", opts?)   اتجاهان (الأنسب لواجهة تفاعلية)
//    oop.bindView(obj, {field: "cell", ...} | "prefix_", opts?) -> أسماء الحقول المربوطة (اتجاهان)
//
//  عام
//    oop.syncLinks() -> عدد الروابط التي حُدِّثت (سحب يدوي يغطي الكتابات الخام)
//    oop.unobserve(id) / oop.unobserveAll(obj) / oop.observers(obj)  (تشمل هذه الروابط أيضاً)
//
//  opts (قاموس اختياري): { transform: fn(value), init: "object" | "target" | "none" }
//    transform: يحوّل القيمة العابرة في الاتجاه الوحيد (push أو pull)؛ مرفوض مع two-way (لا معكوس له).
//    init: المزامنة الأولية — الافتراضي "object" لـ push وtwo-way (الكائن مصدر الحقيقة)،
//          و"target" لـ pull. "none" = بلا مزامنة أولية.
//
//  حدود Warp: Indsin يخزّن الخلايا أرقاماً أو نصوصاً فقط (والمنطقي كنص "true"/"false" يُعاد منطقياً)؛
//  لذا تُربَط بها حقول بدائية (رقم/نص/منطقي). nil تُعامَل كنص فارغ في الاتجاهين.
//  الحاويات تقبل أي قيمة.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace oopl {
// "فارغ" = nil أو نص فارغ: Indsin يحوّل nil إلى "" عند تخزين الخلية، فيجب اعتبارهما متساويين لمنع صدى وهمي.
static bool emptyish(const Value& v) {
    return v.type == Value::Type::NIL || (v.type == Value::Type::STRING && v.str.empty());
}
static bool sameCell(const Value& a, const Value& b) {
    if (emptyish(a) && emptyish(b)) return true;
    if (a.type != b.type) return false;
    if (a.type == Value::Type::INSTANCE || a.type == Value::Type::ARRAY || a.type == Value::Type::MAP ||
        a.type == Value::Type::FUNCTION)
        return false; // قيم مركّبة: لا نفترض تساوياً (تُنقل دائماً)
    return valuesEqual(a, b);
}
} // namespace oopl

// ---------------------------------------------------------------------------
// أدوات داخلية
// ---------------------------------------------------------------------------
static OopLink* linkById(std::vector<OopLink>& v, int id) {
    for (auto& l : v) if (l.id == id) return &l;
    return nullptr;
}

// بيئة بسياق صنف الكائن: تسمح بقراءة/كتابة حقوله private/protected وخصائصه (get/set) عند الربط،
// بدل معاملة الربط كنداء خارجي. final والتجميد (freeze) يبقيان محترمَين (يرفضان الكتابة).
static EnvPtr classCtxEnv(const EnvPtr& globals, const std::string& cls) {
    auto env = std::make_shared<Environment>(globals);
    env->define("__class__", Value::string(cls));
    return env;
}

void Interpreter::pushLinks(const Value& obj, const std::string& field, const Value& newV, int line) {
    if (obj.type != Value::Type::INSTANCE || !obj.instance) return;
    std::vector<int> ids;
    for (auto& l : links_) {
        if (l.target == OopLink::Target::Watch || l.mode == 1) continue; // Watch بلا كائن؛ pull لا يدفع
        auto sp = l.obj.lock();
        if (sp && sp == obj.instance && l.field == field) ids.push_back(l.id);
    }
    for (int id : ids) {
        OopLink* l = linkById(links_, id);
        if (!l || l->busy) continue;
        Value v = newV;
        if (l->mode == 0 && l->transform.type == Value::Type::FUNCTION) {
            Value tf = l->transform;
            v = callFlexible(tf, {newV, obj}, line);
            l = linkById(links_, id);
            if (!l) continue;
        }
        if (l->mode == 2 && l->hasSeen && oopl::sameCell(v, l->lastSeen)) continue; // صدى اتجاه الرجوع
        l->busy = true;
        l->lastSeen = v;
        l->hasSeen = true;
        OopLink::Target tgt = l->target;
        std::string cont = l->container, key = l->key;
        try {
            if (tgt == OopLink::Target::Warp) {
                globals->values[key] = v;
            } else {
                auto cit = containers.find(cont);
                if (cit != containers.end() && cit->second) {
                    EnvPtr keep = cit->second;
                    assignStateAware(keep.get(), key, v, line); // يُطلِق on update لحقول state
                }
            }
        } catch (...) {
            if (OopLink* l2 = linkById(links_, id)) l2->busy = false;
            throw;
        }
        if (OopLink* l2 = linkById(links_, id)) l2->busy = false;
    }
}

// يطبّق قيمة قادمة من الهدف على حقل الكائن. true إن كُتبت فعلاً.
bool Interpreter::applyPullLink(int id, const Value& incoming, int line) {
    OopLink* l = linkById(links_, id);
    if (!l || l->busy) return false;
    auto sp = l->obj.lock();
    if (!sp) return false;
    if (l->hasSeen && oopl::sameCell(incoming, l->lastSeen)) return false; // لا جديد / صدى
    Value v = incoming;
    if (l->mode == 1 && l->transform.type == Value::Type::FUNCTION) {
        Value tf = l->transform;
        Value objV = Value::makeInstance(sp);
        v = callFlexible(tf, {incoming, objV}, line);
        l = linkById(links_, id);
        if (!l) return false;
    }
    l->lastSeen = incoming;   // نخزّن ما رآه الهدف (قبل التحويل) كي لا يُعاد سحبه
    l->hasSeen = true;
    l->busy = true;
    std::string field = l->field;
    try {
        Value objV = Value::makeInstance(sp);
        writeMember(objV, field, v, classCtxEnv(globals, sp->className), line);
    } catch (...) {
        if (OopLink* l2 = linkById(links_, id)) l2->busy = false;
        throw;
    }
    if (OopLink* l2 = linkById(links_, id)) l2->busy = false;
    return true;
}

void Interpreter::onContainerWrite(const std::string& container, const std::string& key, const Value& oldV,
                                   const Value& newV, int line) {
    std::vector<int> ids;
    for (auto& l : links_) {
        if (l.container != container) continue;
        if (l.target == OopLink::Target::Watch) {
            if (l.key == "*" || l.key == key) ids.push_back(l.id);
        } else if (l.target == OopLink::Target::Container && l.mode != 0 && l.key == key) {
            ids.push_back(l.id);
        }
    }
    for (int id : ids) {
        OopLink* l = linkById(links_, id);
        if (!l) continue;
        if (l->target == OopLink::Target::Watch) {
            if (oopl::sameCell(oldV, newV) && !oopl::emptyish(oldV)) continue; // لا تغيير فعلي
            Value fn = l->fn;
            callFlexible(fn, {newV, oldV, Value::string(key), Value::string(container)}, line);
        } else {
            applyPullLink(id, newV, line);
        }
    }
    // تنظيف الروابط التي زال كائنها
    links_.erase(std::remove_if(links_.begin(), links_.end(), [](const OopLink& l) {
        return l.target != OopLink::Target::Watch && l.obj.expired();
    }), links_.end());
}

void Interpreter::syncWarpLinks(int line) {
    std::vector<int> ids;
    for (auto& l : links_)
        if (l.target == OopLink::Target::Warp && l.mode != 0) ids.push_back(l.id);
    for (int id : ids) {
        OopLink* l = linkById(links_, id);
        if (!l) continue;
        Value cur;
        if (!globals->get(l->key, cur)) continue;
        applyPullLink(id, cur, line);
    }
}

int Interpreter::syncAllLinks(int line) {
    int n = 0;
    std::vector<int> ids;
    for (auto& l : links_)
        if (l.target != OopLink::Target::Watch && l.mode != 0) ids.push_back(l.id);
    for (int id : ids) {
        OopLink* l = linkById(links_, id);
        if (!l) continue;
        Value cur;
        bool have = false;
        if (l->target == OopLink::Target::Warp) {
            have = globals->get(l->key, cur);
        } else {
            auto cit = containers.find(l->container);
            have = cit != containers.end() && cit->second && cit->second->get(l->key, cur);
        }
        if (have && applyPullLink(id, cur, line)) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// التسجيل
// ---------------------------------------------------------------------------
void Interpreter::registerNativesOopLink() {
    using namespace extra;
    using Args = std::vector<Value>;

    auto N = [](double d) { return Value::num(d); };
    auto S = [](const std::string& s) { return Value::string(s); };

    auto needInst = [](const Value& v, const std::string& fn, int line) {
        if (v.type != Value::Type::INSTANCE || !v.instance)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an object but got " + v.typeName());
    };
    auto needContainer = [this](const std::string& name, const std::string& fn, int line) -> EnvPtr {
        auto it = containers.find(name);
        if (it == containers.end() || !it->second)
            throw errWithReason(diag::Code::E0014_InvalidContainer, line,
                                "'" + fn + "': '" + name + "' is not a known container",
                                "create it first via `@container=" + name + "` or `spawn(kind, \"" + name + "\")`");
        return it->second;
    };
    // opts -> (transform, init)
    struct Opts { Value transform; std::string init; };
    auto parseOpts = [this](const Args& a, size_t idx, const std::string& fn, int line, int mode) -> Opts {
        Opts o;
        o.init = mode == 1 ? "target" : "object";
        if (a.size() <= idx || a[idx].type == Value::Type::NIL) return o;
        if (a[idx].type != Value::Type::MAP || !a[idx].map)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' options must be a map like {transform: fn, init: \"object\"} but got " + a[idx].typeName());
        for (auto& kv : *a[idx].map) {
            std::string k = kv.first.toDisplayString();
            if (k == "transform") {
                if (kv.second.type != Value::Type::FUNCTION && kv.second.type != Value::Type::NIL)
                    throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "': option `transform` must be a function");
                o.transform = kv.second;
            } else if (k == "init") {
                o.init = kv.second.toDisplayString();
                if (o.init != "object" && o.init != "target" && o.init != "none")
                    throw errWithReason(diag::Code::E0007_InvalidArguments, line,
                                        "'" + fn + "': option `init` must be \"object\", \"target\" or \"none\" but got \"" + o.init + "\"",
                                        "\"object\" copies the object's value to the target first, \"target\" copies the target's value into the object, \"none\" does neither");
            } else {
                throw errWithReason(diag::Code::E0007_InvalidArguments, line,
                                    "'" + fn + "': unknown option `" + k + "`", "valid options are `transform` and `init`");
            }
        }
        if (mode == 2 && o.transform.type == Value::Type::FUNCTION)
            throw errWithReason(diag::Code::E0007_InvalidArguments, line,
                                "'" + fn + "': a two-way binding cannot take a `transform`",
                                "a transform has no inverse, so the two directions could never agree; use a one-way binding (bindX / bindXFrom) instead");
        return o;
    };

    // الإنشاء المشترك لرابط حاوية/Warp.
    auto createLink = [this, parseOpts](const Value& obj, const std::string& field, OopLink::Target tgt,
                                        const std::string& container, const std::string& key, int mode,
                                        const Args& a, size_t optsIdx, const std::string& fn, int line) -> int {
        Opts o = parseOpts(a, optsIdx, fn, line, mode);
        EnvPtr ctx = classCtxEnv(globals, obj.instance->className);
        Value cur = readMember(obj, field, ctx, line); // يتحقق أن الحقل/الخاصية موجود، ويعطي القيمة الأولية
        OopLink l;
        l.id = nextLinkId_++;
        l.target = tgt;
        l.obj = obj.instance;
        l.field = field;
        l.container = container;
        l.key = key;
        l.mode = mode;
        l.transform = o.transform;
        int id = l.id;
        links_.push_back(std::move(l));
        linksExist_ = true;
        observersExist_ = true; // يفعّل خطافات الكتابة في writeMember
        if (o.init == "object") {
            Value v = cur;
            if (mode == 0 && o.transform.type == Value::Type::FUNCTION) v = callFlexible(o.transform, {cur, obj}, line);
            OopLink* lp = linkById(links_, id);
            lp->busy = true; lp->lastSeen = v; lp->hasSeen = true;
            try {
                if (tgt == OopLink::Target::Warp) globals->values[key] = v;
                else { EnvPtr keep = containers.at(container); assignStateAware(keep.get(), key, v, line); }
            } catch (...) { if (OopLink* l2 = linkById(links_, id)) l2->busy = false; throw; }
            if (OopLink* l2 = linkById(links_, id)) l2->busy = false;
        } else if (o.init == "target") {
            Value tv;
            bool have = false;
            if (tgt == OopLink::Target::Warp) have = globals->get(key, tv);
            else { auto cit = containers.find(container); have = cit != containers.end() && cit->second->get(key, tv); }
            if (have) applyPullLink(id, tv, line);
        } else if (mode != 0) { // init none: نسجّل القيمة الحالية كي لا تُعدّ "تغييراً" عند أول مزامنة
            Value tv;
            bool have = false;
            if (tgt == OopLink::Target::Warp) have = globals->get(key, tv);
            else { auto cit = containers.find(container); have = cit != containers.end() && cit->second->get(key, tv); }
            if (have) { OopLink* lp = linkById(links_, id); lp->lastSeen = tv; lp->hasSeen = true; }
        }
        return id;
    };

    // ======================================================== الحاويات
    auto defContainerBind = [this, needInst, needContainer, createLink, N](const char* name, int mode) {
        std::string fn = name;
        natives[fn] = [this, fn, mode, needInst, needContainer, createLink, N](Args& a, int line) -> Value {
            need(fn, a, 4, 5, line);
            needInst(a[0], fn, line);
            std::string field = str(a[1], fn, line), cont = str(a[2], fn, line), key = str(a[3], fn, line);
            needContainer(cont, fn, line);
            return N(createLink(a[0], field, OopLink::Target::Container, cont, key, mode, a, 4, fn, line));
        };
    };
    defContainerBind("oop.bindContainer", 0);
    defContainerBind("oop.bindContainerFrom", 1);
    defContainerBind("oop.bindContainerTwoWay", 2);

    natives["oop.watchContainer"] = [this, needContainer, N](Args& a, int line) -> Value {
        need("oop.watchContainer", a, 3, 3, line);
        std::string cont = str(a[0], "oop.watchContainer", line), key = str(a[1], "oop.watchContainer", line);
        needContainer(cont, "oop.watchContainer", line);
        if (a[2].type != Value::Type::FUNCTION)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'oop.watchContainer' expects a callback function as 3rd argument but got " + a[2].typeName());
        OopLink l;
        l.id = nextLinkId_++;
        l.target = OopLink::Target::Watch;
        l.container = cont;
        l.key = key;
        l.fn = a[2];
        int id = l.id;
        links_.push_back(std::move(l));
        linksExist_ = true;
        return N(id);
    };

    // نسخ لمرة واحدة: حقول الكائن العامة -> الحاوية.
    natives["oop.toContainer"] = [this, needInst, needContainer, N](Args& a, int line) -> Value {
        need("oop.toContainer", a, 2, 3, line);
        needInst(a[0], "oop.toContainer", line);
        std::string cont = str(a[1], "oop.toContainer", line);
        EnvPtr env = needContainer(cont, "oop.toContainer", line);
        InstanceData& inst = *a[0].instance;
        std::vector<std::string> names;
        if (a.size() == 3) {
            for (auto& n : arr(a[2], "oop.toContainer", line)) names.push_back(n.toDisplayString());
        } else {
            for (auto& n : inst.fieldOrder) {
                auto fit = inst.fields.find(n);
                if (fit == inst.fields.end() || fit->second.type == Value::Type::FUNCTION) continue;
                if (const ClassDef::MemberMeta* fm = findFieldMeta(inst.className, n)) { if (!fm->access.empty()) continue; }
                names.push_back(n);
            }
        }
        EnvPtr ctx = classCtxEnv(globals, inst.className);
        double count = 0;
        for (auto& n : names) {
            Value v = readMember(a[0], n, ctx, line);
            assignStateAware(env.get(), n, copyForBinding(v), line);
            count += 1;
        }
        return N(count);
    };
    // نسخ لمرة واحدة: حقول الحاوية -> الكائن (فقط الحقول الموجودة أصلاً في الكائن إن لم تُحدَّد أسماء).
    natives["oop.fromContainer"] = [this, needInst, needContainer, N](Args& a, int line) -> Value {
        need("oop.fromContainer", a, 2, 3, line);
        needInst(a[0], "oop.fromContainer", line);
        std::string cont = str(a[1], "oop.fromContainer", line);
        EnvPtr env = needContainer(cont, "oop.fromContainer", line);
        InstanceData& inst = *a[0].instance;
        std::vector<std::string> names;
        if (a.size() == 3) {
            for (auto& n : arr(a[2], "oop.fromContainer", line)) names.push_back(n.toDisplayString());
        } else {
            for (auto& kv : env->values) {
                if (kv.first.rfind("__", 0) == 0 || kv.second.type == Value::Type::FUNCTION) continue;
                if (inst.fields.count(kv.first)) names.push_back(kv.first);
            }
            std::sort(names.begin(), names.end());
        }
        EnvPtr ctx = classCtxEnv(globals, inst.className);
        double count = 0;
        for (auto& n : names) {
            Value v;
            if (!env->get(n, v)) continue;
            writeMember(a[0], n, v, ctx, line);
            count += 1;
        }
        return N(count);
    };

    // ======================================================== Indsin / Warp
    auto defWarpBind = [this, needInst, createLink, N](const char* name, int mode) {
        std::string fn = name;
        natives[fn] = [this, fn, mode, needInst, createLink, N](Args& a, int line) -> Value {
            need(fn, a, 3, 4, line);
            needInst(a[0], fn, line);
            std::string field = str(a[1], fn, line), cell = str(a[2], fn, line);
            return N(createLink(a[0], field, OopLink::Target::Warp, "", cell, mode, a, 3, fn, line));
        };
    };
    defWarpBind("oop.bindWarp", 0);
    defWarpBind("oop.bindWarpFrom", 1);
    defWarpBind("oop.bindWarpTwoWay", 2);

    // bindView(obj, {field: "cell"}) ربط اتجاهين لكل زوج؛ bindView(obj, "prefix_") يربط كل حقل بدائي عام f
    // بالخلية prefix_f **إن كانت الخلية معرَّفة** (warp prefix_f = ...). يُعيد أسماء الحقول التي رُبطت.
    natives["oop.bindView"] = [this, needInst, createLink](Args& a, int line) -> Value {
        need("oop.bindView", a, 2, 3, line);
        needInst(a[0], "oop.bindView", line);
        std::vector<std::pair<std::string, std::string>> pairs;
        if (a[1].type == Value::Type::MAP && a[1].map) {
            for (auto& kv : *a[1].map) pairs.push_back({kv.first.toDisplayString(), kv.second.toDisplayString()});
        } else if (a[1].type == Value::Type::STRING) {
            InstanceData& inst = *a[0].instance;
            for (auto& n : inst.fieldOrder) {
                auto fit = inst.fields.find(n);
                if (fit == inst.fields.end()) continue;
                const Value::Type t = fit->second.type;
                if (t != Value::Type::NUMBER && t != Value::Type::STRING && t != Value::Type::BOOL && t != Value::Type::NIL) continue;
                if (const ClassDef::MemberMeta* fm = findFieldMeta(inst.className, n)) { if (!fm->access.empty()) continue; }
                Value probe;
                if (!globals->get(a[1].str + n, probe)) continue; // الخلية غير معرَّفة => لا ربط
                pairs.push_back({n, a[1].str + n});
            }
        } else {
            throw diagErr(diag::Code::E0004_InvalidType, line,
                          "'oop.bindView' expects a map {field: \"cell\"} or a cell-name prefix string but got " + a[1].typeName());
        }
        ArrayData out;
        for (auto& p : pairs) {
            createLink(a[0], p.first, OopLink::Target::Warp, "", p.second, 2, a, 2, "oop.bindView", line);
            out.push_back(Value::string(p.first));
        }
        return newArray(std::move(out));
    };

    natives["oop.syncLinks"] = [this, N](Args& a, int line) -> Value {
        need("oop.syncLinks", a, 0, 0, line);
        return N(syncAllLinks(line));
    };
}

} // namespace rin
