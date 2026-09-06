// loom/rin_loom_c_api.cpp
#include "rin_loom_c_api.h"
#include "rin_loom_pipeline.h"
#include "rin_loom_needle.h"
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <memory>

namespace {
char* dupToC(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

// ---- Loomtime session state ----
struct LoomSession {
    loom::PipelineResult state;
    loom::Loom loomEngine;
    std::unordered_map<loom::StrandId, loom::StrandPtr> index; // rebuilt after any structural change
    int rootWidth = 390;
    int viewportHeight = 844; // Overlay Engine: see rin_loom_session_set_viewport's doc comment
    loom::OverlayLayer overlayLayer; // rebuilt every relayout() -- see rin_loom_overlay.h

    // Persistent interpreter for this session's onTap handlers (see dispatchTap's doc comment in
    // rin_loom_needle.h)... One LoomSession == one screen/session == one Interpreter, seeded on
    // first tap (or on cold render -- see rin_loom_session_create's real-runtime comment below).
    //
    // Held via unique_ptr rather than by value: rin::Interpreter has an implicitly-deleted copy
    // assignment operator (it owns non-copy-assignable state), so `sess->interp = Interpreter()`
    // (needed on the "previous run failed, retry from a clean Interpreter" path in
    // rin_loom_session_update_source) cannot compile against a plain value member -- reassigning
    // the pointee via std::make_unique sidesteps that entirely.
    std::unique_ptr<rin::Interpreter> interp = std::make_unique<rin::Interpreter>();
    bool interpSeeded = false;
};

void relayout(LoomSession* sess) {
    if (!sess->state.ok || !sess->state.fabric) return;
    sess->loomEngine = loom::Loom{}; // fresh stats per call; Tension caching lives on the Strands themselves
    sess->loomEngine.layout(sess->state.fabric, loom::Constraints{0, (double)sess->rootWidth, 0, 1e9}, 0, 0);
    // Overlay Engine second pass: re-homes every open Dialog / anchored Tooltip against the
    // viewport now that the whole tree (including their own content boxes) has been measured.
    sess->overlayLayer = loom::buildOverlayLayer(sess->loomEngine, sess->state.fabric,
                                                  (double)sess->rootWidth, (double)sess->viewportHeight);
}
void rebuildIndex(LoomSession* sess) {
    sess->index.clear();
    if (sess->state.ok && sess->state.fabric) loom::buildIndex(sess->state.fabric, sess->index);
}
// Overlay Engine (rin_loom_overlay.h): serializes the current overlay layer so a renderer can
// draw scrim + correct z-order without having to re-derive "which Strand ids are overlays" by
// re-walking attrs itself -- the JSON already has the corrected on-screen x/y for each overlay
// Strand (buildOverlayLayer() mutated s->geometry in place, same field fabricToJson reads), so
// this array only needs to carry what fabricToJson's generic per-Strand shape doesn't: which ids
// are overlays, their paint/stacking order, and their scrim/modal metadata.
std::string overlayLayerJson(const loom::OverlayLayer& layer) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < layer.entries.size(); i++) {
        const auto& e = layer.entries[i];
        if (i) os << ",";
        os << "{\"id\":" << (e.strand ? e.strand->id : 0)
           << ",\"name\":\"" << loom::jsonEscape(e.strand ? e.strand->name : "") << "\""
           << ",\"kind\":\"" << (e.kind == loom::OverlayKind::DIALOG ? "dialog" : "tooltip") << "\""
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
std::string fabricEnvelope(LoomSession* sess, const std::string& extraFields) {
    std::ostringstream os;
    loom::Dye dye;
    auto draw = dye.paintWithOverlay(sess->state.fabric, sess->overlayLayer);
    os << "{\"ok\":true" << extraFields
       << ",\"strandsMeasured\":" << sess->loomEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << sess->loomEngine.stats.cacheHits
       << ",\"overlays\":" << overlayLayerJson(sess->overlayLayer)
       << ",\"paint\":" << loom::drawListToJsonString(draw)
       << ",\"fabric\":" << loom::fabricToJsonString(sess->state.fabric) << "}";
    return os.str();
}
std::string sessionErrorJson(LoomSession* sess) {
    std::ostringstream os;
    os << "{\"ok\":false,\"error\":\"" << loom::jsonEscape(sess ? sess->state.errorMessage : "null session")
       << "\",\"line\":" << (sess ? sess->state.errorLine : 0) << "}";
    return os.str();
}
}

extern "C" {

RIN_API char* rin_loom_render_json(const char* source, int rootWidth) {
    std::string src = source ? source : "";
    if (rootWidth <= 0) rootWidth = 390;

    loom::PipelineResult r = loom::runColdPipeline(src);
    if (!r.ok) {
        std::ostringstream os;
        os << "{\"error\":\"" << loom::jsonEscape(r.errorMessage) << "\",\"line\":" << r.errorLine << "}";
        return dupToC(os.str());
    }

    loom::Loom loomEngine;
    loomEngine.layout(r.fabric, loom::Constraints{0, (double)rootWidth, 0, 1e9}, 0, 0);

    loom::Dye dye;
    auto draw = dye.paint(r.fabric);
    std::ostringstream os;
    os << "{\"ok\":true,\"strandsMeasured\":" << loomEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << loomEngine.stats.cacheHits
       << ",\"paint\":" << loom::drawListToJsonString(draw)
       << ",\"fabric\":" << loom::fabricToJsonString(r.fabric) << "}";
    return dupToC(os.str());
}

RIN_API char* rin_loom_render_container_json(const char* source, const char* containerName, int rootWidth) {
    std::string src = source ? source : "";
    std::string name = containerName ? containerName : "";
    if (rootWidth <= 0) rootWidth = 390;

    loom::PipelineResult r = loom::runColdPipelineForContainer(src, name);
    if (!r.ok) {
        std::ostringstream os;
        os << "{\"error\":\"" << loom::jsonEscape(r.errorMessage) << "\",\"line\":" << r.errorLine << "}";
        return dupToC(os.str());
    }

    loom::Loom loomEngine;
    loomEngine.layout(r.fabric, loom::Constraints{0, (double)rootWidth, 0, 1e9}, 0, 0);

    std::ostringstream os;
    os << "{\"ok\":true,\"container\":\"" << loom::jsonEscape(name) << "\""
       << ",\"strandsMeasured\":" << loomEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << loomEngine.stats.cacheHits
       << ",\"fabric\":" << loom::fabricToJsonString(r.fabric) << "}";
    return dupToC(os.str());
}

RIN_API void* rin_loom_session_create(const char* source, int rootWidth) {
    auto* sess = new (std::nothrow) LoomSession();
    if (!sess) return nullptr;
    sess->rootWidth = rootWidth > 0 ? rootWidth : 390;
    // Real Runtime Session (see rin_loom_pipeline.h's runColdPipelineWithRuntime): the session's
    // own persistent rin::Interpreter runs the program for real right here at creation time --
    // not a throwaway one -- so the exact same Interpreter instance (with its containers/chat
    // state/etc. already populated by this real run) is reused by every subsequent onTap via
    // Needle, instead of a brand-new empty Interpreter being created per tap or per session.
    sess->state = loom::runColdPipelineWithRuntime(source ? source : "", *sess->interp);
    // Mark seeded immediately: the run() above already executed the whole program (container
    // bodies, top-level funs hoisted, etc.), so Needle's own lazy "seed once on first tap" path
    // in dispatchTap (rin_loom_needle.h) must NOT run() this same Interpreter a second time --
    // that would double every side effect (chat messages appended twice, docs inserted twice, ...).
    if (sess->state.ok) { sess->interpSeeded = true; relayout(sess); rebuildIndex(sess); }
    return sess;
}

RIN_API void* rin_loom_session_create_for_container(const char* source, const char* containerName, int rootWidth) {
    auto* sess = new (std::nothrow) LoomSession();
    if (!sess) return nullptr;
    sess->rootWidth = rootWidth > 0 ? rootWidth : 390;
    sess->state = loom::runColdPipelineForContainerWithRuntime(source ? source : "", containerName ? containerName : "", *sess->interp);
    if (sess->state.ok) { sess->interpSeeded = true; relayout(sess); rebuildIndex(sess); } // see rin_loom_session_create's comment
    return sess;
}

RIN_API char* rin_loom_session_render_json(void* sessionPtr) {
    auto* sess = static_cast<LoomSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));
    return dupToC(fabricEnvelope(sess, ""));
}

RIN_API char* rin_loom_session_tap(void* sessionPtr, double x, double y) {
    auto* sess = static_cast<LoomSession*>(sessionPtr);
    if (!sess || !sess->state.ok) return dupToC(sessionErrorJson(sess));

    loom::TapResult tap = loom::dispatchTapWithOverlay(sess->state.fabric, sess->state.warp,
                                                        sess->state.program, sess->overlayLayer, x, y,
                                                        nullptr, nullptr,
                                                        sess->interp.get(), &sess->interpSeeded);

    if (!tap.changedWarpNames.empty()) {
        loom::Shuttle shuttle;
        for (auto& name : tap.changedWarpNames) {
            shuttle.applyWarpChange(name, sess->state.warp, sess->state.subs, sess->index);
        }
        // Tension's cache is keyed on each Strand's contentHash; applyWarpChange() above mutates
        // a.value directly (by design -- see its own comment) but does not touch contentHash, so
        // without this, relayout() below can short-circuit on the stale cached geometry for any
        // Strand whose size actually depends on the changed value (e.g. Text whose text= grew
        // from a single digit to two digits). Recomputing before layout is what
        // rin_loom_strand.h's recomputeHashes() itself says it exists for.
        loom::recomputeHashes(sess->state.fabric);
        relayout(sess); // an attribute change (e.g. a longer counter string) may resize its Strand
    }

    std::ostringstream extra;
    extra << ",\"handled\":" << (tap.handled ? "true" : "false")
          << ",\"targetId\":" << tap.targetId
          << ",\"handler\":\"" << loom::jsonEscape(tap.handlerDescription) << "\""
          << ",\"changed\":[";
    for (size_t i = 0; i < tap.changedWarpNames.size(); i++) {
        if (i) extra << ",";
        extra << "\"" << loom::jsonEscape(tap.changedWarpNames[i]) << "\"";
    }
    extra << "]";
    if (!tap.error.empty()) extra << ",\"error\":\"" << loom::jsonEscape(tap.error) << "\"";

    return dupToC(fabricEnvelope(sess, extra.str()));
}

RIN_API char* rin_loom_session_update_source(void* sessionPtr, const char* newSource) {
    auto* sess = static_cast<LoomSession*>(sessionPtr);
    if (!sess) return dupToC("{\"ok\":false,\"error\":\"null session\"}");
    std::string src = newSource ? newSource : "";

    if (!sess->state.ok) {
        // Never had a good Fabric to diff against -- just retry the cold pipeline outright, still
        // through the session's own persistent Interpreter (not a throwaway one) so state stays
        // consistent with whatever taps run next -- see rin_loom_session_create's comment.
        sess->interp = std::make_unique<rin::Interpreter>(); // previous run (if any) never completed successfully; start clean
        sess->interpSeeded = false;
        sess->state = loom::runColdPipelineWithRuntime(src, *sess->interp);
        if (sess->state.ok) {
            sess->interpSeeded = true;
            relayout(sess); rebuildIndex(sess);
            return dupToC(fabricEnvelope(sess, ",\"handled\":false,\"changed\":[]"));
        }
        return dupToC(sessionErrorJson(sess));
    }

    std::string err; int errLine = 0;
    loom::runHotPipeline(sess->state, src, err, errLine);
    // runHotPipeline leaves the previous Fabric completely untouched on error (Snag containment),
    // so sess->state.fabric below is either the freshly-diffed tree or still the last-good one.
    rebuildIndex(sess);
    relayout(sess);

    std::ostringstream extra;
    extra << ",\"handled\":false,\"changed\":[]";
    if (!err.empty()) {
        extra << ",\"error\":\"" << loom::jsonEscape(err) << "\",\"line\":" << errLine << ",\"snag\":true";
    }
    return dupToC(fabricEnvelope(sess, extra.str()));
}

RIN_API void rin_loom_session_free(void* sessionPtr) {
    delete static_cast<LoomSession*>(sessionPtr);
}

RIN_API void rin_loom_session_set_viewport(void* sessionPtr, int viewportHeight) {
    auto* sess = static_cast<LoomSession*>(sessionPtr);
    if (!sess) return;
    sess->viewportHeight = viewportHeight > 0 ? viewportHeight : 844;
    relayout(sess); // Dialog centering / Tooltip clamping depends on this -- redo immediately
}

} // extern "C"
