// ============================================================================
//  rin_timer.cpp — مفهوما time و timer للحاويات (docs/TIMER.md)
// ----------------------------------------------------------------------------
//  تُضمَّن (#include) في نهاية rin_interpreter.cpp بعد rin_extra_natives7.cpp (فتبقى مساعدات مفهوم time مرئية).
//
//  1) ساعة قابلة للتحكّم (rin_clock.h):  time.freeze([t]) · time.advance(مدّة) · time.unfreeze() · time.frozen()
//     — ساعة مُجمَّدة تتحرك يدوياً فتصير المؤقّتات والاختبارات حتمية. time.sleep يطلق ما استحقّ أثناء النوم.
//  2) مجدوِل داخل المفسّر:                timers.after/every/at/cancel/list/next/pending/tick/run/clear
//  3) `timer` متغيّر حيّ (من عائلة val/dialog):  عدّ تنازلي/تصاعدي، every/after/times/loop/until،
//     معالجا tick/ring، وأفعال start/pause/resume/stop/reset/restart/lap/extend، وخصائص t.left/.state/.text ...
//     داخل @container يصير حقلاً حيّاً (shop.promo.left) ويتكامل مع val وdialog وbell.
//  4) مفاهيم الحاوية الزمنية:  containerTime(c) عمر وآخر تعديل · timers.expire/ttl/persist انتهاء صلاحية حقل
//     · timers.pauseAll/resumeAll/stopAll(c) · dialog ... timeout DUR (إغلاق تلقائي).
// ============================================================================
#include "rin_interpreter.h"
#include "rin_clock.h"
#include "indsin/rin_indsin_icons.h"   // سجلّ الأيقونات (استيراد أيقونة من ردّ api)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

namespace rin {

// ------------------------------------------------------------------ المجدوِل
struct SchedEntry {
    enum class K { Callback, Timer, Ttl, DialogTimeout };
    K kind = K::Callback;
    long id = 0, seq = 0;
    double due = 0;                                   // الساعة الرتيبة (ms)
    Value fn; std::vector<Value> args;                // Callback
    std::function<void(int)> action;                  // Callback بفعل أصلي (بديل fn): يستعمله Intent.send {after}
    double interval = 0; long remaining = 1;          // Callback: التكرار (remaining = -1 بلا نهاية)
    std::string label, owner;                         // وصف للقوائم · اسم الحاوية (إن وُجد)
    std::weak_ptr<Environment> env; std::string name; // Timer / DialogTimeout
    std::weak_ptr<LiveMeta> meta;
    std::string container, field; Value fallback, onExpire; // Ttl
};

struct TimerHub {
    std::vector<std::shared_ptr<SchedEntry>> q;
    long nextId = 1, seq = 0, ttls = 0, fired = 0;
    int depth = 0;
};

TimerHub& Interpreter::hub() { if (!hub_) hub_ = std::make_shared<TimerHub>(); return *hub_; }
bool Interpreter::timersPending() const { return hub_ && !hub_->q.empty(); }

static const double kInf = std::numeric_limits<double>::infinity();
static const size_t kMaxEntries = 10000;
static const long kMaxEvents = 1000000;

// ---- api ✕ icons: استخراج مصدر أيقونة (SVG/مسار/data URI) من قيمة ردّ، واسمها المقترح ----------------
//  القيمة نصّ ⇒ هو المصدر. خريطة ⇒ svg | path | d | icon | body | content (النصّي)، والاسم من name | id. مصفوفة ⇒ أول عنصر.
static bool iconSourceOf(const Value& v, std::string& src, std::string& name, std::string& glyph) {
    if (v.type == Value::Type::STRING) { src = v.str; return !src.empty(); }
    const Value* m = &v;
    if (v.type == Value::Type::ARRAY && v.array && !v.array->empty()) m = &(*v.array)[0];
    if (m->type != Value::Type::MAP || !m->map) return false;
    for (auto& kv : *m->map) {
        const std::string k = kv.first.toDisplayString();
        if (kv.second.type != Value::Type::STRING) continue;
        if ((k == "name" || k == "id") && name.empty()) name = kv.second.str;
        else if (k == "glyph" && glyph.empty()) glyph = kv.second.str;
    }
    for (const char* k : {"svg", "path", "d", "icon", "body", "content"})
        for (auto& kv : *m->map) if (kv.second.type == Value::Type::STRING && kv.first.toDisplayString() == k && !indsin::IconRegistry::sourcePath(kv.second.str).empty()) { src = kv.second.str; return true; }
    return false;
}

// ---- حزم أيقونات خارجية: خريطة اسم→مصدر · {icons: خريطة|مصفوفة} · مصفوفة {name,svg|path|d} · نص sprite (<symbol id=…>) · نص JSON ----
static void iconPackEntries(const Value& v0, std::vector<std::pair<std::string, std::string>>& out, int depth = 0) {
    if (depth > 3) return;
    if (v0.type == Value::Type::STRING) {
        const std::string& t = v0.str; size_t a = t.find_first_not_of(" \t\r\n");
        if (a != std::string::npos && (t[a] == '{' || t[a] == '[')) { Value d = json::decodeOrRaw(t); if (d.type != Value::Type::STRING) iconPackEntries(d, out, depth + 1); return; }
        for (auto& e : indsin::IconRegistry::symbols(t)) out.push_back(e);
        return;
    }
    if (v0.type == Value::Type::ARRAY && v0.array) {
        for (auto& el : *v0.array) { std::string src, nm, gl; if (el.type == Value::Type::MAP && iconSourceOf(el, src, nm, gl) && !nm.empty()) out.emplace_back(nm, src); }
        return;
    }
    if (v0.type != Value::Type::MAP || !v0.map) return;
    for (auto& kv : *v0.map) if (kv.first.toDisplayString() == "icons" && (kv.second.type == Value::Type::MAP || kv.second.type == Value::Type::ARRAY || kv.second.type == Value::Type::STRING)) { iconPackEntries(kv.second, out, depth + 1); return; }
    for (auto& kv : *v0.map) {
        std::string src, nm, gl; const std::string k = kv.first.toDisplayString();
        if (kv.second.type == Value::Type::STRING) { if (!indsin::IconRegistry::sourcePath(kv.second.str).empty()) out.emplace_back(k, kv.second.str); }
        else if (kv.second.type == Value::Type::MAP && iconSourceOf(kv.second, src, nm, gl)) out.emplace_back(k, src);
    }
}
// يسجّل كل أيقونة صالحة (مع بادئة اختيارية) ويعيد أسماءها؛ الأعداد محدودة (2000) لمنع حزمة ضخمة.
static std::vector<std::string> iconPackImport(const Value& v, const std::string& prefix) {
    std::vector<std::pair<std::string, std::string>> es; iconPackEntries(v, es);
    std::vector<std::string> names;
    for (auto& e : es) { if (names.size() >= 2000) break; std::string n = prefix + e.first; if (indsin::iconRegistry().importSource(n, e.second)) names.push_back(n); }
    return names;
}

struct TimerOps {
    using St = TimerState::St;
    using Args = std::vector<Value>;

    // ---------------------------------------------------------- مساعدات عامة
    static RinError misuse(int line, const std::string& msg, const std::string& hint = "") { return liveErr(diag::Code::E0046_LiveMisuse, line, msg, hint); }
    static RinError badArg(int line, const std::string& msg) { return diagErr(diag::Code::E0007_InvalidArguments, line, msg); }
    static RinError badTyp(int line, const std::string& msg) { return diagErr(diag::Code::E0004_InvalidType, line, msg); }

    // مدّة: رقم = ميلي ثانية؛ نص ("1h30m" · "ساعة") أو خريطة ({hours:2}) أو span عبر مفهوم time
    static double dur(const Value& v, int line, const std::string& fn, bool allowZero = true) {
        double ms;
        if (v.type == Value::Type::NUMBER) ms = v.number;
        else if (v.type == Value::Type::STRING || v.type == Value::Type::MAP)
            ms = extra7::durationMs(extra7::durationParts(v, nullptr, line, fn.c_str()));
        else throw badTyp(line, fn + ": a duration must be milliseconds, text like \"1h30m\", or a map like {minutes: 5}; got " + v.typeName());
        if (std::isnan(ms) || std::isinf(ms) || ms < 0 || (!allowZero && ms == 0)) throw badArg(line, fn + ": the duration must be " + (allowZero ? "zero or more" : "greater than zero") + " (got " + Value::num(ms).toDisplayString() + ")");
        return ms;
    }
    static Value arr(std::vector<Value> v) { return Value::makeArray(std::make_shared<ArrayData>(std::move(v))); }
    static Value mapOf(std::vector<std::pair<std::string, Value>> kv) {
        auto m = std::make_shared<MapData>();
        for (auto& p : kv) m->push_back({Value::string(p.first), std::move(p.second)});
        return Value::makeMap(m);
    }
    static Value instant(double epochMs) {
        extra7::T t; t.ms = static_cast<long long>(std::llround(epochMs)); t.off = extra7::localOffsetMin(t.ms);
        return extra7::mkTime(t);
    }
    static Value span(double ms) { return extra7::mkSpan(ms); }
    static std::string containerKeyOf(Interpreter& I, const Environment* env) {
        for (auto& kv : I.containers) if (kv.second.get() == env) return kv.first;
        return "";
    }

    // ---------------------------------------------------------- إدارة المدخلات
    static long add(Interpreter& I, std::shared_ptr<SchedEntry> e, int line) {
        TimerHub& h = I.hub();
        if (h.q.size() >= kMaxEntries) throw misuse(line, "too many pending timers (" + std::to_string(kMaxEntries) + ")", "cancel some with `timers.cancel(id)` or `timers.clear()`");
        if (e->id == 0) e->id = h.nextId++;
        e->seq = h.seq++;
        if (e->kind == SchedEntry::K::Ttl) h.ttls++;
        h.q.push_back(e);
        return e->id;
    }
    static bool remove(Interpreter& I, long id) {
        TimerHub& h = I.hub();
        for (size_t i = 0; i < h.q.size(); ++i) if (h.q[i]->id == id) {
            if (h.q[i]->kind == SchedEntry::K::Ttl) h.ttls--;
            h.q.erase(h.q.begin() + static_cast<long>(i));
            return true;
        }
        return false;
    }
    static std::shared_ptr<SchedEntry> popDue(Interpreter& I, double until) {
        TimerHub& h = I.hub();
        int best = -1;
        for (size_t i = 0; i < h.q.size(); ++i) {
            auto& e = h.q[i];
            if (e->due > until + 1e-9) continue;
            if (best < 0 || e->due < h.q[static_cast<size_t>(best)]->due ||
                (e->due == h.q[static_cast<size_t>(best)]->due && e->seq < h.q[static_cast<size_t>(best)]->seq)) best = static_cast<int>(i);
        }
        if (best < 0) return nullptr;
        auto e = h.q[static_cast<size_t>(best)];
        if (e->kind == SchedEntry::K::Ttl) h.ttls--;
        h.q.erase(h.q.begin() + best);
        return e;
    }
    static double nextDue(Interpreter& I) {
        double d = kInf;
        if (I.hub_) for (auto& e : I.hub_->q) d = std::min(d, e->due);
        return d;
    }

    // ---------------------------------------------------------- حالة المؤقّت: قيم
    static double elapsedAt(const TimerState& t, double now) {
        double el = t.base + (t.st == St::Running ? now - t.segStart : 0);
        return el < 0 ? 0 : el;
    }
    static double clampEl(const TimerState& t, double el) { return (t.total > 0 && el > t.total) ? t.total : el; }
    static double valueFrom(const TimerState& t, double el) {
        el = clampEl(t, el);
        return std::round(t.up ? el : (t.total - el));
    }
    static const char* stName(St s) {
        switch (s) { case St::Idle: return "idle"; case St::Armed: return "armed"; case St::Running: return "running";
                     case St::Paused: return "paused"; case St::Done: return "done"; case St::Stopped: return "stopped"; }
        return "idle";
    }
    // نص مدّة: 01:30 · 1:02:03 · 2d 03:00:00 — العدّ التنازلي يُقرَّب لأعلى كي لا يُظهر 00:00 قبل الانتهاء
    static std::string clockText(double ms, bool ceilUp) {
        long long s = static_cast<long long>(ceilUp ? std::ceil(ms / 1000.0 - 1e-9) : std::floor(ms / 1000.0 + 1e-9));
        if (s < 0) s = 0;
        long long d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60, sec = s % 60;
        char buf[64];
        if (d > 0) std::snprintf(buf, sizeof buf, "%lldd %02lld:%02lld:%02lld", d, h, m, sec);
        else if (h > 0) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, sec);
        else std::snprintf(buf, sizeof buf, "%02lld:%02lld", m, sec);
        return buf;
    }

    // ---------------------------------------------------------- الكتابة والجدولة
    // يكتب قيمة الخلية (بلا عدّها «تعديلاً» للحاوية) ثم الأجراس ثم يحدّث كل val/dialog يقرؤها. true إن تغيّرت.
    static bool write(Interpreter& I, Environment* o, const std::string& name, const std::shared_ptr<LiveMeta>& m, const Value& nv, int line) {
        Value old;
        { auto it = o->values.find(name); if (it != o->values.end()) old = it->second; }
        if (valuesEqual(old, nv)) return false;
        m->writes++;
        const bool saved = I.stampSuppress_;
        I.stampSuppress_ = true;
        try { I.assignStateAware(o, name, nv, line); } catch (...) { I.stampSuppress_ = saved; throw; }
        I.stampSuppress_ = saved;
        if (!m->bells.empty()) I.liveFireBells(m, old, nv, line);
        if (!m->dependents.empty()) I.valPropagate(o, name, line);
        return true;
    }
    // يعيد جدولة المدخل التالي للمؤقّت (tick القادم أو نهاية الدورة أو نهاية التأخير).
    static void schedule(Interpreter& I, Environment* o, const std::string& name, const std::shared_ptr<LiveMeta>& m, int line) {
        TimerState& t = *m->timer;
        if (t.entryId) { remove(I, t.entryId); t.entryId = 0; }
        double now = clk::monoMs(), due;
        if (t.st == St::Armed) due = t.armedAt;
        else if (t.st == St::Running) {
            double el = elapsedAt(t, now), target = kInf;
            if (t.every > 0) target = static_cast<double>(t.ticks + 1) * t.every;
            if (t.total > 0 && t.total < target) target = t.total;
            if (std::isinf(target)) return;                         // ساعة إيقاف بلا tick: لا أحداث
            due = now + (target - el);
        } else return;
        auto e = std::make_shared<SchedEntry>();
        e->kind = SchedEntry::K::Timer; e->due = due; e->name = name; e->meta = m;
        e->env = o->weak_from_this();
        e->owner = containerKeyOf(I, o);
        e->label = (e->owner.empty() ? "" : e->owner + ".") + name;
        t.entryId = add(I, e, line);
    }
    static void callHandlers(Interpreter& I, const std::vector<Value>& hs, const Args& provided, int line) {
        std::vector<Value> copy = hs;
        for (auto& h : copy) {
            if (h.type != Value::Type::FUNCTION || !h.function) continue;
            size_t n = h.function->declaration ? h.function->declaration->params.size() : 0;
            Args a(provided.begin(), provided.begin() + static_cast<long>(std::min(n, provided.size())));
            I.callFunction(h.function, a, line);
        }
    }
    static Value ringInfo(Interpreter& I, const std::string& name, const TimerState& t, const std::string& reason, double elapsed, bool again) {
        (void)I;
        return mapOf({{"name", Value::string(name)}, {"reason", Value::string(reason)}, {"cycles", Value::num(static_cast<double>(t.cycles))},
                      {"ticks", Value::num(static_cast<double>(t.fires))}, {"elapsed", Value::num(std::round(elapsed))},
                      {"total", Value::num(t.total)}, {"again", Value::boolean_(again)}});
    }
    // نهاية دورة: عادية (reason=complete) أو مبكرة (until). elAt = ما انقضى عند الانتهاء.
    static void finish(Interpreter& I, Environment* o, const std::string& name, const std::shared_ptr<LiveMeta>& m,
                       const std::string& reason, double elAt, double now, int line) {
        TimerState& t = *m->timer;
        t.gen++;
        if (reason == "complete") write(I, o, name, m, Value::num(valueFrom(t, t.total)), line);
        t.cycles++;
        const bool again = reason == "complete" && (t.loop || t.cycles < t.times);
        t.reason = reason;
        Value info = ringInfo(I, name, t, reason, elAt, again);
        if (!again) {
            t.st = St::Done; t.base = elAt; t.hasFinished = true; t.finishedEpoch = clk::epochMs();
            t.entryId = 0;
        } else {
            t.base = std::max(0.0, elAt - t.total); t.segStart = now; t.ticks = 0;
        }
        const long g1 = t.gen;
        callHandlers(I, t.ringHandlers, {info}, line);
        if (t.gen != g1) return;                                     // المعالج غيّر المؤقّت (restart/stop...): فعله أعاد الجدولة
        if (again) {
            write(I, o, name, m, Value::num(valueFrom(t, t.base)), line);
            schedule(I, o, name, m, line);
        }
    }
    // المدخل المستحقّ لمؤقّت: بدء بعد التأخير، أو ticks، أو نهاية الدورة.
    static void fire(Interpreter& I, const std::shared_ptr<SchedEntry>& e, int line) {
        auto m = e->meta.lock(); auto env = e->env.lock();
        if (!m || !env || !m->timer || m->timer->entryId != e->id) return;     // مدخل قديم (أُلغي/أُعيد تعريفه)
        TimerState& t = *m->timer; Environment* o = env.get(); const std::string name = e->name;
        t.entryId = 0;
        const double now = clk::monoMs();
        if (t.st == St::Armed) {                                              // انتهى تأخير after: يبدأ العدّ من لحظة الاستحقاق نفسها
            t.st = St::Running; t.base = 0; t.ticks = 0; t.segStart = e->due; t.gen++;
            t.hasStarted = true; t.startedEpoch = clk::epochMs() - (now - e->due);
            schedule(I, o, name, m, line);
            return;
        }
        if (t.st != St::Running) return;
        const double el = elapsedAt(t, now);
        while (t.every > 0) {
            const double bt = static_cast<double>(t.ticks + 1) * t.every;
            if (bt > el + 1e-6) break;
            if (t.total > 0 && bt > t.total + 1e-6) break;
            t.ticks++; t.fires++;
            const double v = valueFrom(t, bt);
            write(I, o, name, m, Value::num(v), line);
            const long g0 = t.gen;
            callHandlers(I, t.tickHandlers, {Value::num(static_cast<double>(t.ticks)), Value::num(v)}, line);
            if (t.gen != g0 || t.st != St::Running) return;
            if (t.until) {
                EnvPtr scope = m->scope.lock();
                if (scope && I.evaluate(t.until, scope).isTruthy()) { finish(I, o, name, m, "until", bt, now, line); return; }
            }
        }
        if (t.total > 0 && el >= t.total - 1e-6) { finish(I, o, name, m, "complete", el, now, line); return; }
        schedule(I, o, name, m, line);
    }
    static void fireTtl(Interpreter& I, const std::shared_ptr<SchedEntry>& e, int line) {
        auto it = I.containers.find(e->container);
        if (it == I.containers.end()) return;
        Environment* env = it->second.get();
        auto fv = env->values.find(e->field);
        if (fv == env->values.end()) return;
        Value old = fv->second;
        I.assignStateAware(env, e->field, e->fallback, line);
        if (e->onExpire.type == Value::Type::FUNCTION && e->onExpire.function) {
            size_t n = e->onExpire.function->declaration ? e->onExpire.function->declaration->params.size() : 0;
            Args a; if (n >= 1) a.push_back(old); if (n >= 2) a.push_back(Value::string(e->container)); if (n >= 3) a.push_back(Value::string(e->field));
            I.callFunction(e->onExpire.function, a, line);
        }
    }
    static void fireDialogTimeout(Interpreter& I, const std::shared_ptr<SchedEntry>& e, int line) {
        auto m = e->meta.lock(); auto env = e->env.lock();
        if (!m || !env || m->dTimeoutId != e->id) return;
        m->dTimeoutId = 0;
        if (m->dOpen) I.dialogDoClose(env.get(), e->name, m, nullptr, line);       // انتهت المهلة: يُغلق بلا اختيار (reply يستلم nil)
    }
    static void run(Interpreter& I, const std::shared_ptr<SchedEntry>& e, int line) {
        I.hub().fired++;
        switch (e->kind) {
            case SchedEntry::K::Timer: fire(I, e, line); break;
            case SchedEntry::K::Ttl: fireTtl(I, e, line); break;
            case SchedEntry::K::DialogTimeout: fireDialogTimeout(I, e, line); break;
            case SchedEntry::K::Callback: {
                bool again = e->interval > 0 && (e->remaining < 0 || e->remaining > 1);
                Value fn = e->fn; Args args = e->args;
                if (again) { if (e->remaining > 0) e->remaining--; e->due += e->interval; add(I, e, line); }
                if (e->action) e->action(line); else I.callValue(fn, args, line);
                break;
            }
        }
    }
    static bool endless(const SchedEntry& e) {
        if (e.kind == SchedEntry::K::Callback) return e.interval > 0 && e.remaining < 0;
        if (e.kind == SchedEntry::K::Timer) {
            auto m = e.meta.lock();
            if (!m || !m->timer) return false;
            const TimerState& t = *m->timer;
            return t.loop || (t.total == 0 && t.every > 0);
        }
        return false;
    }

    // ---------------------------------------------------------- أفعال المؤقّت
    static void reqTimer(const std::shared_ptr<LiveMeta>& m) { (void)m; }
    static Value doAction(Interpreter& I, const std::string& action, Environment* o, const std::string& name,
                          const std::shared_ptr<LiveMeta>& m, const Value* arg, int line) {
        TimerState& t = *m->timer;
        const double now = clk::monoMs();
        const St before = t.st;
        auto startValue = [&]() { return Value::num(valueFrom(t, 0)); };
        auto begin = [&]() {                                                  // بداية تشغيل جديد من الصفر
            t.gen++; t.base = 0; t.ticks = 0; t.cycles = 0; t.laps.clear(); t.starts++;
            t.hasStarted = false; t.hasFinished = false; t.reason.clear();
            write(I, o, name, m, startValue(), line);
            if (t.delay > 0) { t.st = St::Armed; t.armedAt = now + t.delay; }
            else { t.st = St::Running; t.segStart = now; t.hasStarted = true; t.startedEpoch = clk::epochMs(); }
            schedule(I, o, name, m, line);
        };
        if (action == "start") {
            if (t.st == St::Running || t.st == St::Armed) return Value::boolean_(false);
            if (t.st == St::Paused) return doAction(I, "resume", o, name, m, nullptr, line);
            begin();
            return Value::boolean_(true);
        }
        if (action == "pause") {
            if (t.st != St::Running && t.st != St::Armed) return Value::boolean_(false);
            t.gen++;
            if (t.st == St::Running) { t.base = elapsedAt(t, now); t.wasArmed = false; }
            else { t.delayLeft = std::max(0.0, t.armedAt - now); t.wasArmed = true; }
            t.st = St::Paused;
            if (t.entryId) { remove(I, t.entryId); t.entryId = 0; }
            write(I, o, name, m, Value::num(valueFrom(t, t.base)), line);
            return Value::boolean_(true);
        }
        if (action == "resume") {
            if (t.st != St::Paused) return Value::boolean_(false);
            t.gen++;
            if (t.wasArmed) { t.st = St::Armed; t.armedAt = now + t.delayLeft; }
            else { t.st = St::Running; t.segStart = now; }
            schedule(I, o, name, m, line);
            return Value::boolean_(true);
        }
        if (action == "stop") {
            if (t.st != St::Running && t.st != St::Armed && t.st != St::Paused) return Value::boolean_(false);
            t.gen++;
            if (t.st == St::Running) t.base = elapsedAt(t, now);
            t.st = St::Stopped; t.reason = "stopped"; t.hasFinished = true; t.finishedEpoch = clk::epochMs();
            if (t.entryId) { remove(I, t.entryId); t.entryId = 0; }
            write(I, o, name, m, Value::num(valueFrom(t, t.base)), line);
            return Value::boolean_(true);
        }
        if (action == "reset") {
            t.gen++;
            if (t.entryId) { remove(I, t.entryId); t.entryId = 0; }
            t.st = St::Idle; t.base = 0; t.ticks = 0; t.cycles = 0; t.fires = 0; t.laps.clear(); t.reason.clear();
            t.hasStarted = false; t.hasFinished = false;
            write(I, o, name, m, startValue(), line);
            return Value::boolean_(before != St::Idle);
        }
        if (action == "restart") {
            doAction(I, "reset", o, name, m, nullptr, line);
            doAction(I, "start", o, name, m, nullptr, line);
            return Value::boolean_(true);
        }
        if (action == "lap") {
            if (t.st != St::Running && t.st != St::Paused) throw misuse(line, "`lap " + name + "`: the timer is " + stName(t.st) + ", a lap needs it running or paused", "start it first: `start " + name + ";`");
            const double el = clampEl(t, elapsedAt(t, now));
            const double prev = t.laps.empty() ? 0.0 : t.laps.back();
            t.laps.push_back(std::round(el));
            return Value::num(std::round(el - prev));
        }
        if (action == "extend") {
            if (!arg) throw misuse(line, "`extend " + name + "` needs a duration", "e.g. `extend " + name + " 10s;`");
            const double d = dur(*arg, line, "extend " + name, false);
            t.gen++;
            const double oldTotal = t.total;
            t.total += d;
            if (t.st == St::Done) {                                           // انتهى: التمديد يوقظه ليكمل بالمدّة المُضافة
                t.base = oldTotal; t.st = St::Running; t.segStart = now; t.hasFinished = false;
                if (t.cycles > 0) t.cycles--;
            }
            write(I, o, name, m, Value::num(valueFrom(t, elapsedAt(t, now))), line);
            if (t.st == St::Running) schedule(I, o, name, m, line);
            return Value::num(t.total);
        }
        throw misuse(line, "unknown timer action `" + action + "`");
    }

    static Value infoMap(Interpreter& I, Environment* o, const std::string& name, const std::shared_ptr<LiveMeta>& m, int line) {
        const TimerState& t = *m->timer;
        const double now = clk::monoMs();
        const double el = clampEl(t, elapsedAt(t, now));
        const double v = valueFrom(t, elapsedAt(t, now));
        const bool live = t.st == St::Running;
        std::vector<Value> laps; for (double l : t.laps) laps.push_back(Value::num(l));
        (void)o; (void)line;
        return mapOf({{"name", Value::string(name)}, {"kind", Value::string("timer")}, {"state", Value::string(stName(t.st))},
                      {"value", Value::num(v)}, {"left", Value::num(t.up ? std::max(0.0, t.total > 0 ? t.total - el : 0.0) : v)},
                      {"elapsed", Value::num(std::round(el))}, {"total", Value::num(t.total)},
                      {"progress", Value::num(t.total > 0 ? el / t.total : 0.0)},
                      {"up", Value::boolean_(t.up)}, {"loop", Value::boolean_(t.loop)}, {"every", Value::num(t.every)}, {"after", Value::num(t.delay)},
                      {"times", Value::num(t.loop ? 0 : static_cast<double>(t.times))}, {"cycles", Value::num(static_cast<double>(t.cycles))},
                      {"ticks", Value::num(static_cast<double>(t.fires))}, {"starts", Value::num(static_cast<double>(t.starts))},
                      {"laps", arr(laps)}, {"text", Value::string(clockText(v, !t.up))}, {"reason", t.reason.empty() ? Value::nil() : Value::string(t.reason)},
                      {"eta", (live && !t.up && t.total > 0) ? instant(clk::epochMs() + (t.total - el)) : Value::nil()},
                      {"running", Value::boolean_(live)}, {"done", Value::boolean_(t.st == St::Done)}});
        (void)I;
    }

    // ---------------------------------------------------------- نداءات المضيف
    static void advanceTo(Interpreter& I, double until, int line) {
        TimerHub& h = I.hub();
        if (h.depth >= 8) return;                                             // معالج يستدعي time.advance داخل معالج...: لا تعشيش بلا حدّ
        struct G { int& d; explicit G(int& x) : d(x) { ++d; } ~G() { --d; } } g(h.depth);
        long guard = 0;
        while (auto e = popDue(I, until)) {
            if (++guard > kMaxEvents) throw misuse(line, "the timers fired more than " + std::to_string(kMaxEvents) + " times in one step", "a timer with a tiny `every` over a long time? use a larger interval");
            if (clk::isVirtual()) clk::advanceMonoTo(e->due);
            run(I, e, line);
        }
        if (clk::isVirtual()) clk::advanceMonoTo(until);
    }
    static void sleepReal(double ms) {
        double left = ms;
        while (left > 0) { double c = std::min(left, 50.0); std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(c * 1000))); left -= c; }
    }
    static void sleepFor(Interpreter& I, double ms, int line) {
        if (clk::isVirtual()) { advanceTo(I, clk::monoMs() + ms, line); return; }
        if (!I.timersPending()) { sleepReal(ms); return; }
        const double end = clk::monoMs() + ms;
        while (true) {
            const double now = clk::monoMs();
            if (now >= end) break;
            sleepReal(std::min(end - now, 20.0));
            advanceTo(I, clk::monoMs(), line);
        }
        advanceTo(I, clk::monoMs(), line);
    }
    static long runFor(Interpreter& I, double ms, bool untilEmpty, int line) {
        TimerHub& h = I.hub();
        const long fired0 = h.fired;
        if (untilEmpty) {
            for (auto& e : h.q) if (endless(*e))
                throw misuse(line, "`timers.run()` would never end: `" + e->label + "` repeats forever", "give it a time limit: `timers.run(5s)`, or stop the repeating timer");
        }
        const double start = clk::monoMs();
        const double end = untilEmpty ? kInf : start + ms;
        long guard = 0;
        while (true) {
            const double nd = nextDue(I);
            if (std::isinf(nd) || nd > end) break;
            if (++guard > kMaxEvents) throw misuse(line, "the timers fired more than " + std::to_string(kMaxEvents) + " times", "use a larger interval or a shorter duration");
            if (clk::isVirtual()) advanceTo(I, nd, line);
            else {
                if (nd - clk::monoMs() > 600000.0) throw misuse(line, "`timers.run` will not wait more than 10 minutes in real time", "freeze the clock with `time.freeze()` to run long schedules instantly");
                sleepFor(I, std::max(0.0, nd - clk::monoMs()), line);
            }
        }
        if (!untilEmpty) {
            if (clk::isVirtual()) advanceTo(I, end, line);
            else sleepFor(I, std::max(0.0, end - clk::monoMs()), line);
        }
        return h.fired - fired0;
    }

    // ---------------------------------------------------------- تحديد الهدف
    static void locateIn(Interpreter& I, const std::string& container, const std::string& name, int line, Environment*& owner, std::shared_ptr<LiveMeta>& m) {
        std::string cn = I.canonContainer(container);
        auto it = I.containers.find(cn);
        if (it == I.containers.end())
            throw I.errWithReason(diag::Code::E0014_InvalidContainer, line, "'" + cn + "' is not a known container",
                                  "create it first via `@container=" + cn + "` or `spawn(kind, \"" + cn + "\")`");
        owner = it->second.get();
        auto lit = owner->live.find(name);
        if (lit == owner->live.end() || !lit->second || !lit->second->timer)
            throw misuse(line, "`" + name + "` is not a timer of container `" + cn + "`", "declare it inside the container with `timer " + name + " = 30s;`");
        m = lit->second;
    }
    static void locate(Interpreter& I, const EnvPtr& env, const std::string& path, int line, Environment*& owner, std::string& name, std::shared_ptr<LiveMeta>& m) {
        size_t dot = path.rfind('.');
        if (dot != std::string::npos) { name = path.substr(dot + 1); locateIn(I, path.substr(0, dot), name, line, owner, m); return; }
        name = path;
        owner = I.findLiveOwner(env, path);
        if (!owner) throw I.undefinedVariableErr(path, line, env);
        auto lit = owner->live.find(path);
        if (lit == owner->live.end() || !lit->second || !lit->second->timer)
            throw misuse(line, "`" + path + "` is not a timer (it is " + (lit != owner->live.end() && lit->second ? "a " + lit->second->kind : std::string("a plain variable")) + ")",
                         "declare it with `timer " + path + " = 30s;`");
        m = lit->second;
    }
    static std::vector<std::pair<std::string, std::shared_ptr<LiveMeta>>> timersOf(Environment* env) {
        std::vector<std::pair<std::string, std::shared_ptr<LiveMeta>>> out;
        for (auto& kv : env->live) if (kv.second && kv.second->timer) out.push_back({kv.first, kv.second});
        std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first < b.first; });
        return out;
    }
    static Environment* containerEnv(Interpreter& I, const Value& v, const char* fn, int line, std::string* keyOut = nullptr) {
        if (v.type != Value::Type::STRING) throw badTyp(line, std::string(fn) + ": the container name must be text, got " + v.typeName());
        std::string cn = I.canonContainer(v.str);
        auto it = I.containers.find(cn);
        if (it == I.containers.end())
            throw I.errWithReason(diag::Code::E0014_InvalidContainer, line, "'" + cn + "' is not a known container",
                                  "create it first via `@container=" + cn + "` or `spawn(kind, \"" + cn + "\")`");
        if (keyOut) *keyOut = cn;
        return it->second.get();
    }
};

// ------------------------------------------------------------------ واجهة Interpreter
void Interpreter::timersPump(int line) { if (hub_ && !hub_->q.empty()) TimerOps::advanceTo(*this, clk::monoMs(), line); }
void Interpreter::timersAdvanceTo(double untilMono, int line) { TimerOps::advanceTo(*this, untilMono, line); }
void Interpreter::timersRunFor(double ms, bool untilEmpty, int line) { TimerOps::runFor(*this, ms, untilEmpty, line); }
void Interpreter::timersSleep(double ms, int line) { TimerOps::sleepFor(*this, ms, line); }

void Interpreter::schedCancelOwner(const Environment* env) {
    if (!hub_) return;
    auto& q = hub_->q;
    for (size_t i = 0; i < q.size();) {
        auto e = q[i];
        auto ev = e->env.lock();
        if ((ev && ev.get() == env) || (!ev && (e->kind == SchedEntry::K::Timer || e->kind == SchedEntry::K::DialogTimeout))) {
            if (auto m = e->meta.lock()) { if (m->timer && m->timer->entryId == e->id) m->timer->entryId = 0; if (m->dTimeoutId == e->id) m->dTimeoutId = 0; }
            q.erase(q.begin() + static_cast<long>(i));
        } else ++i;
    }
}

void Interpreter::noteContainerBorn(const std::string& key) {
    ContainerStamp& s = containerStamp_[key];
    s.born = s.updated = clk::epochMs(); s.writes = 0;
}
void Interpreter::noteContainerWrite(const std::string& key, const std::string& field) {
    ContainerStamp& s = containerStamp_[key];
    if (s.born == 0) s.born = clk::epochMs();
    s.updated = clk::epochMs(); s.writes++;
    if (hub_ && hub_->ttls > 0) {                                     // كتابة جديدة على حقل له صلاحية تُلغيها (كـ SET في Redis)
        for (size_t i = 0; i < hub_->q.size(); ++i) {
            auto& e = hub_->q[i];
            if (e->kind == SchedEntry::K::Ttl && e->container == key && e->field == field) { TimerOps::remove(*this, e->id); break; }
        }
    }
}

// تهيئة timer عند التصريح (من liveExecDecl): يقيّم المدّة/الخطوة/التأخير/عدد الدورات ويحسب القيمة الابتدائية.
void Interpreter::timerSetup(const std::shared_ptr<LiveDeclStmt>& s, const EnvPtr& env, const std::shared_ptr<LiveMeta>& m, Value& initial) {
    m->kind = "timer"; m->timer = std::make_shared<TimerState>(); m->scope = env;
    TimerState& t = *m->timer;
    auto ev = [&](const ExprPtr& e, const char* what, bool zeroOk) -> double {
        Value v = evaluate(e, env);
        return TimerOps::dur(v, s->line, "timer `" + s->name + "` " + what, zeroOk);
    };
    t.up = s->tUp; t.loop = s->tLoop; t.until = s->tUntil;
    t.total = s->initializer ? ev(s->initializer, "duration", t.up) : 0;
    if (!t.up && t.total <= 0) throw TimerOps::misuse(s->line, "timer `" + s->name + "` needs a duration greater than zero", "e.g. `timer " + s->name + " = 30s;`");
    t.every = s->tEvery ? ev(s->tEvery, "`every` step", false) : 0;
    t.delay = s->tAfter ? ev(s->tAfter, "`after` delay", true) : 0;
    if (s->tTimes) {
        Value v = evaluate(s->tTimes, env);
        if (v.type != Value::Type::NUMBER || v.number < 1 || v.number > 1000000 || v.number != std::floor(v.number))
            throw TimerOps::misuse(s->line, "`times` of timer `" + s->name + "` must be a whole number from 1 to 1000000, got " + v.toDisplayString());
        t.times = static_cast<long>(v.number);
    }
    if (t.every > 0 && t.total > 0 && t.total / t.every > 1e6)
        throw TimerOps::misuse(s->line, "timer `" + s->name + "` would tick more than a million times per cycle", "use a larger `every`");
    initial = Value::num(TimerOps::valueFrom(t, 0));
}
void Interpreter::timerBegin(Environment* owner, const std::string& name, const std::shared_ptr<LiveMeta>& m, int line) {
    TimerOps::doAction(*this, "start", owner, name, m, nullptr, line);
}
Value Interpreter::timerAction(const std::string& action, Environment* owner, const std::string& name, const std::shared_ptr<LiveMeta>& m, const Value* arg, int line) {
    return TimerOps::doAction(*this, action, owner, name, m, arg, line);
}
Value Interpreter::timerNow(const std::shared_ptr<LiveMeta>& m) const {
    const TimerState& t = *m->timer;
    return Value::num(TimerOps::valueFrom(t, TimerOps::elapsedAt(t, clk::monoMs())));
}
Value Interpreter::timerInfoMap(Environment* owner, const std::string& name, const std::shared_ptr<LiveMeta>& m, int line) {
    return TimerOps::infoMap(*this, owner, name, m, line);
}
void Interpreter::timerLocate(const EnvPtr& env, const std::string& path, int line, Environment*& owner, std::string& name, std::shared_ptr<LiveMeta>& m) {
    TimerOps::locate(*this, env, path, line, owner, name, m);
}
void Interpreter::timerAddHandler(const std::string& path, const std::string& role, const EnvPtr& env, const Value& fn, int line) {
    Environment* o = nullptr; std::string nm; std::shared_ptr<LiveMeta> m;
    TimerOps::locate(*this, env, path, line, o, nm, m);
    (role == "tick" ? m->timer->tickHandlers : m->timer->ringHandlers).push_back(fn);
}

// خصائص المؤقّت (t.left · t.state · t.text ...). false إن لم تكن خاصية مؤقّت (تكمل المعالجة العامة).
bool Interpreter::timerProp(Environment* owner, const std::string& name, const std::shared_ptr<LiveMeta>& m, const std::string& prop, Value& out, int line) {
    using St = TimerState::St;
    const TimerState& t = *m->timer;
    const double now = clk::monoMs();
    const double rawEl = TimerOps::elapsedAt(t, now);
    const double el = TimerOps::clampEl(t, rawEl);
    const double v = TimerOps::valueFrom(t, rawEl);
    const bool running = t.st == St::Running;
    if (prop == "info") { out = TimerOps::infoMap(*this, owner, name, m, line); return true; }
    if (prop == "left")      { out = Value::num(t.total > 0 ? std::max(0.0, std::round(t.total - el)) : 0.0); return true; }
    if (prop == "elapsed")   { out = Value::num(std::round(el)); return true; }
    if (prop == "total")     { out = Value::num(t.total); return true; }
    if (prop == "progress")  { out = Value::num(t.total > 0 ? el / t.total : 0.0); return true; }
    if (prop == "percent")   { out = Value::num(t.total > 0 ? std::round(el / t.total * 10000.0) / 100.0 : 0.0); return true; }
    if (prop == "state")     { out = Value::string(TimerOps::stName(t.st)); return true; }
    if (prop == "running")   { out = Value::boolean_(running); return true; }
    if (prop == "paused")    { out = Value::boolean_(t.st == St::Paused); return true; }
    if (prop == "armed")     { out = Value::boolean_(t.st == St::Armed); return true; }
    if (prop == "stopped")   { out = Value::boolean_(t.st == St::Stopped); return true; }
    if (prop == "done")      { out = Value::boolean_(t.st == St::Done); return true; }
    if (prop == "idle")      { out = Value::boolean_(t.st == St::Idle); return true; }
    if (prop == "ready")     { out = Value::boolean_(t.st == St::Idle || t.st == St::Done || t.st == St::Stopped); return true; }
    if (prop == "fires")     { out = Value::num(static_cast<double>(t.fires)); return true; }
    if (prop == "cycles")    { out = Value::num(static_cast<double>(t.cycles)); return true; }
    if (prop == "starts")    { out = Value::num(static_cast<double>(t.starts)); return true; }
    if (prop == "every")     { out = Value::num(t.every); return true; }
    if (prop == "laps")      { std::vector<Value> l; for (double x : t.laps) l.push_back(Value::num(x)); out = TimerOps::arr(l); return true; }
    if (prop == "text")      { out = Value::string(TimerOps::clockText(v, !t.up)); return true; }
    if (prop == "reason")    { out = t.reason.empty() ? Value::nil() : Value::string(t.reason); return true; }
    if (prop == "eta")       { out = (running && !t.up && t.total > 0) ? TimerOps::instant(clk::epochMs() + (t.total - el)) : Value::nil(); return true; }
    if (prop == "started")   { out = t.hasStarted ? TimerOps::instant(t.startedEpoch) : Value::nil(); return true; }
    if (prop == "finished")  { out = t.hasFinished ? TimerOps::instant(t.finishedEpoch) : Value::nil(); return true; }
    return false;
}

// حوار: timeout DUR — يُفتح فيُجدول إغلاقاً تلقائياً، ويُلغى عند أي إغلاق.
void Interpreter::dialogTimeoutArm(Environment* owner, const std::string& name, const std::shared_ptr<LiveMeta>& m, int line) {
    dialogTimeoutDisarm(m);
    if (!m->dTimeout) return;
    EnvPtr scope = m->scope.lock();
    if (!scope) return;
    const double ms = TimerOps::dur(evaluate(m->dTimeout, scope), line, "dialog `" + name + "` timeout", false);
    auto e = std::make_shared<SchedEntry>();
    e->kind = SchedEntry::K::DialogTimeout; e->due = clk::monoMs() + ms; e->name = name; e->meta = m;
    e->env = owner->weak_from_this(); e->owner = TimerOps::containerKeyOf(*this, owner);
    e->label = (e->owner.empty() ? "" : e->owner + ".") + name + " (timeout)";
    m->dTimeoutId = TimerOps::add(*this, e, line);
}
void Interpreter::dialogTimeoutDisarm(const std::shared_ptr<LiveMeta>& m) {
    if (m->dTimeoutId) { TimerOps::remove(*this, m->dTimeoutId); m->dTimeoutId = 0; }
}

// ------------------------------------------------------------------ الدوال الأصلية
void Interpreter::registerNativesTimer() {
    using TO = TimerOps;
    using Args = std::vector<Value>;
    auto argc = [this](const char* fn, const Args& a, size_t lo, size_t hi, int line) {
        if (a.size() < lo || a.size() > hi)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "' expects " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) + " argument(s) but got " + std::to_string(a.size()));
    };

    // ---- api ✕ indsin: كل استدعاء api (متزامن أو مجدوَل) ينشر حالته في حقول الحاوية المعرَّفة فيها: data · status · error · updated · loading ----
    //   (فتقرؤها خصائص الواجهة مباشرة: <Text text=<<shop.status>> />). غير المعرَّف من هذه الحقول يُتجاهل. المرحلة nil = بدء الطلب (loading=true).
    auto apiPublish = std::make_shared<std::function<void(const std::string&, const Value&, bool)>>([this](const std::string& key, const Value& res, bool begin) {
        auto it = containers.find(key);
        if (it == containers.end() || !it->second) return;
        auto& vals = it->second->values;
        auto set = [&](const char* f, const Value& v) { auto x = vals.find(f); if (x != vals.end()) x->second = v; };
        if (begin) { set("loading", Value::boolean_(true)); return; }
        set("loading", Value::boolean_(false));
        if (res.type != Value::Type::MAP || !res.map) return;
        Value data = Value::nil(), status = Value::nil(), error = Value::nil(); bool ok = false, haveJson = false;
        for (auto& kv : *res.map) {
            const std::string k = kv.first.toDisplayString();
            if (k == "json") { data = kv.second; haveJson = true; } else if (k == "body" && !haveJson) data = kv.second;
            else if (k == "status") status = kv.second; else if (k == "ok") ok = kv.second.isTruthy();
            else if (k == "error" && kv.second.type == Value::Type::STRING && !kv.second.str.empty()) error = kv.second;
        }
        // صورة معاينة: ردّ صورة ⇒ data = data URI؛ وإلا أول رابط/URI في json (image · thumbnail · avatar · icon · picture · photo · preview) أو جسم mock نصّي data:image/
        Value preview = Value::nil();
        for (auto& kv : *res.map) if (kv.first.toDisplayString() == "image" && kv.second.type == Value::Type::STRING) { preview = kv.second; data = kv.second; }
        if (preview.type == Value::Type::NIL) {
            auto urlish = [](const Value& v) { return v.type == Value::Type::STRING && (v.str.rfind("http://", 0) == 0 || v.str.rfind("https://", 0) == 0 || v.str.rfind("data:image/", 0) == 0); };
            if (urlish(data)) { const std::string& d = data.str; if (d.rfind("data:image/", 0) == 0) preview = data; }
            else {
                // خريطة، أو أول عنصر خريطة في مصفوفة؛ وإن لم يُوجد مفتاح صورة مباشر فابحث في `owner`/`user`/`author` (مثل GitHub: owner.avatar_url)
                const Value* m = &data;
                if (data.type == Value::Type::ARRAY && data.array && !data.array->empty()) m = &(*data.array)[0];
                if (m->type == Value::Type::MAP && m->map) {
                    auto scan = [&](const Value& mv) {
                        for (const char* k : {"preview", "image", "thumbnail", "avatar", "icon", "picture", "photo", "avatar_url", "image_url", "thumbnail_url", "icon_url"}) {
                            for (auto& kv : *mv.map) if (kv.first.toDisplayString() == k && urlish(kv.second)) { preview = kv.second; return; }
                        }
                    };
                    scan(*m);
                    if (preview.type == Value::Type::NIL) for (auto& kv : *m->map) { const std::string k = kv.first.toDisplayString(); if ((k == "owner" || k == "user" || k == "author") && kv.second.type == Value::Type::MAP && kv.second.map) { scan(kv.second); if (preview.type != Value::Type::NIL) break; } }
                }
            }
        }
        long st = status.type == Value::Type::NUMBER ? static_cast<long>(status.number) : 0;
        const bool good = ok && st >= 200 && st < 300;            // «ok» = تمّ الاتصال؛ النجاح الفعلي = 2xx
        if (!good && error.type == Value::Type::NIL && st >= 300) {   // خطأ HTTP: رسالة الخادم (json.message) أو "HTTP <status>"
            std::string msg = "HTTP " + std::to_string(st);
            for (auto& kv : *res.map) if ((kv.first.toDisplayString() == "json" || kv.first.toDisplayString() == "body") && kv.second.type == Value::Type::MAP && kv.second.map)
                for (auto& m : *kv.second.map) if (m.first.toDisplayString() == "message" && m.second.type == Value::Type::STRING) msg += ": " + m.second.str;
            error = Value::string(msg);
        }
        if (good) { set("data", data); set("preview", preview); error = Value::nil(); }     // الفشل يُبقي آخر بيانات ناجحة (stale) ويضع error
        // أيقونة: إن عرّفت الحاوية الحقل icon (أو svg) والردّ يحوي SVG/مسار/data URI صالحاً ⇒ يُسجَّل في سجلّ indsin
        //   ويُملأ icon باسمه (iconName إن عُرّف، وإلا name/id من الردّ، وإلا اسم الحاوية) وsvg بوسم SVG كامل — فيعمل <Icon icon=<<c.icon>> />.
        if (good && vals.count("iconList")) {        // حزمة كاملة: iconList ← أسماء ما سُجِّل (iconPrefix اختيارية)
            std::string pre; auto ip = vals.find("iconPrefix"); if (ip != vals.end() && ip->second.type == Value::Type::STRING) pre = ip->second.str;
            std::vector<Value> names; for (auto& n : iconPackImport(data, pre)) names.push_back(Value::string(n));
            if (!names.empty()) set("iconList", Value::makeArray(std::make_shared<ArrayData>(std::move(names))));
        }
        if (good && (vals.count("icon") || vals.count("svg"))) {
            std::string src, nm, gl;
            auto inm = vals.find("iconName"); if (inm != vals.end() && inm->second.type == Value::Type::STRING) nm = inm->second.str;
            auto ig = vals.find("iconGlyph"); if (ig != vals.end() && ig->second.type == Value::Type::STRING) gl = ig->second.str;
            std::string rn, rg; if (iconSourceOf(data, src, rn, rg)) {
                if (nm.empty()) nm = rn.empty() ? key : rn;
                if (gl.empty()) gl = rg;
                if (indsin::iconRegistry().importSource(nm, src, gl)) { set("icon", Value::string(nm)); set("svg", Value::string(indsin::iconRegistry().svg(nm))); }
            }
        }
        set("status", status); set("error", error); set("updated", Value::num(clk::epochMs()));
    });

    // ---- api ✕ timer: callApi(c, method, path, opts) و call(method, path, opts) — خيارات اختيارية بلا كلمات جديدة ----
    //   cache: مدّة  → يعيد آخر ردّ ناجح للمسار نفسه (مع cached:true) حتى تنتهي المدّة؛ cache: 0 يتجاوز الكاش ويحذفه
    //   after / at / every [+ times] → يجدوِل الاستدعاء (ملك الحاوية فيُلغى بحذفها أو timers.clear)؛ يعيد {ok, scheduled, id, kind:"api", target, in}
    //   then: دالة(ردّ) → تُستدعى بالردّ بعد كل استدعاء (فيصير every+then استطلاعاً دورياً poll)
    {
        auto cache = std::make_shared<std::unordered_map<std::string, std::pair<double, Value>>>();
        auto wrap = [this, cache, apiPublish](const char* fnName, size_t baseArgs) {
            auto base = natives[fnName];
            const std::string fn = fnName;
            natives[fnName] = [this, cache, apiPublish, base, fn, baseArgs](Args& a, int line) -> Value {
                if (a.size() != baseArgs + 1 || a.back().type == Value::Type::NIL) { if (a.size() == baseArgs + 1) a.pop_back(); return base(a, line); }   // بلا خيارات = السلوك القديم حرفياً
                Value opts = a.back(); a.pop_back();
                if (opts.type != Value::Type::MAP || !opts.map) throw TO::badArg(line, fn + ": options must be a map but got " + opts.typeName());
                Value vCache, vAfter, vAt, vEvery, vTimes, vThen;
                for (auto& kv : *opts.map) {
                    const std::string k = kv.first.toDisplayString();
                    if (k == "cache") vCache = kv.second; else if (k == "after") vAfter = kv.second; else if (k == "at") vAt = kv.second;
                    else if (k == "every") vEvery = kv.second; else if (k == "times") vTimes = kv.second; else if (k == "then") vThen = kv.second;
                    else throw diagErr(diag::Code::E0007_InvalidArguments, line, fn + ": unknown option '" + k + "' (allowed: cache, after, at, every, times, then)");
                }
                const bool N = true; (void)N;
                auto isNil = [](const Value& v) { return v.type == Value::Type::NIL; };
                if (!isNil(vAfter) && !isNil(vAt)) throw TO::badArg(line, fn + ": use `after` or `at`, not both");
                if (!isNil(vTimes) && isNil(vEvery)) throw TO::badArg(line, fn + ": `times` needs `every`");
                if (!isNil(vThen) && vThen.type != Value::Type::FUNCTION) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": `then` must be a function");
                std::string key, method, path;
                if (fn == "call") { key = containerStack.empty() ? "" : containerStack.back(); method = a[0].str; path = a[1].str; }
                else { key = a[0].str; method = a[1].str; path = a[2].str; }
                if (a.size() != baseArgs || (fn == "call" ? (a[0].type != Value::Type::STRING || a[1].type != Value::Type::STRING) : (a[0].type != Value::Type::STRING || a[1].type != Value::Type::STRING || a[2].type != Value::Type::STRING)))
                    return base(a, line);   // وسائط خاطئة: دع الأصل يرمي رسالته المعتادة
                double cacheMs = -1;
                if (!isNil(vCache)) cacheMs = TO::dur(vCache, line, fn + " `cache`", true);
                Value thenFn = vThen;
                auto doCall = [this, cache, apiPublish, base, key, method, path, cacheMs, thenFn](int ln) -> Value {
                    const std::string ck = key + "|" + toUpperAscii(method) + "|" + path;
                    Value res; bool hit = false;
                    if (cacheMs == 0) cache->erase(ck);
                    else if (cacheMs > 0) {
                        auto it = cache->find(ck);
                        if (it != cache->end() && clk::monoMs() < it->second.first) { res = it->second.second; hit = true; }
                    }
                    if (!hit) {
                        res = performApiCall(key, method, path, ln);
                        if (cacheMs > 0 && res.type == Value::Type::MAP) {
                            bool ok = false; for (auto& kv : *res.map) if (kv.first.toDisplayString() == "ok") ok = kv.second.isTruthy();
                            if (ok) (*cache)[ck] = {clk::monoMs() + cacheMs, res};
                        }
                    } else {
                        auto m = std::make_shared<MapData>(*res.map); m->push_back({Value::string("cached"), Value::boolean_(true)}); res = Value::makeMap(m);
                    }
                    (*apiPublish)(key, res, false);
                    if (thenFn.type == Value::Type::FUNCTION) { std::vector<Value> ta{res}; callValue(thenFn, ta, ln); }
                    return res;
                };
                if (!isNil(vAfter) || !isNil(vAt) || !isNil(vEvery)) {
                    double delay = 0, interval = 0; long times = 1;
                    if (!isNil(vEvery)) { interval = TO::dur(vEvery, line, fn + " `every`", false); times = -1; delay = interval; }
                    if (!isNil(vAfter)) delay = TO::dur(vAfter, line, fn + " `after`", true);
                    if (!isNil(vAt)) delay = timerAtArg(vAt, line, fn + " `at`");
                    if (!isNil(vTimes)) {
                        if (vTimes.type != Value::Type::NUMBER || vTimes.number < 1 || vTimes.number != std::floor(vTimes.number)) throw TO::badArg(line, fn + ": `times` must be a whole number >= 1");
                        times = static_cast<long>(vTimes.number);
                    }
                    long id = schedAction(delay, interval, times, [doCall](int ln) { doCall(ln); }, "api " + toUpperAscii(method) + " " + path, key);
                    auto m = std::make_shared<MapData>();
                    m->push_back({Value::string("ok"), Value::boolean_(true)}); m->push_back({Value::string("scheduled"), Value::boolean_(true)});
                    m->push_back({Value::string("id"), Value::num(static_cast<double>(id))}); m->push_back({Value::string("kind"), Value::string("api")});
                    m->push_back({Value::string("target"), Value::string(toUpperAscii(method) + " " + path)}); m->push_back({Value::string("in"), Value::num(std::round(delay))});
                    return Value::makeMap(m);
                }
                return doCall(line);
            };
        };
        wrap("callApi", 3);
        wrap("call", 2);
    }

    // ---- api حقيقي ✕ timer: apiGet/apiPost/apiPut/apiPatch/apiDelete/apiCall + opts اختياري أخير ----
    //   cache: مدّة (GET فقط) · after/at/every/times · then: fn(ردّ)  — كما في callApi أعلاه
    //   retry: n [+ backoff: مدّة] → يعيد المحاولة عند الفشل (ok=false أو status>=500)، وبين المحاولات ينام بـ time.sleep
    //     (فتتقدّم الساعة المُجمَّدة وتُطلَق المؤقّتات المستحقّة) ويضيف الردّ `attempts`
    //   timeout: مدّة → مهلة هذا الطلب وحده (بدل httpSetTimeout العامة)
    {
        auto cache = std::make_shared<std::unordered_map<std::string, std::pair<double, Value>>>();
        struct Shape { const char* fn; size_t baseArgs; const char* method; int pathIdx; int bodyIdx; };   // method=nullptr: من الوسيط 1
        auto wrapReal = [this, cache, apiPublish](Shape sh) {
            auto base = natives[sh.fn];
            const std::string fn = sh.fn;
            natives[sh.fn] = [this, cache, apiPublish, base, fn, sh](Args& a, int line) -> Value {
                if (a.size() != sh.baseArgs + 1 || a.back().type == Value::Type::NIL) { if (a.size() == sh.baseArgs + 1) a.pop_back(); return base(a, line); }   // بلا خيارات = السلوك القديم
                // آخر وسيط خريطة خيارات؟ للأفعال ذات الجسم (POST…) قد يكون الجسم نفسه خريطة، لكن عدد الوسائط يحسم: baseArgs+1
                Value opts = a.back(); a.pop_back();
                if (opts.type != Value::Type::MAP || !opts.map) throw TO::badArg(line, fn + ": options must be a map but got " + opts.typeName());
                Value vCache, vAfter, vAt, vEvery, vTimes, vThen, vRetry, vBackoff, vTimeout;
                for (auto& kv : *opts.map) {
                    const std::string k = kv.first.toDisplayString();
                    if (k == "cache") vCache = kv.second; else if (k == "after") vAfter = kv.second; else if (k == "at") vAt = kv.second;
                    else if (k == "every") vEvery = kv.second; else if (k == "times") vTimes = kv.second; else if (k == "then") vThen = kv.second;
                    else if (k == "retry") vRetry = kv.second; else if (k == "backoff") vBackoff = kv.second; else if (k == "timeout") vTimeout = kv.second;
                    else throw diagErr(diag::Code::E0007_InvalidArguments, line, fn + ": unknown option '" + k + "' (allowed: cache, after, at, every, times, then, retry, backoff, timeout)");
                }
                auto isNil = [](const Value& v) { return v.type == Value::Type::NIL; };
                if (!isNil(vAfter) && !isNil(vAt)) throw TO::badArg(line, fn + ": use `after` or `at`, not both");
                if (!isNil(vTimes) && isNil(vEvery)) throw TO::badArg(line, fn + ": `times` needs `every`");
                if (!isNil(vBackoff) && isNil(vRetry)) throw TO::badArg(line, fn + ": `backoff` needs `retry`");
                if (!isNil(vThen) && vThen.type != Value::Type::FUNCTION) throw diagErr(diag::Code::E0004_InvalidType, line, fn + ": `then` must be a function");
                if (a.size() <= static_cast<size_t>(sh.pathIdx) || a[0].type != Value::Type::STRING || a[sh.pathIdx].type != Value::Type::STRING) return base(a, line);   // وسائط خاطئة: رسالة الأصل
                std::string method = sh.method ? sh.method : (a[1].type == Value::Type::STRING ? a[1].str : "");
                if (method.empty()) return base(a, line);
                const std::string name = a[0].str, path = a[sh.pathIdx].str;
                const Value body = sh.bodyIdx >= 0 ? a[sh.bodyIdx] : Value::nil();
                double cacheMs = -1, backoffMs = 0, timeoutMs = 0; long retry = 0;
                if (!isNil(vCache)) cacheMs = TO::dur(vCache, line, fn + " `cache`", true);
                if (!isNil(vBackoff)) backoffMs = TO::dur(vBackoff, line, fn + " `backoff`", true);
                if (!isNil(vTimeout)) timeoutMs = TO::dur(vTimeout, line, fn + " `timeout`", false);
                if (!isNil(vRetry)) {
                    if (vRetry.type != Value::Type::NUMBER || vRetry.number < 0 || vRetry.number != std::floor(vRetry.number) || vRetry.number > 20) throw TO::badArg(line, fn + ": `retry` must be a whole number from 0 to 20");
                    retry = static_cast<long>(vRetry.number);
                }
                const bool hasRetry = !isNil(vRetry);
                const std::string M = toUpperAscii(method);
                if (cacheMs >= 0 && M != "GET") throw TO::badArg(line, fn + ": `cache` is for GET only (a " + M + " changes data)");
                Value thenFn = vThen;
                auto doCall = [this, cache, apiPublish, name, M, path, body, cacheMs, backoffMs, timeoutMs, retry, hasRetry, thenFn](int ln) -> Value {
                    const std::string ck = "real|" + name + "|" + path;
                    Value res; bool hit = false;
                    if (cacheMs == 0) cache->erase(ck);
                    else if (cacheMs > 0) {
                        auto it = cache->find(ck);
                        if (it != cache->end() && clk::monoMs() < it->second.first) { res = it->second.second; hit = true; }
                    }
                    auto okOf = [](const Value& r) { bool ok = false; long st = 0; if (r.type == Value::Type::MAP) for (auto& kv : *r.map) { const std::string k = kv.first.toDisplayString(); if (k == "ok") ok = kv.second.isTruthy(); else if (k == "status" && kv.second.type == Value::Type::NUMBER) st = static_cast<long>(kv.second.number); } return ok && st < 500; };
                    if (!hit) {
                        (*apiPublish)(name, Value::nil(), true);                 // loading = true
                        const int savedTimeout = defaultHttpTimeoutMs;
                        if (timeoutMs > 0) defaultHttpTimeoutMs = static_cast<int>(timeoutMs);
                        long attempts = 0;
                        try {
                            for (;;) {
                                ++attempts;
                                res = performRealApiCall(name, M, path, body, ln);
                                if (okOf(res) || attempts > retry) break;
                                if (backoffMs > 0) timersSleep(backoffMs, ln);
                            }
                        } catch (...) { defaultHttpTimeoutMs = savedTimeout; (*apiPublish)(name, Value::nil(), false); throw; }
                        defaultHttpTimeoutMs = savedTimeout;
                        if (hasRetry && res.type == Value::Type::MAP) { auto m = std::make_shared<MapData>(*res.map); m->push_back({Value::string("attempts"), Value::num(static_cast<double>(attempts))}); res = Value::makeMap(m); }
                        if (cacheMs > 0 && okOf(res)) (*cache)[ck] = {clk::monoMs() + cacheMs, res};
                    } else {
                        auto m = std::make_shared<MapData>(*res.map); m->push_back({Value::string("cached"), Value::boolean_(true)}); res = Value::makeMap(m);
                    }
                    (*apiPublish)(name, res, false);
                    if (thenFn.type == Value::Type::FUNCTION) { std::vector<Value> ta{res}; callValue(thenFn, ta, ln); }
                    return res;
                };
                if (!isNil(vAfter) || !isNil(vAt) || !isNil(vEvery)) {
                    double delay = 0, interval = 0; long times = 1;
                    if (!isNil(vEvery)) { interval = TO::dur(vEvery, line, fn + " `every`", false); times = -1; delay = interval; }
                    if (!isNil(vAfter)) delay = TO::dur(vAfter, line, fn + " `after`", true);
                    if (!isNil(vAt)) delay = timerAtArg(vAt, line, fn + " `at`");
                    if (!isNil(vTimes)) {
                        if (vTimes.type != Value::Type::NUMBER || vTimes.number < 1 || vTimes.number != std::floor(vTimes.number)) throw TO::badArg(line, fn + ": `times` must be a whole number >= 1");
                        times = static_cast<long>(vTimes.number);
                    }
                    long id = schedAction(delay, interval, times, [doCall](int ln) { doCall(ln); }, "api " + M + " " + name + path, "");
                    auto m = std::make_shared<MapData>();
                    m->push_back({Value::string("ok"), Value::boolean_(true)}); m->push_back({Value::string("scheduled"), Value::boolean_(true)});
                    m->push_back({Value::string("id"), Value::num(static_cast<double>(id))}); m->push_back({Value::string("kind"), Value::string("api")});
                    m->push_back({Value::string("target"), Value::string(M + " " + name + path)}); m->push_back({Value::string("in"), Value::num(std::round(delay))});
                    return Value::makeMap(m);
                }
                return doCall(line);
            };
        };
        wrapReal({"apiGet", 2, "GET", 1, -1});
        wrapReal({"apiDelete", 2, "DELETE", 1, -1});
        wrapReal({"apiPost", 3, "POST", 1, 2});
        wrapReal({"apiPut", 3, "PUT", 1, 2});
        wrapReal({"apiPatch", 3, "PATCH", 1, 2});
        wrapReal({"apiCall", 4, nullptr, 2, 3});
    }

    // ---- container.api الحقيقي والكامل (docs/api_container.md) ----
    //  حاوية api تصير حقيقية بمجرد وجود الحقل `baseUrl` (أو apiRegister بنفس اسمها)، وتُقرأ إعداداتها من حقولها **وقت كل استدعاء**
    //  فتعديل `users.token = "…"` يسري فوراً:  baseUrl · headers (map) · token (Authorization: Bearer) · timeout · retry · backoff · cache (GET) · mock (true = يفرض route الوهمية)
    //  callApi(c, method, path, [body], [opts]) و call(method, path, [body], [opts]):  GET/DELETE: الوسيط الرابع = opts؛  POST/PUT/PATCH: الرابع = body والخامس = opts
    //  opts: params {id: 5} يستبدل :id في المسار · query {page: 2} يلحق ?page=2 · + كل خيارات الـ API الحقيقي (retry/backoff/timeout/cache/after/at/every/times/then)
    //  apiInfo(c) → {name, mode: "real"|"mock", baseUrl, routes, calls, failed, lastStatus, headers}
    {
        struct Stat { double calls = 0, failed = 0; Value last; };
        auto stats = std::make_shared<std::unordered_map<std::string, Stat>>();
        auto fieldOf = [this](const std::string& key, const char* f) -> Value {
            auto it = containers.find(key);
            if (it == containers.end() || !it->second) return Value::nil();
            auto v = it->second->values.find(f);
            return v == it->second->values.end() ? Value::nil() : v->second;
        };
        // هل هذه الحاوية حقيقية؟ ويزامن نقطتها (baseUrl + ترويسات) من الحقول
        auto isReal = [this, fieldOf](const std::string& key) -> bool {
            if (fieldOf(key, "mock").isTruthy()) return false;
            Value bu = fieldOf(key, "baseUrl");
            if (bu.type == Value::Type::STRING && !bu.str.empty()) {
                auto& ep = apiEndpoints[key];
                ep.baseUrl = bu.str; ep.headers.clear();
                Value h = fieldOf(key, "headers");
                bool hasAuth = false;
                if (h.type == Value::Type::MAP && h.map) for (auto& kv : *h.map) {
                    const std::string k = kv.first.toDisplayString(); ep.headers.push_back({k, kv.second.toDisplayString()});
                    if (toUpperAscii(k) == "AUTHORIZATION") hasAuth = true;
                }
                Value tk = fieldOf(key, "token");
                if (!hasAuth && tk.type == Value::Type::STRING && !tk.str.empty()) ep.headers.push_back({"Authorization", "Bearer " + tk.str});
                return true;
            }
            return apiEndpoints.count(key) > 0;
        };
        auto enc = [](const std::string& sv) { static const char* hex = "0123456789ABCDEF"; std::string o; for (unsigned char c : sv) { if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c); else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; } } return o; };
        auto mkWrap = [this, stats, fieldOf, isReal, enc, apiPublish](const char* fnName, bool named) {
            auto prev = natives[fnName];                        // النسخة السابقة (محاكاة + خيارات المؤقّت): تُستعمل للحاويات الوهمية
            const std::string fn = fnName;
            natives[fnName] = [this, stats, fieldOf, isReal, enc, apiPublish, prev, fn, named](Args& a, int line) -> Value {
                const size_t o = named ? 1 : 0;                  // callApi: (c, method, path…) · call: (method, path…)
                if (a.size() < o + 2 || (named && a[0].type != Value::Type::STRING) || a[o].type != Value::Type::STRING || a[o + 1].type != Value::Type::STRING) return prev(a, line);
                const std::string key = named ? a[0].str : (containerStack.empty() ? "" : containerStack.back());
                auto publishSync = [apiPublish, stats, &key](const Value& r) {   // الاستدعاء المتزامن ينشر حالته ويُحصى (المجدوَل ينشرها عند لحظته)
                    bool sched = false; if (r.type == Value::Type::MAP && r.map) for (auto& kv : *r.map) if (kv.first.toDisplayString() == "scheduled") sched = true;
                    if (sched) return;
                    (*apiPublish)(key, r, false);
                    auto& st = (*stats)[key]; st.calls += 1; st.last = r;
                    bool ok = false; double code = 0; if (r.type == Value::Type::MAP) for (auto& kv : *r.map) { const std::string k = kv.first.toDisplayString(); if (k == "ok") ok = kv.second.isTruthy(); else if (k == "status" && kv.second.type == Value::Type::NUMBER) code = kv.second.number; }
                    if (!ok || code >= 400) st.failed += 1;
                };
                if (!isReal(key)) {
                    // محاكاة: params/query تعمل أيضاً (المسار يُوسَّع ويُطابَق route بلا الـ query) فيعمل نفس الكود في الحقيقي والمحاكاة
                    if (a.size() == o + 3 && a.back().type == Value::Type::MAP && a.back().map) {
                        Value params, query; auto rest = std::make_shared<MapData>();
                        for (auto& kv : *a.back().map) { const std::string k = kv.first.toDisplayString(); if (k == "params") params = kv.second; else if (k == "query") query = kv.second; else rest->push_back(kv); }
                        if (params.type != Value::Type::NIL || query.type != Value::Type::NIL) {
                            std::string pth = a[o + 1].str;
                            if (params.type == Value::Type::MAP && params.map) for (auto& kv : *params.map) {
                                const std::string ph = ":" + kv.first.toDisplayString(); size_t at;
                                while ((at = pth.find(ph)) != std::string::npos) pth.replace(at, ph.size(), enc(kv.second.toDisplayString()));
                            }
                            if (pth.find("/:") != std::string::npos) throw TO::badArg(line, fn + ": the path still has an unfilled parameter `" + pth.substr(pth.find("/:") + 1) + "` — pass `params: {name: value}`");
                            std::vector<Value> na(a.begin(), a.begin() + static_cast<long>(o + 2)); na[o + 1] = Value::string(pth);
                            if (!rest->empty()) na.push_back(Value::makeMap(rest));
                            Value r = prev(na, line); publishSync(r); return r;
                        }
                    }
                    Value r = prev(a, line); publishSync(r); return r;
                }
                const std::string M = toUpperAscii(a[o].str);
                std::string path = a[o + 1].str;
                const bool bodyM = (M == "POST" || M == "PUT" || M == "PATCH");
                Value body = Value::nil(), opts = Value::nil();
                const size_t extra = a.size() - (o + 2);
                if (extra > 2) throw TO::badArg(line, fn + ": too many arguments (method, path, [body], [options])");
                if (bodyM) { if (extra >= 1) body = a[o + 2]; if (extra == 2) opts = a[o + 3]; }
                else { if (extra == 2) throw TO::badArg(line, fn + ": " + M + " takes no body — pass options as the 4th argument"); if (extra == 1) opts = a[o + 2]; }
                if (opts.type != Value::Type::NIL && (opts.type != Value::Type::MAP || !opts.map)) throw TO::badArg(line, fn + ": options must be a map but got " + opts.typeName());
                // params / query ثم الافتراضات من الحقول
                auto nopts = std::make_shared<MapData>();
                Value params, query;
                if (opts.type == Value::Type::MAP) for (auto& kv : *opts.map) { const std::string k = kv.first.toDisplayString(); if (k == "params") params = kv.second; else if (k == "query") query = kv.second; else nopts->push_back(kv); }
                auto has = [&](const char* k) { for (auto& kv : *nopts) if (kv.first.toDisplayString() == k) return true; return false; };
                if (params.type == Value::Type::MAP && params.map) for (auto& kv : *params.map) {
                    const std::string ph = ":" + kv.first.toDisplayString(); size_t at;
                    while ((at = path.find(ph)) != std::string::npos) path.replace(at, ph.size(), enc(kv.second.toDisplayString()));
                }
                if (path.find("/:") != std::string::npos) throw TO::badArg(line, fn + ": the path still has an unfilled parameter `" + path.substr(path.find("/:") + 1) + "` — pass `params: {name: value}`");
                if (query.type == Value::Type::MAP && query.map && !query.map->empty()) {
                    std::string qs; for (auto& kv : *query.map) { if (!qs.empty()) qs += "&"; qs += enc(kv.first.toDisplayString()) + "=" + enc(kv.second.toDisplayString()); }
                    path += (path.find('?') == std::string::npos ? "?" : "&") + qs;
                }
                const bool timed = has("after") || has("at") || has("every");
                for (const char* d : {"retry", "backoff", "timeout"}) { Value f = fieldOf(key, d); if (f.type == Value::Type::NUMBER && !has(d) && !(std::string(d) == "backoff" && !has("retry") && fieldOf(key, "retry").type != Value::Type::NUMBER)) nopts->push_back({Value::string(d), f}); }
                { Value f = fieldOf(key, "cache"); if (M == "GET" && f.type == Value::Type::NUMBER && !has("cache")) nopts->push_back({Value::string("cache"), f}); }
                if (has("backoff") && !has("retry")) throw TO::badArg(line, fn + ": `backoff` needs `retry` (set the `retry` field or pass `retry: n`)");
                std::vector<Value> ca{Value::string(key), Value::string(M), Value::string(path), body};
                if (!nopts->empty()) ca.push_back(Value::makeMap(nopts));
                Value r = natives["apiCall"](ca, line);
                (void)timed; publishSync(r);          // النشر والإحصاء معاً في publishSync
                return r;
            };
        };
        mkWrap("callApi", true);
        mkWrap("call", false);
        natives["apiInfo"] = [this, stats, fieldOf, isReal, argc](Args& a, int line) -> Value {
            argc("apiInfo", a, 1, 1, line);
            if (a[0].type != Value::Type::STRING) throw TO::badArg(line, "apiInfo: the API name must be text");
            const std::string key = a[0].str;
            const bool real = isReal(key);
            auto rit = apiRoutes.find(key);
            if (!real && rit == apiRoutes.end() && !containers.count(key)) throw diagErr(diag::Code::E0007_InvalidArguments, line, "apiInfo: no api container or registered API named `" + key + "`");
            auto m = std::make_shared<MapData>();
            auto add = [&](const char* k, Value v) { m->push_back({Value::string(k), std::move(v)}); };
            add("name", Value::string(key)); add("mode", Value::string(real ? "real" : "mock"));
            add("baseUrl", real ? Value::string(apiEndpoints[key].baseUrl) : Value::nil());
            add("routes", Value::num(rit == apiRoutes.end() ? 0.0 : static_cast<double>(rit->second.size())));
            auto hs = std::make_shared<ArrayData>(); if (real) for (auto& h : apiEndpoints[key].headers) hs->push_back(Value::string(h.first));
            add("headers", Value::makeArray(hs));
            auto sit = stats->find(key);
            add("calls", Value::num(sit == stats->end() ? 0.0 : sit->second.calls)); add("failed", Value::num(sit == stats->end() ? 0.0 : sit->second.failed));
            Value ls = Value::nil(); if (sit != stats->end() && sit->second.last.type == Value::Type::MAP) for (auto& kv : *sit->second.last.map) if (kv.first.toDisplayString() == "status") ls = kv.second;
            add("lastStatus", ls);
            return Value::makeMap(m);
        };
    }
    auto needFn = [](const Value& v, const char* fn, int line) {
        if ((v.type != Value::Type::FUNCTION || !v.function) && v.type != Value::Type::INSTANCE)
            throw TO::badTyp(line, std::string(fn) + ": expected a function to call, got " + v.typeName());
    };
    auto fnLabel = [](const Value& v) { return v.type == Value::Type::FUNCTION && v.function && v.function->declaration ? v.function->declaration->name : std::string("callback"); };

    // ---- الساعة ----
    natives["time.freeze"] = [argc](Args& a, int line) -> Value {
        argc("time.freeze", a, 0, 1, line);
        double at = clk::epochMs();
        if (!a.empty()) at = static_cast<double>(extra7::toT(a[0], line, "time.freeze").ms);
        clk::freeze(at);
        return TO::instant(clk::epochMs());
    };
    natives["time.unfreeze"] = [argc](Args& a, int line) -> Value { argc("time.unfreeze", a, 0, 0, line); clk::unfreeze(); return Value::boolean_(true); };
    // ---- icons: استيراد أيقونة (من ردّ api أو نصّ) إلى سجلّ indsin -------------------------------------
    //  icons.import(name, src[, glyph]) → true/false · icons.has(name) · icons.path(name) · icons.svg(name[, size, color])
    //  icons.fromApi(c, name[, glyph]) يستورد من c.data (آخر ردّ ناجح) · icons.names() أسماء الأيقونات المتجهية
    natives["icons.importPack"] = [argc](Args& a, int line) -> Value {
        argc("icons.importPack", a, 1, 2, line);
        std::string prefix = a.size() > 1 && a[1].type == Value::Type::STRING ? a[1].str : "";
        std::vector<Value> out; for (auto& n : iconPackImport(a[0], prefix)) out.push_back(Value::string(n));
        return TimerOps::arr(std::move(out));
    };
    // icons.fromUrl(url[, name | prefix]) — يجلب رابطاً (https أو localhost): SVG واحد (يحتاج name) أو حزمة (JSON/sprite، والوسيط بادئة)
    natives["icons.fromUrl"] = [this, argc](Args& a, int line) -> Value {
        argc("icons.fromUrl", a, 1, 2, line);
        if (a[0].type != Value::Type::STRING) throw TimerOps::badArg(line, "icons.fromUrl: the url must be a string");
        const std::string& url = a[0].str;
        const bool local = url.rfind("http://127.0.0.1", 0) == 0 || url.rfind("http://localhost", 0) == 0;
        if (url.rfind("https://", 0) != 0 && !local) throw TimerOps::badArg(line, "icons.fromUrl: only https:// URLs are allowed (http:// only for localhost)");
        auto r = http::performRequest("GET", url, {}, "", defaultHttpTimeoutMs);
        if (!r.ok || r.status < 200 || r.status >= 300 || r.body.size() > 4u * 1024 * 1024) return TimerOps::arr({});
        std::string arg = a.size() > 1 && a[1].type == Value::Type::STRING ? a[1].str : "";
        std::vector<Value> out;
        std::string d = indsin::IconRegistry::sourcePath(r.body);
        if (!d.empty() && !arg.empty() && r.body.find("<symbol") == std::string::npos) { if (indsin::iconRegistry().importPath(arg, d)) out.push_back(Value::string(arg)); }
        else for (auto& n : iconPackImport(Value::string(r.body), arg)) out.push_back(Value::string(n));
        return TimerOps::arr(std::move(out));
    };
    natives["icons.import"] = [argc](Args& a, int line) -> Value {
        argc("icons.import", a, 2, 3, line);
        if (a[0].type != Value::Type::STRING || a[0].str.empty()) throw TimerOps::badArg(line, "icons.import: the icon name must be a non-empty string");
        std::string src, nm, gl; if (a.size() > 2 && a[2].type == Value::Type::STRING) gl = a[2].str;
        if (!iconSourceOf(a[1], src, nm, gl)) return Value::boolean_(false);
        return Value::boolean_(indsin::iconRegistry().importSource(a[0].str, src, gl));
    };
    natives["icons.has"] = [argc](Args& a, int line) -> Value {
        argc("icons.has", a, 1, 1, line);
        return Value::boolean_(a[0].type == Value::Type::STRING && (indsin::iconRegistry().has(a[0].str) || indsin::iconRegistry().hasVector(a[0].str)));
    };
    natives["icons.path"] = [argc](Args& a, int line) -> Value {
        argc("icons.path", a, 1, 1, line);
        std::string d = a[0].type == Value::Type::STRING ? indsin::iconRegistry().vectorPath(a[0].str) : "";
        return d.empty() ? Value::nil() : Value::string(d);
    };
    natives["icons.svg"] = [argc](Args& a, int line) -> Value {
        argc("icons.svg", a, 1, 3, line);
        if (a[0].type != Value::Type::STRING) return Value::nil();
        int size = a.size() > 1 && a[1].type == Value::Type::NUMBER ? static_cast<int>(a[1].number) : 24;
        std::string col = a.size() > 2 && a[2].type == Value::Type::STRING ? a[2].str : "currentColor";
        std::string s = indsin::iconRegistry().svg(a[0].str, size, col);
        return s.empty() ? Value::nil() : Value::string(s);
    };
    natives["icons.names"] = [this, argc](Args& a, int line) -> Value {
        argc("icons.names", a, 0, 0, line);
        auto& R = indsin::iconRegistry(); R.loadVectors();
        std::vector<std::string> v; for (auto& kv : R.vectors) v.push_back(kv.first);
        std::sort(v.begin(), v.end());
        std::vector<Value> out; for (auto& n : v) out.push_back(Value::string(n));
        return TimerOps::arr(std::move(out));
    };
    natives["icons.fromApi"] = [this, argc](Args& a, int line) -> Value {
        argc("icons.fromApi", a, 2, 3, line);
        std::string key; Environment* env = TimerOps::containerEnv(*this, a[0], "icons.fromApi", line, &key);
        if (a[1].type != Value::Type::STRING || a[1].str.empty()) throw TimerOps::badArg(line, "icons.fromApi: the icon name must be a non-empty string");
        auto it = env->values.find("data"); if (it == env->values.end()) return Value::boolean_(false);
        std::string src, nm, gl; if (a.size() > 2 && a[2].type == Value::Type::STRING) gl = a[2].str;
        if (!iconSourceOf(it->second, src, nm, gl)) return Value::boolean_(false);
        return Value::boolean_(indsin::iconRegistry().importSource(a[1].str, src, gl));
    };
    natives["time.frozen"] = [argc](Args& a, int line) -> Value { argc("time.frozen", a, 0, 0, line); return Value::boolean_(clk::isVirtual()); };
    natives["time.advance"] = [this, argc](Args& a, int line) -> Value {
        argc("time.advance", a, 1, 2, line);
        if (!clk::isVirtual()) throw TO::misuse(line, "`time.advance` moves a frozen clock, but the clock is running", "freeze it first: `time.freeze();` (and `time.unfreeze();` to go back to real time)");
        double ms;
        if (a.size() == 2) {
            if (a[0].type != Value::Type::NUMBER) throw TO::badTyp(line, "time.advance(n, unit): n must be a number");
            ms = extra7::durationMs(extra7::durationParts(a[0], &a[1], line, "time.advance"));
            if (ms < 0) throw TO::badArg(line, "time.advance: the duration cannot be negative");
        } else ms = TO::dur(a[0], line, "time.advance");
        TO::advanceTo(*this, clk::monoMs() + ms, line);
        return TO::instant(clk::epochMs());
    };

    // ---- المجدوِل ----
    natives["timers.after"] = [this, argc, needFn, fnLabel](Args& a, int line) -> Value {
        if (a.size() < 2) argc("timers.after", a, 2, 2, line);
        needFn(a[1], "timers.after", line);
        Args rest(a.begin() + 2, a.end());
        return Value::num(static_cast<double>(schedCallback(TO::dur(a[0], line, "timers.after"), 0, 1, a[1], rest, fnLabel(a[1]), "")));
    };
    natives["timers.every"] = [this, argc, needFn, fnLabel](Args& a, int line) -> Value {
        argc("timers.every", a, 2, 3, line);
        needFn(a[1], "timers.every", line);
        const double iv = TO::dur(a[0], line, "timers.every", false);
        double first = iv; long times = -1; Args args;
        if (a.size() == 3) {
            if (a[2].type != Value::Type::MAP || !a[2].map) throw TO::badTyp(line, "timers.every: the options must be a map like {times: 3, after: \"1s\", args: [1]}");
            for (auto& kv : *a[2].map) {
                if (kv.first.type != Value::Type::STRING) continue;
                const std::string& k = kv.first.str;
                if (k == "times") { if (kv.second.type != Value::Type::NUMBER || kv.second.number < 1 || kv.second.number != std::floor(kv.second.number)) throw TO::badArg(line, "timers.every: `times` must be a whole number >= 1"); times = static_cast<long>(kv.second.number); }
                else if (k == "after") first = TO::dur(kv.second, line, "timers.every `after`");
                else if (k == "args") { if (kv.second.type != Value::Type::ARRAY || !kv.second.array) throw TO::badTyp(line, "timers.every: `args` must be a list"); args = *kv.second.array; }
                else throw TO::badArg(line, "timers.every: unknown option `" + k + "` — times, after, args");
            }
        }
        return Value::num(static_cast<double>(schedCallback(first, iv, times, a[1], args, fnLabel(a[1]), "")));
    };
    natives["timers.at"] = [this, argc, needFn, fnLabel](Args& a, int line) -> Value {
        if (a.size() < 2) argc("timers.at", a, 2, 2, line);
        needFn(a[1], "timers.at", line);
        const double target = static_cast<double>(extra7::toT(a[0], line, "timers.at").ms);
        const double delay = target - clk::epochMs();
        if (delay < 0) throw TO::badArg(line, "timers.at: that moment is already in the past (" + Value::num(-delay / 1000.0).toDisplayString() + " s ago)");
        Args rest(a.begin() + 2, a.end());
        return Value::num(static_cast<double>(schedCallback(delay, 0, 1, a[1], rest, fnLabel(a[1]), "")));
    };
    natives["timers.cancel"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.cancel", a, 1, 1, line);
        if (a[0].type != Value::Type::NUMBER) throw TO::badTyp(line, "timers.cancel expects the id returned by timers.after/every/at");
        const long id = static_cast<long>(a[0].number);
        if (hub_) for (auto& e : hub_->q) if (e->id == id && e->kind == SchedEntry::K::Timer) throw TO::misuse(line, "id " + std::to_string(id) + " belongs to a `timer` variable", "use `stop " + e->label + ";` instead");
        return Value::boolean_(TO::remove(*this, id));
    };
    natives["timers.pending"] = [this, argc](Args& a, int line) -> Value { argc("timers.pending", a, 0, 0, line); return Value::num(hub_ ? static_cast<double>(hub_->q.size()) : 0.0); };
    natives["timers.next"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.next", a, 0, 0, line);
        const double d = TO::nextDue(*this);
        return std::isinf(d) ? Value::nil() : Value::num(std::max(0.0, std::round(d - clk::monoMs())));
    };
    natives["timers.list"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.list", a, 0, 1, line);
        std::string only;
        if (!a.empty()) TO::containerEnv(*this, a[0], "timers.list", line, &only);
        std::vector<std::shared_ptr<SchedEntry>> es;
        if (hub_) es = hub_->q;
        std::sort(es.begin(), es.end(), [](auto& x, auto& y) { return x->due != y->due ? x->due < y->due : x->seq < y->seq; });
        std::vector<Value> out;
        const double now = clk::monoMs();
        for (auto& e : es) {
            if (!only.empty() && e->owner != only) continue;
            const char* k = e->kind == SchedEntry::K::Callback ? "callback" : e->kind == SchedEntry::K::Timer ? "timer" : e->kind == SchedEntry::K::Ttl ? "ttl" : "dialog";
            out.push_back(TO::mapOf({{"id", Value::num(static_cast<double>(e->id))}, {"kind", Value::string(k)}, {"label", Value::string(e->label)},
                                     {"in", Value::num(std::max(0.0, std::round(e->due - now)))}, {"every", Value::num(e->interval)},
                                     {"owner", e->owner.empty() ? Value::nil() : Value::string(e->owner)}}));
        }
        return TO::arr(out);
    };
    natives["timers.tick"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.tick", a, 0, 0, line);
        const long f0 = hub().fired;
        TO::advanceTo(*this, clk::monoMs(), line);
        return Value::num(static_cast<double>(hub().fired - f0));
    };
    natives["timers.run"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.run", a, 0, 1, line);
        return Value::num(static_cast<double>(a.empty() ? TO::runFor(*this, 0, true, line) : TO::runFor(*this, TO::dur(a[0], line, "timers.run"), false, line)));
    };
    natives["timers.clear"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.clear", a, 0, 1, line);
        std::string only;
        if (!a.empty()) TO::containerEnv(*this, a[0], "timers.clear", line, &only);
        long n = 0;
        if (hub_) for (size_t i = 0; i < hub_->q.size();) {
            auto e = hub_->q[i];
            const bool plain = e->kind == SchedEntry::K::Callback || e->kind == SchedEntry::K::Ttl;
            if (plain && (only.empty() || e->owner == only || e->container == only)) { TO::remove(*this, e->id); ++n; } else ++i;
        }
        return Value::num(static_cast<double>(n));
    };

    // ---- انتهاء صلاحية حقل حاوية (ttl) ----
    natives["timers.expire"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.expire", a, 3, 4, line);
        std::string key; Environment* env = TO::containerEnv(*this, a[0], "timers.expire", line, &key);
        if (a[1].type != Value::Type::STRING) throw TO::badTyp(line, "timers.expire: the field name must be text");
        if (!env->values.count(a[1].str)) throw TO::misuse(line, "container `" + key + "` has no field `" + a[1].str + "`", "create it first, e.g. `setField(\"" + key + "\", \"" + a[1].str + "\", value)`");
        const double ms = TO::dur(a[2], line, "timers.expire", false);
        if (hub_) for (auto& e : hub_->q) if (e->kind == SchedEntry::K::Ttl && e->container == key && e->field == a[1].str) { TO::remove(*this, e->id); break; }
        auto e = std::make_shared<SchedEntry>();
        e->kind = SchedEntry::K::Ttl; e->due = clk::monoMs() + ms; e->container = key; e->field = a[1].str; e->owner = key;
        e->label = key + "." + a[1].str + " (ttl)";
        if (a.size() == 4) { if (a[3].type == Value::Type::FUNCTION) e->onExpire = a[3]; else e->fallback = a[3]; }
        return Value::num(static_cast<double>(TO::add(*this, e, line)));
    };
    natives["timers.ttl"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.ttl", a, 2, 2, line);
        std::string key; TO::containerEnv(*this, a[0], "timers.ttl", line, &key);
        if (hub_) for (auto& e : hub_->q) if (e->kind == SchedEntry::K::Ttl && e->container == key && a[1].type == Value::Type::STRING && e->field == a[1].str)
            return Value::num(std::max(0.0, std::round(e->due - clk::monoMs())));
        return Value::nil();
    };
    natives["timers.persist"] = [this, argc](Args& a, int line) -> Value {
        argc("timers.persist", a, 2, 2, line);
        std::string key; TO::containerEnv(*this, a[0], "timers.persist", line, &key);
        if (hub_) for (auto& e : hub_->q) if (e->kind == SchedEntry::K::Ttl && e->container == key && a[1].type == Value::Type::STRING && e->field == a[1].str)
            return Value::boolean_(TO::remove(*this, e->id));
        return Value::boolean_(false);
    };

    // ---- مؤقّتات الحاوية كلها ----
    auto bulk = [this, argc](const char* fn, const char* action, bool onlyWhen(const TimerState&)) {
        natives[fn] = [this, argc, fn, action, onlyWhen](Args& a, int line) -> Value {
            argc(fn, a, 1, 1, line);
            Environment* env = TO::containerEnv(*this, a[0], fn, line);
            long n = 0;
            for (auto& kv : TO::timersOf(env)) if (onlyWhen(*kv.second->timer)) { if (TO::doAction(*this, action, env, kv.first, kv.second, nullptr, line).isTruthy()) ++n; }
            return Value::num(static_cast<double>(n));
        };
    };
    bulk("timers.pauseAll", "pause", [](const TimerState& t) { return t.st == TimerState::St::Running || t.st == TimerState::St::Armed; });
    bulk("timers.resumeAll", "resume", [](const TimerState& t) { return t.st == TimerState::St::Paused; });
    bulk("timers.stopAll", "stop", [](const TimerState& t) { return t.st == TimerState::St::Running || t.st == TimerState::St::Armed || t.st == TimerState::St::Paused; });

    // ---- timer بصيغة الدوال (للتحكم من خارج الحاوية) ----
    natives["timerList"] = [this, argc](Args& a, int line) -> Value {
        argc("timerList", a, 1, 1, line);
        Environment* env = TO::containerEnv(*this, a[0], "timerList", line);
        std::vector<Value> out;
        for (auto& kv : TO::timersOf(env)) out.push_back(Value::string(kv.first));
        return TO::arr(out);
    };
    natives["timerInfo"] = [this, argc](Args& a, int line) -> Value {
        argc("timerInfo", a, 2, 2, line);
        Environment* o = nullptr; std::shared_ptr<LiveMeta> m;
        if (a[1].type != Value::Type::STRING) throw TO::badTyp(line, "timerInfo: the timer name must be text");
        timerLocateIn(a[0].toDisplayString(), a[1].str, line, o, m);
        return TO::infoMap(*this, o, a[1].str, m, line);
    };
    for (const char* act : {"start", "pause", "resume", "stop", "reset", "restart", "lap", "extend"}) {
        const std::string action = act;
        const std::string fn = "timer" + std::string(1, static_cast<char>(std::toupper(act[0]))) + (act + 1);
        const bool needArg = action == "extend";
        natives[fn] = [this, argc, action, fn, needArg](Args& a, int line) -> Value {
            argc(fn.c_str(), a, needArg ? 3 : 2, needArg ? 3 : 2, line);
            Environment* o = nullptr; std::shared_ptr<LiveMeta> m;
            if (a[1].type != Value::Type::STRING) throw TO::badTyp(line, fn + ": the timer name must be text");
            timerLocateIn(a[0].toDisplayString(), a[1].str, line, o, m);
            return TO::doAction(*this, action, o, a[1].str, m, needArg ? &a[2] : nullptr, line);
        };
    }

    // ---- عمر الحاوية ----
    natives["containerTime"] = [this, argc](Args& a, int line) -> Value {
        argc("containerTime", a, 1, 1, line);
        std::string key; Environment* env = TO::containerEnv(*this, a[0], "containerTime", line, &key);
        auto st = containerStamp_.find(key);
        if (st == containerStamp_.end()) { noteContainerBorn(key); st = containerStamp_.find(key); }
        const double now = clk::epochMs();
        long ntimers = static_cast<long>(TO::timersOf(env).size()), nttl = 0;
        if (hub_) for (auto& e : hub_->q) if (e->kind == SchedEntry::K::Ttl && e->container == key) ++nttl;
        return TO::mapOf({{"name", Value::string(key)}, {"created", TO::instant(st->second.born)}, {"updated", TO::instant(st->second.updated)},
                          {"age", TO::span(now - st->second.born)}, {"idle", TO::span(now - st->second.updated)},
                          {"writes", Value::num(static_cast<double>(st->second.writes))},
                          {"timers", Value::num(static_cast<double>(ntimers))}, {"ttls", Value::num(static_cast<double>(nttl))}});
    };
}

void Interpreter::timerLocateIn(const std::string& container, const std::string& name, int line, Environment*& owner, std::shared_ptr<LiveMeta>& m) {
    TimerOps::locateIn(*this, container, name, line, owner, m);
}

long Interpreter::schedAction(double delayMs, double intervalMs, long times, std::function<void(int)> action, const std::string& label, const std::string& owner) {
    auto e = std::make_shared<SchedEntry>();
    e->kind = SchedEntry::K::Callback; e->due = clk::monoMs() + delayMs; e->action = std::move(action);
    e->interval = intervalMs; e->remaining = times; e->label = label; e->owner = owner;
    auto it = containers.find(owner);
    if (!owner.empty() && it != containers.end()) e->env = it->second;     // حذف الحاوية يلغي ما جدولته
    return TimerOps::add(*this, e, 0);
}
double Interpreter::timerDurationArg(const Value& v, int line, const std::string& what) { return TimerOps::dur(v, line, what); }
double Interpreter::timerAtArg(const Value& v, int line, const std::string& what) {
    const double delay = static_cast<double>(extra7::toT(v, line, what.c_str()).ms) - clk::epochMs();
    if (delay < 0) throw TimerOps::badArg(line, what + ": that moment is already in the past");
    return delay;
}

long Interpreter::schedCallback(double delayMs, double intervalMs, long times, const Value& fn, std::vector<Value> args, const std::string& label, const std::string& owner) {
    auto e = std::make_shared<SchedEntry>();
    e->kind = SchedEntry::K::Callback; e->due = clk::monoMs() + delayMs; e->fn = fn; e->args = std::move(args);
    e->interval = intervalMs; e->remaining = times; e->label = label; e->owner = owner;
    return TimerOps::add(*this, e, 0);
}

} // namespace rin
