// indsin/rin_indsin_c_api.cpp
#include "rin_indsin_c_api.h"
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include "rin_indsin_effects.h"
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <chrono>

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

    // Effects Engine (rin_indsin_effects.h): one animation clock per session, monotonic from
    // whenever the session was created — a plain elapsed-ms counter is all EffectRuntime needs
    // (it only ever compares two of its own readings), so there's no wall-clock/timezone concern.
    std::chrono::steady_clock::time_point clockStart = std::chrono::steady_clock::now();
    indsin::EffectRuntime effectRuntime;
    bool animating = false; // set by relayout() below; read back into every JSON envelope
};

double nowMsFor(IndsinSession* sess) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sess->clockStart).count();
}

void relayout(IndsinSession* sess) {
    if (!sess->state.ok || !sess->state.fabric) return;
    sess->indsinEngine = indsin::Indsin{}; // fresh stats per call; Tension caching lives on the Strands themselves
    sess->indsinEngine.layout(sess->state.fabric, indsin::Constraints{0, (double)sess->rootWidth, 0, 1e9}, 0, 0);
    // Overlay Engine second pass: re-homes every open Dialog / anchored Tooltip against the
    // viewport now that the whole tree (including their own content boxes) has been measured.
    sess->overlayLayer = indsin::buildOverlayLayer(sess->indsinEngine, sess->state.fabric,
                                                  (double)sess->rootWidth, (double)sess->viewportHeight);
    // Effects Engine: applied last, after both passes above, so the geometry it nudges
    // (translate/scale for an in-progress enter animation) is this frame's FINAL geometry —
    // anything painted or exported after this reflects the current animation frame for free.
    sess->animating = indsin::applyEffectsToFabric(sess->state.fabric, sess->effectRuntime, nowMsFor(sess));
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
    indsin::Dye dye;
    auto draw = dye.paintWithOverlay(sess->state.fabric, sess->overlayLayer);
    os << "{\"ok\":true" << extraFields
       << ",\"strandsMeasured\":" << sess->indsinEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << sess->indsinEngine.stats.cacheHits
       << ",\"animating\":" << (sess->animating ? "true" : "false")
       << ",\"overlays\":" << overlayLayerJson(sess->overlayLayer)
       << ",\"paint\":" << indsin::drawListToJsonString(draw)
       << ",\"fabric\":" << indsin::fabricToJsonString(sess->state.fabric) << "}";
    return os.str();
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
    indsinEngine.layout(r.fabric, indsin::Constraints{0, (double)rootWidth, 0, 1e9}, 0, 0);

    indsin::Dye dye;
    auto draw = dye.paint(r.fabric);
    std::ostringstream os;
    os << "{\"ok\":true,\"strandsMeasured\":" << indsinEngine.stats.strandsMeasured
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
    indsinEngine.layout(r.fabric, indsin::Constraints{0, (double)rootWidth, 0, 1e9}, 0, 0);

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
    sess->state = indsin::runColdPipelineForContainerWithRuntime(source ? source : "", containerName ? containerName : "", *sess->interp);
    if (sess->state.ok) { sess->interpSeeded = true; relayout(sess); rebuildIndex(sess); } // see rin_indsin_session_create's comment
    return sess;
}

RIN_API char* rin_indsin_session_render_json(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    return dupToC(fabricEnvelope(sess, ""));
}

RIN_API char* rin_indsin_session_tap(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));

    indsin::TapResult tap = indsin::dispatchTapWithOverlay(sess->state.fabric, sess->state.warp,
                                                        sess->state.program, sess->overlayLayer, x, y,
                                                        nullptr, nullptr,
                                                        sess->interp.get(), &sess->interpSeeded);

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

    return dupToC(fabricEnvelope(sess, extra.str()));
}

// Shared tail for long-press/double-tap/hover below: applies a dispatched indsin::TapResult exactly
// the same way rin_indsin_session_tap does (Warp changes -> Shuttle -> recomputeHashes -> relayout,
// then the {"handled":...,"targetId":...,"handler":...,"changed":[...]} envelope fields) so all
// four gesture endpoints stay byte-for-byte consistent in shape. `error` is only ever set when a
// handler was actually found but failed at runtime -- same convention as the tap endpoint.
char* respondToGesture(IndsinSession* sess, const indsin::TapResult& g) {
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

    return dupToC(fabricEnvelope(sess, extra.str()));
}

RIN_API char* rin_indsin_session_long_press(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::TapResult g = indsin::dispatchLongPressWithOverlay(sess->state.fabric, sess->state.warp,
                                                            sess->state.program, sess->overlayLayer, x, y,
                                                            nullptr, nullptr,
                                                            sess->interp.get(), &sess->interpSeeded);
    return respondToGesture(sess, g);
}

RIN_API char* rin_indsin_session_double_tap(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::TapResult g = indsin::dispatchDoubleTapWithOverlay(sess->state.fabric, sess->state.warp,
                                                            sess->state.program, sess->overlayLayer, x, y,
                                                            nullptr, nullptr,
                                                            sess->interp.get(), &sess->interpSeeded);
    return respondToGesture(sess, g);
}

RIN_API char* rin_indsin_session_hover(void* sessionPtr, double x, double y, int entering) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    indsin::TapResult g = indsin::dispatchHoverWithOverlay(sess->state.fabric, sess->state.warp,
                                                        sess->state.program, sess->overlayLayer, x, y,
                                                        entering != 0, nullptr, nullptr,
                                                        sess->interp.get(), &sess->interpSeeded);
    return respondToGesture(sess, g);
}

RIN_API char* rin_indsin_session_tick(void* sessionPtr) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    relayout(sess); // re-applies the Effects Engine at "now"; sess->animating reflects this tick
    return dupToC(fabricEnvelope(sess, ",\"handled\":false,\"changed\":[]"));
}

RIN_API char* rin_indsin_session_update_source(void* sessionPtr, const char* newSource) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess) return dupToC("{\"ok\":false,\"error\":\"null session\"}");
    std::string src = newSource ? newSource : "";

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

    indsin::Dye dye;
    auto draw = dye.paintWithOverlay(sess->state.fabric, sess->overlayLayer);
    int W = sess->rootWidth;
    int H = (int)std::ceil(sess->state.fabric->geometry.h);
    if (H <= 0) H = sess->viewportHeight; // degenerate/empty Fabric: fall back to a sane canvas
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

    indsin::Dye dye;
    auto draw = dye.paintWithOverlay(sess->state.fabric, sess->overlayLayer);
    int W = sess->rootWidth;
    int H = (int)std::ceil(sess->state.fabric->geometry.h);
    if (H <= 0) H = sess->viewportHeight;
    auto buf = indsin::rasterizeToBuffer(draw, W, H);
    return indsin::writePNG(path, W, H, buf) ? 1 : 0;
}

RIN_API void rin_indsin_session_set_viewport(void* sessionPtr, int viewportHeight) {
    auto* sess = static_cast<IndsinSession*>(sessionPtr);
    if (!sess) return;
    sess->viewportHeight = viewportHeight > 0 ? viewportHeight : 844;
    relayout(sess); // Dialog centering / Tooltip clamping depends on this -- redo immediately
}

} // extern "C"
