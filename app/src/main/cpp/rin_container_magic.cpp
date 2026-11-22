// ============================================================================
//  rin_container_magic.cpp — خُطّافات الأعضاء الغائبة للحاوية (الخطوة 3 من خارطة الحاويات)
//  on get(name) · on set(name, value) · on has(name) · on drop(name) · on call(name, args) · on invoke(args)
//  تُستدعى **فقط** حين يكون العضو غير معرَّف في الحاوية؛ العضو الموجود يغلب دائماً (لا تغيير في سلوك أي كود قديم).
//  يُضمَّن من rin_interpreter.cpp. التوثيق: docs/container_magic.md
// ============================================================================
#include "rin_interpreter.h"

namespace rin {

bool Interpreter::magicHook(const std::shared_ptr<NsNode>& n, Value LifecycleHooks::*slot, const char* kind, const std::string& member,
                            std::vector<Value> args, int line, Value& out) {
    if (!n || n->kind != NsNode::Kind::Container) return false;
    auto it = containerLifecycle.find(n->key);
    if (it == containerLifecycle.end()) return false;
    const Value& fn = it->second.*slot;
    if (fn.type != Value::Type::FUNCTION || !fn.function) return false;
    // حارس إعادة الدخول: داخل الخُطّاف نفسه، قراءة/كتابة/نداء نفس الاسم الغائب يسقط للخطأ العادي بدل دوران لانهائي.
    std::string guard = n->key + "|" + kind + "|" + member;
    if (!magicBusy_.insert(guard).second) return false;
    struct Release { std::unordered_set<std::string>& s; std::string g; ~Release() { s.erase(g); } } rel{magicBusy_, guard};
    Value f = fn; // نسخة: الخُطّاف قد يعيد تعريف نفسه
    out = callFunction(f.function, args, line);
    return true;
}

void Interpreter::registerNativesMagic() {
    // container.has(c, field) / hasField: العضو الغائب يسأل on has(name)؛ النتيجة تُحوَّل إلى منطقي.
    {
        auto orig = natives["hasField"];
        auto wrapped = [this, orig](std::vector<Value>& a, int line) -> Value {
            Value r = orig(a, line);
            if (!magicActive_ || r.type != Value::Type::BOOL || r.boolean || a.size() < 2 || a[0].type != Value::Type::STRING) return r;
            std::string key = canonContainer(a[0].str);
            auto n = nsResolvePath(containerPathOf.count(key) ? containerPathOf[key] : key);
            Value hv; if (n && magicHook(n, &LifecycleHooks::has, "has", a[1].toDisplayString(), {Value::string(a[1].toDisplayString())}, line, hv)) return Value::boolean_(hv.isTruthy());
            return r;
        };
        natives["hasField"] = wrapped;
        natives["container.has"] = wrapped;
    }
    // container.deleteField(c, field): العضو الغائب يستدعي on drop(name)؛ يعيد true إن عالج الخُطّاف الطلب.
    {
        auto orig = natives["container.deleteField"];
        natives["container.deleteField"] = [this, orig](std::vector<Value>& a, int line) -> Value {
            Value r = orig(a, line);
            if (!magicActive_ || r.type != Value::Type::BOOL || r.boolean || a.size() < 2 || a[0].type != Value::Type::STRING) return r;
            std::string key = canonContainer(a[0].str);
            auto n = nsResolvePath(containerPathOf.count(key) ? containerPathOf[key] : key);
            Value hv; if (n && magicHook(n, &LifecycleHooks::drop, "drop", a[1].toDisplayString(), {Value::string(a[1].toDisplayString())}, line, hv)) return Value::boolean_(true);
            return r;
        };
    }
}

} // namespace rin
