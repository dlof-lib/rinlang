// indsin/rin_indsin_c_api.cpp
#include "rin_indsin_c_api.h"
#include "rin_indsin_html.h"
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include "rin_indsin_validate.h"
#include "rin_indsin_effects.h"
#include "rin_indsin_effects_paint.h" // Effects Studio: يطبّق الإطارات على DrawList
#include "rin_indsin_system.h"     // Design System v2: tokens/themes/palette
#include "rin_indsin_query.h"      // Fabric introspection (stats/outline/catalog)
#include "rin_indsin_audit.h"      // accessibility + layout audit
#include "rin_indsin_responsive.h" // <key>_<breakpoint> attribute overrides
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <cmath>

namespace {
char* dupToC(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

// ---- Indsintime session state ----
struct IndsinSession {
    indsin::PipelineResult state;
    indsin::Indsin indsinEngine;
    std::unordered_map<indsin::StrandId, indsin::StrandPtr> index; // rebuilt after any structural change
    int rootWidth = 390;
    int canvasW = 0;          // effective canvas width (root `screen=`/`canvas_width=`); 0 => rootWidth
    int canvasMinH = 0;       // declared minimum canvas height (0 => none)
    indsin::HostBackground hostBg; // host default background (rin_indsin_session_set_default_background)
    int viewportHeight = 844; // Overlay Engine: see rin_indsin_session_set_viewport's doc comment
    indsin::OverlayLayer overlayLayer; // rebuilt every relayout() -- see rin_indsin_overlay.h

    // Persistent interpreter for this session's onTap handlers (see dispatchTap's doc comment in
    // rin_indsin_needle.h)... One IndsinSession == one screen/session == one Interpreter, seeded on
    // first tap (or on cold render -- see rin_indsin_session_create's real-runtime comment below).
    //
    // Held via unique_ptr rather than by value: rin::Interpreter has an implicitly-deleted copy
    // assignment operator (it owns non-copy-assignable state), so `sess->interp = Interpreter()`
    // (needed on the "previous run failed, retry from a clean Interpreter" path in
    // rin_indsin_session_update_source) cannot compile against a plain value member -- reassigning
    // the pointee via std::make_unique sidesteps that entirely.
    std::unique_ptr<rin::Interpreter> interp = std::make_unique<rin::Interpreter>();
    bool interpSeeded = false;
    std::string containerName; // جلسة create_for_container: بيئة الحاوية تُقرأ منها متغيّرات Rin لقيم الخصائص

    // Effects Engine (rin_indsin_effects.h): one animation clock per session, monotonic from
    // whenever the session was created — a plain elapsed-ms counter is all EffectRuntime needs
    // (it only ever compares two of its own readings), so there's no wall-clock/timezone concern.
    std::chrono::steady_clock::time_point clockStart = std::chrono::steady_clock::now();
    indsin::EffectRuntime effectRuntime;
    bool animating = false; // set by relayout() below; read back into every JSON envelope
};

// يثبّت مضيف تقييم Rin لهذه الجلسة طوال نداء واحد: كل خاصية تُعاد قراءتها (نقرة/تحديث/تحميل ساخن)
// تُقيَّم بمفسّر الجلسة الدائم. يقرأ المفسّر عبر sess لحظة الاستعمال (قد يُستبدل في update_source).
struct SessionRinHost {
    indsin::InterpreterExprHost host;
    indsin::ScopedRinHost scope;
    explicit SessionRinHost(IndsinSession* s)
        : host([s] { return s->interp.get(); }, s->containerName), scope(&host) {}
};

double nowMsFor(IndsinSession* sess) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sess->clockStart).count();
}

int canvasWidthOf(const IndsinSession* sess) { return sess->canvasW > 0 ? sess->canvasW : sess->rootWidth; }

// Frame (canvas fill + document + overlay) for the session's current state; W/H = canvas size.
indsin::DrawList paintSessionFrame(IndsinSession* sess, int& W, int& H) {
    W = canvasWidthOf(sess);
    H = (int)std::ceil(sess->state.fabric->geometry.h);
    if (H <= 0) H = sess->viewportHeight; // degenerate/empty Fabric: fall back to a sane canvas
    if (H < sess->canvasMinH) H = sess->canvasMinH;
    indsin::Dye dye;
    indsin::DrawList list = dye.paintFrame(sess->state.fabric, sess->overlayLayer, (double)W, (double)H, sess->hostBg);
    indsin::applyEffectsToDrawList(list, sess->effectRuntime); // غير مدمِّر: الإطار الحالي من relayout()
    return list;
}

std::string canvasJson(IndsinSession* sess) {
    int W = canvasWidthOf(sess);
    int H = (int)std::ceil(sess->state.fabric->geometry.h);
    if (H < sess->canvasMinH) H = sess->canvasMinH;
    indsin::CanvasInfo ci = indsin::resolveCanvas(sess->state.fabric, sess->hostBg);
    std::ostringstream os;
    char hex[16]; std::snprintf(hex, sizeof hex, "#%02X%02X%02X", ci.color.r, ci.color.g, ci.color.b);
    os << "{\"width\":" << W << ",\"height\":" << H << ",\"background\":\"" << hex
       << "\",\"source\":\"" << ci.source << "\"";
    if (ci.gradient()) {
        char hex2[16]; std::snprintf(hex2, sizeof hex2, "#%02X%02X%02X", ci.stops.back().r, ci.stops.back().g, ci.stops.back().b);
        os << ",\"background2\":\"" << hex2 << "\",\"angle\":" << ci.angle << ",\"stops\":[";
        for (size_t k = 0; k < ci.stops.size(); k++) {
            char hx[16]; std::snprintf(hx, sizeof hx, "#%02X%02X%02X", ci.stops[k].r, ci.stops[k].g, ci.stops[k].b);
            os << (k ? "," : "") << "\"" << hx << "\"";
        }
        os << "]";
    }
    os << "}";
    return os.str();
}

void relayout(IndsinSession* sess) {
    if (!sess->state.ok || !sess->state.fabric) return;
    sess->indsinEngine = indsin::Indsin{}; // fresh stats per call; Tension caching lives on the Strands themselves
    sess->state.fabric->screenRoot = true;
    // Design canvas (root `screen=` / `canvas_width=` / `canvas_height=`): the screen may be any size
    // the design asks for, not only the host's width. Falls back to the host width.
    double cw = 0, ch = 0;
    sess->canvasW = 0; sess->canvasMinH = 0;
    if (indsin::resolveCanvasSize(*sess->state.fabric, (double)sess->rootWidth, cw, ch)) {
        if (cw >= 1) sess->canvasW = (int)std::lround(cw);
        if (ch >= 1) sess->canvasMinH = (int)std::lround(ch);
    }
    const double W = (double)canvasWidthOf(sess);
    indsin::viewportWidthPx() = W;
    indsin::viewportHeightPx() = sess->canvasMinH > 0 ? (double)sess->canvasMinH : (double)sess->viewportHeight;
    // Responsive attrs (rin_indsin_responsive.h): apply columns_md=/gap_lg=/... for the current width
    // BEFORE measuring. Idempotent, so running it on every relayout (tap, edit, resize) is safe.
    indsin::applyResponsiveAttrs(sess->state.fabric, W);
    sess->indsinEngine.layout(sess->state.fabric, indsin::Constraints{W, W, 0, 1e9}, 0, 0);
    // Overlay Engine second pass: re-homes every open Dialog / anchored Tooltip against the
    // viewport now that the whole tree (including their own content boxes) has been measured.
    sess->overlayLayer = indsin::buildOverlayLayer(sess->indsinEngine, sess->state.fabric,
                                                  W, indsin::viewportHeightPx());
    // Effects Studio: بعد التخطيط والـOverlay تُحسَب إطارات التأثيرات (لا تُعدَّل هندسة الـFabric إطلاقاً —
    // تُطبَّق على DrawList وقت الطلاء في paintSessionFrame)، فلا انجراف ولا تعارض مع كاش التخطيط.
    const double now = nowMsFor(sess);
    sess->effectRuntime.processPending(sess->state.fabric, now); // أوامر Rin: indsin.playEffect / effect()
    sess->animating = sess->effectRuntime.update(sess->state.fabric, now);
}
void rebuildIndex(IndsinSession* sess) {
    sess->index.clear();
    if (sess->state.ok && sess->state.fabric) indsin::buildIndex(sess->state.fabric, sess->index);
}
// Overlay Engine (rin_indsin_overlay.h): serializes the current overlay layer so a renderer can
// draw scrim + correct z-order without having to re-derive "which Strand ids are overlays" by
// re-walking attrs itself -- the JSON already has the corrected on-screen x/y for each overlay
// Strand (buildOverlayLayer() mutated s->geometry in place, same field fabricToJson reads), so
// this array only needs to carry what fabricToJson's generic per-Strand shape doesn't: which ids
// are overlays, their paint/stacking order, and their scrim/modal metadata.
std::string overlayLayerJson(const indsin::OverlayLayer& layer) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < layer.entries.size(); i++) {
        const auto& e = layer.entries[i];
        if (i) os << ",";
        os << "{\"id\":" << (e.strand ? e.strand->id : 0)
           << ",\"name\":\"" << indsin::jsonEscape(e.strand ? e.strand->name : "") << "\""
           << ",\"kind\":\"" << (e.kind == indsin::OverlayKind::DIALOG ? "dialog" : "tooltip") << "\""
           << ",\"modal\":" << (e.modal ? "true" : "false")
           << ",\"scrim\":" << (e.scrim ? "true" : "false")
           << ",\"box\":{\"x\":" << e.box.x << ",\"y\":" << e.box.y << ",\"w\":" << e.box.w << ",\"h\":" << e.box.h << "}";
        if (e.scrim) {
            os << ",\"scrimRect\":{\"x\":" << e.scrimRect.x << ",\"y\":" << e.scrimRect.y
               << ",\"w\":" << e.scrimRect.w << ",\"h\":" << e.scrimRect.h << "}";
        }
        os << "}";
    }
    os << "]";
    return os.str();
}
// Envelopes the session's current Fabric + stats as JSON, with room for extra caller-supplied
// fields (already-serialized, comma-prefixed) spliced in before "fabric".
std::string fabricEnvelope(IndsinSession* sess, const std::string& extraFields) {
    std::ostringstream os;
    int cvW = 0, cvH = 0;
    auto draw = paintSessionFrame(sess, cvW, cvH);
    os << "{\"ok\":true" << extraFields
       << ",\"canvas\":" << canvasJson(sess)
       << ",\"strandsMeasured\":" << sess->indsinEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << sess->indsinEngine.stats.cacheHits
       << ",\"animating\":" << (sess->animating ? "true" : "false")
       << ",\"overlays\":" << overlayLayerJson(sess->overlayLayer)
       << ",\"paint\":" << indsin::drawListToJsonString(draw)
       << ",\"fabric\":" << indsin::fabricToJsonString(sess->state.fabric) << "}";
    return os.str();
}
// Media: طلب المضيف (منتقي/رفع) كحقل "media" في الغلاف، أو "" إن لم يوجد.
std::string mediaExtraJson(const indsin::TapResult& t) {
    if (t.mediaPick)   return ",\"media\":" + indsin::media::pickRequestJson(t.pick);
    if (t.mediaUpload) return ",\"media\":" + indsin::media::uploadRequestJson(t.upload);
    if (t.mediaCancel) return ",\"media\":" + indsin::media::cancelRequestJson(t.cancelCell);
    return "";
}
// Effects Studio: إيماءة => تأثير effect_tap/effect_hover/…؛ true إن شُغّل شيء أو كان في الطابور أوامر من Rin.
bool fireGestureEffects(IndsinSession* sess, const char* attrKey, double x, double y) {
    bool any = false;
    if (attrKey && sess->state.fabric) {
        std::vector<indsin::StrandPtr> path;
        if (indsin::hitTestPath(sess->state.fabric, x, y, path) && !path.empty())
            any = sess->effectRuntime.fireTrigger(path, attrKey, nowMsFor(sess));
    }
    return any || indsin::effectLibrary().hasPending();
}
// يطبّق تغييرات Warp (من نتيجة المضيف) كما يفعل tap: Shuttle -> hashes -> relayout.
void applyWarpNames(IndsinSession* sess, const std::vector<std::string>& names) {
    if (!names.empty()) {
        indsin::Shuttle shuttle;
        for (auto& n : names) shuttle.applyWarpChange(n, sess->state.warp, sess->state.subs, sess->index);
        indsin::recomputeHashes(sess->state.fabric);
    }
    relayout(sess);
}
std::string changedJson(const std::vector<std::string>& names) {
    std::string o = ",\"changed\":[";
    for (size_t i = 0; i < names.size(); i++) { if (i) o += ","; o += "\"" + indsin::jsonEscape(names[i]) + "\""; }
    return o + "]";
}
std::string sessionErrorJson(IndsinSession* sess) {
    std::ostringstream os;
    os << "{\"ok\":false,\"error\":\"" << indsin::jsonEscape(sess ? sess->state.errorMessage : "null session")
       << "\",\"line\":" << (sess ? sess->state.errorLine : 0) << "}";
    return os.str();
}
}

extern "C" {

RIN_API char* rin_indsin_render_json(const char* source, int rootWidth) {
    std::string src = source ? source : "";
    if (rootWidth <= 0) rootWidth = 390;

    indsin::PipelineResult r = indsin::runColdPipeline(src);
    if (!r.ok) {
        std::ostringstream os;
        os << "{\"error\":\"" << indsin::jsonEscape(r.errorMessage) << "\",\"line\":" << r.errorLine << "}";
        return dupToC(os.str());
    }

    indsin::Indsin indsinEngine;
    r.fabric->screenRoot = true;
    double cw = 0, ch = 0;
    int canvasW = rootWidth;
    if (indsin::resolveCanvasSize(*r.fabric, (double)rootWidth, cw, ch) && cw >= 1) canvasW = (int)std::lround(cw);
    indsin::viewportWidthPx() = canvasW; indsin::viewportHeightPx() = ch >= 1 ? ch : 844;
    indsin::applyResponsiveAttrs(r.fabric, (double)canvasW);
    indsinEngine.layout(r.fabric, indsin::Constraints{(double)canvasW, (double)canvasW, 0, 1e9}, 0, 0);

    indsin::Dye dye;
    auto draw = dye.paint(r.fabric);
    {
        indsin::CanvasInfo ci = indsin::resolveCanvas(r.fabric, indsin::HostBackground{});
        indsin::DrawCommand cv{indsin::DrawOp::FILL_RECT,
            indsin::Rect{0, 0, (double)canvasW, std::max(ch, std::ceil(r.fabric->geometry.h))}, ci.color, "", 0, 0, 0};
        if (ci.gradient()) { cv.stops = ci.stops; cv.angleDeg = ci.angle; }
        draw.insert(draw.begin(), cv);
    }
    std::ostringstream os;
    os << "{\"ok\":true,\"canvasWidth\":" << canvasW << ",\"strandsMeasured\":" << indsinEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << indsinEngine.stats.cacheHits
       << ",\"paint\":" << indsin::drawListToJsonString(draw)
       << ",\"fabric\":" << indsin::fabricToJsonString(r.fabric) << "}";
    return dupToC(os.str());
}

RIN_API char* rin_indsin_render_container_json(const char* source, const char* containerName, int rootWidth) {
    std::string src = source ? source : "";
    std::string name = containerName ? containerName : "";
    if (rootWidth <= 0) rootWidth = 390;

    indsin::PipelineResult r = indsin::runColdPipelineForContainer(src, name);
    if (!r.ok) {
        std::ostringstream os;
        os << "{\"error\":\"" << indsin::jsonEscape(r.errorMessage) << "\",\"line\":" << r.errorLine << "}";
        return dupToC(os.str());
    }

    indsin::Indsin indsinEngine;
    r.fabric->screenRoot = true;
    indsinEngine.layout(r.fabric, indsin::Constraints{(double)rootWidth, (double)rootWidth, 0, 1e9}, 0, 0);

    std::ostringstream os;
    os << "{\"ok\":true,\"container\":\"" << indsin::jsonEscape(name) << "\""
       << ",\"strandsMeasured\":" << indsinEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << indsinEngine.stats.cacheHits
       << ",\"fabric\":" << indsin::fabricToJsonString(r.fabric) << "}";
    return dupToC(os.str());
}

RIN_API void* rin_indsin_session_create(const char* source, int rootWidth) {
    auto* sess = new (std::nothrow) IndsinSession();
    if (!sess) return nullptr;
    sess->rootWidth = rootWidth > 0 ? rootWidth : 390;
    // Real Runtime Session (see rin_indsin_pipeline.h's runColdPipelineWithRuntime): the session's
    // own persistent rin::Interpreter runs the program for real right here at creation time --
    // not a throwaway one -- so the exact same Interpreter instance (with its containers/chat
    // state/etc. already populated by this real run) is reused by every subsequent onTap via
    // Needle, instead of a brand-new empty Interpreter being created per tap or per session.
    sess->state = indsin::runColdPipelineWithRuntime(source ? source : "", *sess->interp);
    // Mark seeded immediately: the run() above already executed the whole program (container
    // bodies, top-level funs hoisted, etc.), so Needle's own lazy "seed once on first tap" path
    // in dispatchTap (rin_indsin_needle.h) must NOT run() this same Interpreter a second time --
    // that would double every side effect (chat messages appended twice, docs inserted twice, ...).
    if (sess->state.ok) { sess->interpSeeded = true; relayout(sess); rebuildIndex(sess); }
    return sess;
}

RIN_API void* rin_indsin_session_create_for_container(const char* source, const char* containerName, int rootWidth) {
    auto* sess = new (std::nothrow) IndsinSession();
    if (!sess) return nullptr;
    sess->rootWidth = rootWidth > 0 ? rootWidth : 390;
    sess->containerName = containerName ? containerName : "";
    sess->state = indsin::runColdPipelineForContainerWithRuntime(source ? source : "", containerName ? containerName : "", *sess->interp);
    if (sess->state.ok) { sess->interpSeeded = true; relayout(sess); rebuildIndex(sess); } // see rin_indsin_session_create's comment
    return sess;
}

RIN_API char* rin_indsin_session_render_json(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    return dupToC(fabricEnvelope(sess, ""));
}

// Effective `type=` of an Input-like strand (search kind => "search", "phone" => "tel", default "text").
static std::string fieldTypeOf(const indsin::StrandPtr& s) {
    std::string t = s->attrStr("type", "");
    if (t.empty()) t = s->kind == indsin::StrandKind::SEARCH ? "search" : s->kind == indsin::StrandKind::DATE ? "date"
                     : s->kind == indsin::StrandKind::TIME ? "time" : s->kind == indsin::StrandKind::CODE_EDITOR ? "code" : "text";
    return t == "phone" ? "tel" : t;
}
static bool isFieldKind(const indsin::StrandPtr& s) {
    return s->kind == indsin::StrandKind::INPUT || s->kind == indsin::StrandKind::TEXTAREA || s->kind == indsin::StrandKind::SEARCH ||
           s->kind == indsin::StrandKind::DATE || s->kind == indsin::StrandKind::TIME || s->kind == indsin::StrandKind::CODE_EDITOR;
}
static std::string boundCellOf(const indsin::StrandPtr& s) {
    for (auto& a : s->attrs)
        if (a.key == "value")
            if (auto v = std::dynamic_pointer_cast<rin::VariableExpr>(a.rawExpr)) return v->name;
    return "";
}
// The first field whose `value={cell}` is bound to `cell` (null if none): lets set_input apply type/maxlength rules.
static indsin::StrandPtr findFieldBoundTo(const indsin::StrandPtr& s, const std::string& cell) {
    if (!s) return nullptr;
    if (isFieldKind(s) && boundCellOf(s) == cell) return s;
    for (auto& c : s->children) if (auto f = findFieldBoundTo(c, cell)) return f;
    return nullptr;
}

// Input/TextArea/Search under (x,y): {"id","name","kind","bind","value","placeholder","inputType","maxLength","min","max","secure","multiline","readonly"}
// `bind` = the warp cell the field's `value={cell}` is a plain reference to ("" if not bound).
static std::string focusInfoAt(IndsinSession* sess, double x, double y) {
    std::vector<indsin::StrandPtr> path;
    if (!sess->state.fabric || !indsin::hitTestPath(sess->state.fabric, x, y, path) || path.empty()) return "";
    const indsin::StrandPtr& s = path.front();
    if (!isFieldKind(s)) return "";
    if (s->attrStr("disabled", "") == "true" || s->attrStr("readonly", "") == "true") return "";
    std::string type = fieldTypeOf(s);
    auto b = [&](bool v) { return v ? "true" : "false"; };
    std::ostringstream o;
    o << "{\"id\":" << s->id << ",\"name\":\"" << indsin::jsonEscape(s->name) << "\""
      << ",\"kind\":\"" << (s->kind == indsin::StrandKind::TEXTAREA ? "textarea" : s->kind == indsin::StrandKind::CODE_EDITOR ? "code" : "input") << "\""
      << ",\"bind\":\"" << indsin::jsonEscape(boundCellOf(s)) << "\""
      << ",\"value\":\"" << indsin::jsonEscape(s->attrStr("value", "")) << "\""
      << ",\"placeholder\":\"" << indsin::jsonEscape(s->attrStr("placeholder", "")) << "\""
      << ",\"inputType\":\"" << indsin::jsonEscape(type) << "\""
      << ",\"maxLength\":" << (int)s->attrNum("maxlength", 0)
      << ",\"secure\":" << b(s->attrStr("secure", "") == "true" || type == "password")
      << ",\"multiline\":" << b(s->kind == indsin::StrandKind::TEXTAREA || s->kind == indsin::StrandKind::CODE_EDITOR)
      << ",\"language\":\"" << indsin::jsonEscape(s->attrStr("language", s->attrStr("lang", ""))) << "\"";
    o << ",\"rect\":[" << s->geometry.x << "," << s->geometry.y << "," << s->geometry.w << "," << s->geometry.h << "]";
    if (s->attr("min")) o << ",\"min\":" << s->attrNum("min", 0);
    if (s->attr("max")) o << ",\"max\":" << s->attrNum("max", 0);
    o << "}";
    return o.str();
}

RIN_API char* rin_indsin_session_tap(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    SessionRinHost rinHost(sess); // قيم الخصائص المُعاد حسابها تُقيَّم بمفسّر Rin للجلسة
    std::string focusJson = focusInfoAt(sess, x, y); // حقل نصّي تحت اللمسة؟ (يحرّره المضيف ثم set_input)

    indsin::TapResult tap = indsin::dispatchTapWithOverlay(sess->state.fabric, sess->state.warp,
                                                        sess->state.program, sess->overlayLayer, x, y,
                                                        nullptr, nullptr,
                                                        sess->interp.get(), &sess->interpSeeded);
    const bool fxFired = fireGestureEffects(sess, "effect_tap", x, y);

    if (!tap.changedWarpNames.empty()) {
        indsin::Shuttle shuttle;
        for (auto& name : tap.changedWarpNames) {
            shuttle.applyWarpChange(name, sess->state.warp, sess->state.subs, sess->index);
        }
        // Tension's cache is keyed on each Strand's contentHash; applyWarpChange() above mutates
        // a.value directly (by design -- see its own comment) but does not touch contentHash, so
        // without this, relayout() below can short-circuit on the stale cached geometry for any
        // Strand whose size actually depends on the changed value (e.g. Text whose text= grew
        // from a single digit to two digits). Recomputing before layout is what
        // rin_indsin_strand.h's recomputeHashes() itself says it exists for.
        indsin::recomputeHashes(sess->state.fabric);
        relayout(sess); // an attribute change (e.g. a longer counter string) may resize its Strand
    } else if (fxFired) {
        relayout(sess); // تأثير إيماءة/أمر Rin بلا تغيير Warp
    }

    std::ostringstream extra;
    extra << ",\"handled\":" << (tap.handled ? "true" : "false")
          << ",\"targetId\":" << tap.targetId
          << ",\"handler\":\"" << indsin::jsonEscape(tap.handlerDescription) << "\""
          << ",\"changed\":[";
    for (size_t i = 0; i < tap.changedWarpNames.size(); i++) {
        if (i) extra << ",";
        extra << "\"" << indsin::jsonEscape(tap.changedWarpNames[i]) << "\"";
    }
    extra << "]";
    if (!tap.error.empty()) extra << ",\"error\":\"" << indsin::jsonEscape(tap.error) << "\"";
    extra << mediaExtraJson(tap);
    if (!focusJson.empty()) extra << ",\"focus\":" << focusJson;

    return dupToC(fabricEnvelope(sess, extra.str()));
}

// Host text entry: writes `text` into warp cell `name` (the cell an Input's `value={cell}` is bound to),
// re-resolves everything that depends on it and returns the usual envelope with {"changed":[name]}.
RIN_API char* rin_indsin_session_set_input(void* sessionPtr, const char* name, const char* text) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    std::string n = name ? name : "";
    if (n.empty() || !sess->state.warp.has(n))
        return dupToC(fabricEnvelope(sess, ",\"handled\":false,\"error\":\"" + indsin::jsonEscape("'" + n + "' is not a warp cell") + "\""));
    SessionRinHost rinHost(sess);
    std::string clean = text ? text : "";
    if (auto field = findFieldBoundTo(sess->state.fabric, n))   // type / maxlength rules of the field this cell feeds
        clean = indsin::sanitizeFieldText(fieldTypeOf(field), clean, (size_t)field->attrNum("maxlength", 0));
    sess->state.warp.set(n, indsin::Value::txt(clean));
    indsin::Shuttle shuttle;
    shuttle.applyWarpChange(n, sess->state.warp, sess->state.subs, sess->index);
    indsin::recomputeHashes(sess->state.fabric);
    relayout(sess);
    return dupToC(fabricEnvelope(sess, ",\"handled\":true,\"changed\":[\"" + indsin::jsonEscape(n) + "\"]"));
}

// Shared tail for long-press/double-tap/hover below: applies a dispatched indsin::TapResult exactly
// the same way rin_indsin_session_tap does (Warp changes -> Shuttle -> recomputeHashes -> relayout,
// then the {"handled":...,"targetId":...,"handler":...,"changed":[...]} envelope fields) so all
// four gesture endpoints stay byte-for-byte consistent in shape. `error` is only ever set when a
// handler was actually found but failed at runtime -- same convention as the tap endpoint.
char* respondToGesture(IndsinSession* sess, const indsin::TapResult& g, const char* fxKey = nullptr, double fx = 0, double fy = 0) {
    fireGestureEffects(sess, fxKey, fx, fy);
    if (!g.changedWarpNames.empty()) {
        indsin::Shuttle shuttle;
        for (auto& name : g.changedWarpNames) {
            shuttle.applyWarpChange(name, sess->state.warp, sess->state.subs, sess->index);
        }
        indsin::recomputeHashes(sess->state.fabric);
        relayout(sess);
    } else {
        // No Warp change, but an effect= elsewhere in the tree may still be mid-animation --
        // relayout() is cheap (Tension caches unchanged Strands) and keeps sess->animating fresh
        // for this response even when this particular gesture didn't touch anything.
        relayout(sess);
    }

    std::ostringstream extra;
    extra << ",\"handled\":" << (g.handled ? "true" : "false")
          << ",\"targetId\":" << g.targetId
          << ",\"handler\":\"" << indsin::jsonEscape(g.handlerDescription) << "\""
          << ",\"changed\":[";
    for (size_t i = 0; i < g.changedWarpNames.size(); i++) {
        if (i) extra << ",";
        extra << "\"" << indsin::jsonEscape(g.changedWarpNames[i]) << "\"";
    }
    extra << "]";
    if (!g.error.empty()) extra << ",\"error\":\"" << indsin::jsonEscape(g.error) << "\"";
    extra << mediaExtraJson(g);

    return dupToC(fabricEnvelope(sess, extra.str()));
}

RIN_API char* rin_indsin_session_long_press(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    SessionRinHost rinHost(sess); // قيم الخصائص المُعاد حسابها تُقيَّم بمفسّر Rin للجلسة
    indsin::TapResult g = indsin::dispatchLongPressWithOverlay(sess->state.fabric, sess->state.warp,
                                                            sess->state.program, sess->overlayLayer, x, y,
                                                            nullptr, nullptr,
                                                            sess->interp.get(), &sess->interpSeeded);
    return respondToGesture(sess, g, "effect_longpress", x, y);
}

RIN_API char* rin_indsin_session_double_tap(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    SessionRinHost rinHost(sess); // قيم الخصائص المُعاد حسابها تُقيَّم بمفسّر Rin للجلسة
    indsin::TapResult g = indsin::dispatchDoubleTapWithOverlay(sess->state.fabric, sess->state.warp,
                                                            sess->state.program, sess->overlayLayer, x, y,
                                                            nullptr, nullptr,
                                                            sess->interp.get(), &sess->interpSeeded);
    return respondToGesture(sess, g, "effect_doubletap", x, y);
}

RIN_API char* rin_indsin_session_hover(void* sessionPtr, double x, double y, int entering) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    SessionRinHost rinHost(sess); // قيم الخصائص المُعاد حسابها تُقيَّم بمفسّر Rin للجلسة
    indsin::TapResult g = indsin::dispatchHoverWithOverlay(sess->state.fabric, sess->state.warp,
                                                        sess->state.program, sess->overlayLayer, x, y,
                                                        entering != 0, nullptr, nullptr,
                                                        sess->interp.get(), &sess->interpSeeded);
    return respondToGesture(sess, g, entering ? "effect_hover" : nullptr, x, y);
}

RIN_API char* rin_indsin_session_tick(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    // ساعة حيّة (docs/TIME.md): إن عرّف البرنامج `warp clock = time.ms();` تُحدَّث الخلية كل ثانية، فتُعاد
    // تلقائياً كل خاصية تقرؤها (مثل text=time(clock, "HH:mm:ss")). المضيف يواصل النداء ما دام "ticking":true.
    std::vector<std::string> changed;
    bool ticking = false;
    if (sess->state.warp.has("clock")) {
        ticking = true;
        SessionRinHost rinHost(sess);
        const double sec = std::floor(std::chrono::duration<double, std::milli>(
                               std::chrono::system_clock::now().time_since_epoch()).count() / 1000.0) * 1000.0;
        const indsin::Value cur = sess->state.warp.get("clock");
        if (cur.kind == indsin::Value::Kind::NUMBER && cur.number == sec) {
            relayout(sess);
        } else {
            sess->state.warp.set("clock", indsin::Value::num(sec));
            changed.push_back("clock");
            applyWarpNames(sess, changed); // Shuttle -> hashes -> relayout
        }
    } else {
        relayout(sess); // re-applies the Effects Engine at "now"; sess->animating reflects this tick
    }
    std::string extra = ",\"handled\":false" + changedJson(changed);
    if (ticking) extra += ",\"ticking\":true";
    return dupToC(fabricEnvelope(sess, extra));
}

// ---- Effects Studio: تعريف/تشغيل/سرد التأثيرات من المضيف ----
RIN_API int rin_indsin_effect_define(const char* name, const char* frames, const char* options) {
    indsin::fx::KV kv; std::string n = name ? name : "";
    if (frames && *frames) kv.push_back({"frames", frames});
    std::string o = options ? options : "", cur;
    for (char c : o + ";") {
        if (c == ';') {
            size_t e = cur.find('=');
            if (e != std::string::npos) kv.push_back({indsin::fx::trim(cur.substr(0, e)), indsin::fx::trim(cur.substr(e + 1))});
            cur.clear();
        } else cur += c;
    }
    std::string err;
    if (!indsin::effectLibrary().define(n, kv, err)) { indsin::effectLibrary().lastError = err; return 0; }
    return 1;
}
RIN_API char* rin_indsin_effects_json(void) {
    return dupToC("{\"effects\":" + indsin::fx::effectsJson() + ",\"error\":\"" + indsin::fx::jsonStr(indsin::effectLibrary().lastError) + "\"}");
}
RIN_API char* rin_indsin_session_play_effect(void* sessionPtr, const char* target, const char* effect) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::fx::PlayRequest r; r.target = target ? target : ""; r.effect = effect ? effect : "";
    indsin::effectLibrary().queue(std::move(r));
    relayout(sess);
    return dupToC(fabricEnvelope(sess, ",\"handled\":true"));
}
RIN_API char* rin_indsin_session_stop_effect(void* sessionPtr, const char* target) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::fx::PlayRequest r; r.stop = true; r.target = target ? target : "";
    indsin::effectLibrary().queue(std::move(r));
    relayout(sess);
    return dupToC(fabricEnvelope(sess, ",\"handled\":true"));
}

RIN_API char* rin_indsin_session_update_source(void* sessionPtr, const char* newSource) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess) return dupToC("{\"ok\":false,\"error\":\"null session\"}");
    std::string src = newSource ? newSource : "";
    SessionRinHost rinHost(sess);

    if (!sess->state.ok) {
        // Never had a good Fabric to diff against -- just retry the cold pipeline outright, still
        // through the session's own persistent Interpreter (not a throwaway one) so state stays
        // consistent with whatever taps run next -- see rin_indsin_session_create's comment.
        sess->interp = std::make_unique<rin::Interpreter>(); // previous run (if any) never completed successfully; start clean
        sess->interpSeeded = false;
        sess->state = indsin::runColdPipelineWithRuntime(src, *sess->interp);
        if (sess->state.ok) {
            sess->interpSeeded = true;
            relayout(sess); rebuildIndex(sess);
            return dupToC(fabricEnvelope(sess, ",\"handled\":false,\"changed\":[]"));
        }
        return dupToC(sessionErrorJson(sess));
    }

    std::string err; int errLine = 0;
    indsin::runHotPipeline(sess->state, src, err, errLine);
    // runHotPipeline leaves the previous Fabric completely untouched on error (Snag containment),
    // so sess->state.fabric below is either the freshly-diffed tree or still the last-good one.
    rebuildIndex(sess);
    relayout(sess);

    std::ostringstream extra;
    extra << ",\"handled\":false,\"changed\":[]";
    if (!err.empty()) {
        extra << ",\"error\":\"" << indsin::jsonEscape(err) << "\",\"line\":" << errLine << ",\"snag\":true";
    }
    return dupToC(fabricEnvelope(sess, extra.str()));
}

RIN_API void rin_indsin_session_free(void* sessionPtr) {
    delete static_cast<IndsinSession*>(sessionPtr);
}

RIN_API unsigned char* rin_indsin_session_render_rgb(void* sessionPtr, int* outW, int* outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok || !sess->state.fabric) return nullptr;

    int W = 0, H = 0;
    auto draw = paintSessionFrame(sess, W, H);
    auto buf = indsin::rasterizeToBuffer(draw, W, H); // the ONE rasterizer -- see header comment

    unsigned char* out = static_cast<unsigned char*>(std::malloc(buf.size()));
    if (!out) return nullptr;
    std::memcpy(out, buf.data(), buf.size());
    if (outW) *outW = W;
    if (outH) *outH = H;
    return out;
}

RIN_API void rin_indsin_free_buffer(unsigned char* buf) {
    std::free(buf);
}

RIN_API int rin_indsin_session_export_png(void* sessionPtr, const char* path) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok || !sess->state.fabric || !path) return 0;

    int W = 0, H = 0;
    auto draw = paintSessionFrame(sess, W, H);
    auto buf = indsin::rasterizeToBuffer(draw, W, H);
    return indsin::writePNG(path, W, H, buf) ? 1 : 0;
}

RIN_API char* rin_indsin_session_export_html(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok || !sess->state.fabric) return nullptr;
    int W = 0, H = 0;
    auto draw = paintSessionFrame(sess, W, H);
    indsin::CanvasInfo ci = indsin::resolveCanvas(sess->state.fabric, sess->hostBg);
    return dupToC(indsin::drawListToHtml(draw, W, H, "indsin preview", ci.color));
}

// Default background (host setting). "" / NULL clears it (back to the theme's `background`). Any color literal
// the engine understands is accepted (#RGB, #RRGGBB, rgb(), hsl(), CSS names); returns 0 on an unparsable value.
RIN_API int rin_indsin_session_set_default_background(void* sessionPtr, const char* color) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess) return 0;
    std::string c = color ? color : "";
    if (c.empty()) { sess->hostBg = indsin::HostBackground{}; return 1; }
    // Gradient default: "#0B1020,#4B2A8A[,#…][,angle]" — 2..16 colors + optional CSS angle.
    if (c.find(',') != std::string::npos) {
        std::vector<indsin::Color> st; double ang = 180;
        if (!indsin::parseGradientSpec(c, st, ang)) return 0;
        sess->hostBg = indsin::HostBackground{}; sess->hostBg.set = true; sess->hostBg.color = st[0];
        sess->hostBg.stops = st; sess->hostBg.angle = ang;
        return 1;
    }
    if (!indsin::looksLikeHexColor(c)) return 0;
    sess->hostBg = indsin::HostBackground{};
    sess->hostBg.set = true;
    sess->hostBg.color = indsin::parseHexColor(c, indsin::Color{0, 0, 0});
    return 1;
}

// Host resize: new base width (and optional viewport height, 0 = keep). A root `screen=`/`canvas_width=` still wins.
RIN_API void rin_indsin_session_resize(void* sessionPtr, int width, int viewportHeight) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess) return;
    if (width > 0) sess->rootWidth = width;
    if (viewportHeight > 0) sess->viewportHeight = viewportHeight;
    relayout(sess);
}

// {"width","height","background":"#RRGGBB","source":"screen|host|theme"} — what the host should size/clear its surface with.
RIN_API char* rin_indsin_session_canvas_json(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok || !sess->state.fabric) return dupToC(sessionErrorJson(sess));
    return dupToC(canvasJson(sess));
}

RIN_API void rin_indsin_session_set_viewport(void* sessionPtr, int viewportHeight) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess) return;
    sess->viewportHeight = viewportHeight > 0 ? viewportHeight : 844;
    relayout(sess); // Dialog centering / Tooltip clamping depends on this -- redo immediately
}


// ---- Design System v2 / Audit / Introspection exports (see docs/indsin_expansion.md) ----------

// Accessibility + layout audit of the session's current (laid-out) Fabric. JSON shape:
// {"ok","score","nodes","errors","warnings","issues":[{code,severity,strandId,kind,name,path,line,message,hint}]}.
RIN_API char* rin_indsin_session_audit_json(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::AuditOptions opt;
    opt.screenWidth = (double)canvasWidthOf(sess);
    return dupToC(indsin::auditReportToJson(indsin::auditFabric(sess->state.fabric, opt)));
}
// Node/depth/kind/category statistics of the current Fabric.
RIN_API char* rin_indsin_session_stats_json(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    return dupToC(indsin::fabricStatsJson(indsin::fabricStats(sess->state.fabric)));
}
// Plain-text indented outline of the current Fabric (debugging / CI logs).
RIN_API char* rin_indsin_session_outline(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    return dupToC(indsin::fabricOutline(sess->state.fabric));
}
// Every design-token scale + the active theme + registered theme names.
RIN_API char* rin_indsin_tokens_json(void) { return dupToC(indsin::tokensToJson()); }
// Component taxonomy (kind, category, a11y role, interactive/overlay flags).
RIN_API char* rin_indsin_catalog_json(void) { return dupToC(indsin::kindCatalogJson()); }
// Tonal palette (50..900) around a seed color literal; invalid seed -> error object.
RIN_API char* rin_indsin_palette_json(const char* seedLiteral) {
    indsin::Color c;
    if (!seedLiteral || !rincolor::tryParseColor(seedLiteral, c)) return dupToC("{\"error\":\"invalid color\"}");
    return dupToC(indsin::paletteToJson(indsin::tonalPalette(c)));
}
// Full theme generated from one seed color, plus its WCAG report: {"theme":{...},"report":{...}}.
RIN_API char* rin_indsin_theme_from_seed_json(const char* seedLiteral, int dark) {
    indsin::Color c;
    if (!seedLiteral || !rincolor::tryParseColor(seedLiteral, c)) return dupToC("{\"error\":\"invalid color\"}");
    indsin::Theme t = indsin::themeFromSeed(dark ? "GeneratedDark" : "GeneratedLight", c, dark != 0);
    std::ostringstream os;
    os << "{\"theme\":" << indsin::themeToJson(t) << ",\"report\":" << indsin::themeReportToJson(indsin::validateTheme(t)) << "}";
    return dupToC(os.str());
}
// WCAG report for a REGISTERED theme by name (built-in or @theme-declared).
// ---- Web Layout Concepts ----
RIN_API char* rin_indsin_query_selector_json(void* sessionPtr, const char* selector) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    std::vector<indsin::StrandPtr> hits;
    if (!selector || !indsin::webQuery(sess->state.fabric, selector, hits)) return dupToC("{\"error\":\"invalid selector\"}");
    std::ostringstream os; os << "[";
    for (size_t i = 0; i < hits.size(); i++) {
        const auto& h = hits[i];
        os << (i ? "," : "") << "{\"name\":\"" << indsin::jsonEscape(h->name) << "\",\"kind\":\"" << indsin::strandKindName(h->kind)
           << "\",\"x\":" << h->geometry.x << ",\"y\":" << h->geometry.y << ",\"w\":" << h->geometry.w << ",\"h\":" << h->geometry.h << "}";
    }
    os << "]";
    return dupToC(os.str());
}
RIN_API char* rin_indsin_collisions_json(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    std::vector<indsin::WebCollision> cs;
    indsin::webDetectCollisions(sess->state.fabric, cs, true);
    std::ostringstream os; os << "[";
    for (size_t i = 0; i < cs.size(); i++)
        os << (i ? "," : "") << "{\"a\":\"" << indsin::jsonEscape(cs[i].nameA) << "\",\"b\":\"" << indsin::jsonEscape(cs[i].nameB)
           << "\",\"x\":" << cs[i].overlap.x << ",\"y\":" << cs[i].overlap.y << ",\"w\":" << cs[i].overlap.w << ",\"h\":" << cs[i].overlap.h << "}";
    os << "]";
    return dupToC(os.str());
}
RIN_API char* rin_indsin_space_report_json(void* sessionPtr, const char* name) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::StrandPtr n = (name && *name) ? indsin::findAny(sess->state.fabric, [&](const indsin::StrandPtr& s) { return s->name == name; }) : sess->state.fabric;
    if (!n) return dupToC("{\"error\":\"not found\"}");
    return dupToC(indsin::webSpaceReportJson(*n));
}
RIN_API int rin_indsin_scroll_to(void* sessionPtr, const char* name, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok || !name) return 0;
    if (!indsin::webSetScroll(sess->state.fabric, name, x, y)) return 0;
    relayout(sess);
    return 1;
}
RIN_API void rin_indsin_set_scroll_window(void* sessionPtr, double scrollX, double scrollY) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return;
    indsin::webPostLayout(sess->indsinEngine, sess->state.fabric, scrollX, scrollY);
}
RIN_API char* rin_indsin_validate_theme_json(const char* themeName) {
    auto& reg = indsin::themeRegistry();
    auto it = reg.themes.find(themeName ? themeName : "");
    if (it == reg.themes.end()) return dupToC("{\"error\":\"unknown theme\"}");
    return dupToC(indsin::themeReportToJson(indsin::validateTheme(it->second)));
}

} // extern "C"


// ---- Media: نتائج المضيف (rin_indsin_media.h) ------------------------------------------------------
RIN_API char* rin_indsin_session_media_picked(void* sessionPtr, const char* cell, const char* itemsJson,
                                              const char* kind, int multiple, double maxMb, int append) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    SessionRinHost rinHost(sess);
    std::string c = cell ? cell : "";
    if (c.empty()) return dupToC("{\"ok\":false,\"error\":\"empty media cell\"}");
    indsin::media::Kind k = indsin::media::Kind::ANY;
    if (kind) indsin::media::parseKind(kind, k);
    std::string err;
    auto changed = indsin::media::applyPicked(sess->state.warp, c, itemsJson ? itemsJson : "[]", k, maxMb,
                                              multiple != 0, append != 0, err);
    applyWarpNames(sess, changed);
    std::string extra = ",\"handled\":true" + changedJson(changed);
    if (!err.empty()) extra += ",\"error\":\"" + indsin::jsonEscape(err) + "\"";
    return dupToC(fabricEnvelope(sess, extra));
}

RIN_API char* rin_indsin_session_media_progress(void* sessionPtr, const char* cell, const char* status,
                                                double progress, const char* detail) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    SessionRinHost rinHost(sess);
    std::string c = cell ? cell : "";
    if (c.empty()) return dupToC("{\"ok\":false,\"error\":\"empty media cell\"}");
    auto changed = indsin::media::applyProgress(sess->state.warp, c, status ? status : "error", progress,
                                                detail ? detail : "");
    applyWarpNames(sess, changed);
    return dupToC(fabricEnvelope(sess, ",\"handled\":true" + changedJson(changed)));
}

// ============================================================================================
// Rin ⇄ indsin: واجهة من لغة Rin نفسها لتركيب الواجهات وتشغيلها (أي برنامج Rin، لا indsin فقط).
//   let h = indsin.mount(source[, width]);   // معرّف جلسة (رقم)
//   indsin.input(h, "email", "a@b.io");       // يكتب في خلية warp ويعيد {ok, changed, error}
//   indsin.set(h, "count", 5); indsin.get(h, "count");
//   indsin.tap(h, x, y) · indsin.tapText(h, "Sign in")  -> {handled, changed:[...], error}
//   indsin.texts(h) -> كل النصوص الظاهرة · indsin.find(h, "text") -> {x,y,w,h} أو nil
//   indsin.png(h, path) · indsin.html(h) · indsin.json(h) · indsin.free(h)
// تُثبَّت عبر rin::interpreterInstallers() فتتوفّر لكل Interpreter في المحرّك.
// ============================================================================================
namespace {
std::unordered_map<int, IndsinSession*>& uiSessions() { static std::unordered_map<int, IndsinSession*> m; return m; }
int g_uiNextId = 1;

IndsinSession* uiSession(const char* fn, std::vector<rin::Value>& a, size_t i, int line) {
    if (a.size() <= i || a[i].type != rin::Value::Type::NUMBER)
        throw rin::RinError(std::string("'") + fn + "' expects a session handle (from indsin.mount) as argument " + std::to_string(i + 1), line);
    auto it = uiSessions().find((int)a[i].number);
    if (it == uiSessions().end()) throw rin::RinError(std::string("'") + fn + "': unknown or freed session handle", line);
    return it->second;
}
std::string uiStr(const char* fn, std::vector<rin::Value>& a, size_t i, int line) {
    if (a.size() <= i || a[i].type != rin::Value::Type::STRING)
        throw rin::RinError(std::string("'") + fn + "' expects a string as argument " + std::to_string(i + 1), line);
    return a[i].str;
}
rin::Value uiMap(const std::vector<std::pair<std::string, rin::Value>>& kv) {
    auto m = std::make_shared<rin::MapData>();
    for (auto& p : kv) m->push_back({rin::Value::string(p.first), p.second});
    return rin::Value::makeMap(m);
}
// يحوّل غلاف JSON (tap/set_input) إلى قاموس Rin: handled / changed / error
rin::Value uiEnvelope(const std::string& j) {
    auto arr = std::make_shared<rin::ArrayData>();
    size_t p = j.find("\"changed\":[");
    if (p != std::string::npos) {
        p += 11;
        size_t e = j.find(']', p);
        std::string body = j.substr(p, e == std::string::npos ? 0 : e - p), cur; bool in = false;
        for (char c : body) { if (c == '"') { if (in) { arr->push_back(rin::Value::string(cur)); cur.clear(); } in = !in; } else if (in) cur += c; }
    }
    std::string err;
    size_t q = j.find("\"error\":\"");
    if (q != std::string::npos) {
        q += 9;
        for (size_t i = q; i < j.size() && j[i] != '"'; i++) { if (j[i] == '\\' && i + 1 < j.size()) i++; err += j[i]; }
    }
    return uiMap({{"handled", rin::Value::boolean_(j.find("\"handled\":true") != std::string::npos)},
                  {"changed", rin::Value::makeArray(arr)}, {"error", rin::Value::string(err)}});
}
// يجمع النصوص الظاهرة (Text → text، Button → label). `want`: نص مطابق، أو "#name" لاسم العنصر، أو placeholder حقل.
void uiCollect(const indsin::StrandPtr& s, const std::string* want, std::vector<std::string>& out, indsin::StrandPtr* found) {
    if (!s) return;
    if (s->attrStr("visible", "true") == "false") return;
    std::string t;
    if (s->kind == indsin::StrandKind::TEXT) t = s->attrStr("text", "");
    else if (s->kind == indsin::StrandKind::BUTTON) t = s->attrStr("label", s->attrStr("text", ""));
    if (!t.empty()) out.push_back(t);
    if (want && found && !*found) {
        bool byName = !want->empty() && (*want)[0] == '#' && s->name == want->substr(1);
        bool byText = !t.empty() && t == *want;
        bool byHint = (s->kind == indsin::StrandKind::INPUT || s->kind == indsin::StrandKind::TEXTAREA) && s->attrStr("placeholder", "") == *want && !want->empty();
        if (byName || byText || byHint) *found = s;
    }
    for (auto& c : s->children) uiCollect(c, want, out, found);
}
rin::Value uiTapAt(IndsinSession* s, double x, double y) {
    char* r = rin_indsin_session_tap(s, x, y);
    std::string j = r ? r : ""; rin_free_string(r);
    return uiEnvelope(j);
}
} // namespace

static void installRinUiNatives(rin::Interpreter& interp) {
    using rin::Value; using Args = std::vector<rin::Value>;
    interp.registerHostNative("indsin.mount", [](Args& a, int line) {
        std::string src = uiStr("indsin.mount", a, 0, line);
        int w = a.size() > 1 && a[1].type == Value::Type::NUMBER ? (int)a[1].number : 390;
        auto* s = static_cast<IndsinSession*>(rin_indsin_session_create(src.c_str(), w));
        if (!s) throw rin::RinError("'indsin.mount': could not create a session", line);
        if (!s->state.ok) {
            std::string e = sessionErrorJson(s); delete s;
            throw rin::RinError("'indsin.mount': " + e, line);
        }
        int id = g_uiNextId++;
        uiSessions()[id] = s;
        return Value::num(id);
    });
    interp.registerHostNative("indsin.free", [](Args& a, int line) {
        if (a.empty() || a[0].type != Value::Type::NUMBER) throw rin::RinError("'indsin.free' expects a session handle", line);
        auto it = uiSessions().find((int)a[0].number);
        if (it == uiSessions().end()) return Value::boolean_(false);
        rin_indsin_session_free(it->second); uiSessions().erase(it);
        return Value::boolean_(true);
    });
    interp.registerHostNative("indsin.input", [](Args& a, int line) {
        auto* s = uiSession("indsin.input", a, 0, line);
        std::string n = uiStr("indsin.input", a, 1, line), t = uiStr("indsin.input", a, 2, line);
        char* r = rin_indsin_session_set_input(s, n.c_str(), t.c_str()); std::string j = r ? r : ""; rin_free_string(r);
        return uiEnvelope(j);
    });
    interp.registerHostNative("indsin.set", [](Args& a, int line) {
        auto* s = uiSession("indsin.set", a, 0, line);
        std::string n = uiStr("indsin.set", a, 1, line);
        if (a.size() < 3) throw rin::RinError("'indsin.set' expects (handle, cell, value)", line);
        if (!s->state.warp.has(n)) return uiMap({{"handled", Value::boolean_(false)}, {"changed", Value::makeArray(std::make_shared<rin::ArrayData>())},
                                                 {"error", Value::string("'" + n + "' is not a warp cell")}});
        indsin::Value v = indsin::rinValueToIndsin(a[2]);
        s->state.warp.set(n, v);
        indsin::Shuttle sh; sh.applyWarpChange(n, s->state.warp, s->state.subs, s->index);
        indsin::recomputeHashes(s->state.fabric);
        relayout(s);
        auto ch = std::make_shared<rin::ArrayData>(); ch->push_back(Value::string(n));
        return uiMap({{"handled", Value::boolean_(true)}, {"changed", Value::makeArray(ch)}, {"error", Value::string("")}});
    });
    interp.registerHostNative("indsin.get", [](Args& a, int line) {
        auto* s = uiSession("indsin.get", a, 0, line);
        std::string n = uiStr("indsin.get", a, 1, line);
        if (!s->state.warp.has(n)) return Value::nil();
        return indsin::indsinValueToRin(s->state.warp.get(n));
    });
    interp.registerHostNative("indsin.tap", [](Args& a, int line) {
        auto* s = uiSession("indsin.tap", a, 0, line);
        if (a.size() < 3 || a[1].type != Value::Type::NUMBER || a[2].type != Value::Type::NUMBER)
            throw rin::RinError("'indsin.tap' expects (handle, x, y)", line);
        return uiTapAt(s, a[1].number, a[2].number);
    });
    interp.registerHostNative("indsin.find", [](Args& a, int line) {
        auto* s = uiSession("indsin.find", a, 0, line);
        std::string want = uiStr("indsin.find", a, 1, line);
        std::vector<std::string> all; indsin::StrandPtr f;
        uiCollect(s->state.fabric, &want, all, &f);
        if (!f) return Value::nil();
        const auto& g = f->geometry;
        return uiMap({{"x", Value::num(g.x)}, {"y", Value::num(g.y)}, {"w", Value::num(g.w)}, {"h", Value::num(g.h)},
                      {"cx", Value::num(g.x + g.w / 2)}, {"cy", Value::num(g.y + g.h / 2)}});
    });
    interp.registerHostNative("indsin.tapText", [](Args& a, int line) {
        auto* s = uiSession("indsin.tapText", a, 0, line);
        std::string want = uiStr("indsin.tapText", a, 1, line);
        std::vector<std::string> all; indsin::StrandPtr f;
        uiCollect(s->state.fabric, &want, all, &f);
        if (!f) return uiMap({{"handled", Value::boolean_(false)}, {"changed", Value::makeArray(std::make_shared<rin::ArrayData>())},
                              {"error", Value::string("no element with text `" + want + "`")}});
        return uiTapAt(s, f->geometry.x + f->geometry.w / 2, f->geometry.y + f->geometry.h / 2);
    });
    interp.registerHostNative("indsin.texts", [](Args& a, int line) {
        auto* s = uiSession("indsin.texts", a, 0, line);
        std::vector<std::string> all; uiCollect(s->state.fabric, nullptr, all, nullptr);
        auto arr = std::make_shared<rin::ArrayData>();
        for (auto& t : all) arr->push_back(Value::string(t));
        return Value::makeArray(arr);
    });
    interp.registerHostNative("indsin.png", [](Args& a, int line) {
        auto* s = uiSession("indsin.png", a, 0, line);
        return Value::boolean_(rin_indsin_session_export_png(s, uiStr("indsin.png", a, 1, line).c_str()) != 0);
    });
    interp.registerHostNative("indsin.html", [](Args& a, int line) {
        auto* s = uiSession("indsin.html", a, 0, line);
        char* r = rin_indsin_session_export_html(s); std::string h = r ? r : ""; rin_free_string(r);
        return Value::string(h);
    });
    interp.registerHostNative("indsin.json", [](Args& a, int line) {
        auto* s = uiSession("indsin.json", a, 0, line);
        char* r = rin_indsin_session_render_json(s); std::string h = r ? r : ""; rin_free_string(r);
        return Value::string(h);
    });
}
static const bool g_rinUiInstalled = [] {
    rin::interpreterInstallers().push_back([](rin::Interpreter& i) { indsin::registerIndsinRinLib(i); installRinUiNatives(i); });
    return true;
}();
