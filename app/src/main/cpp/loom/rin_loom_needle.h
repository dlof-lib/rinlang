// loom/rin_loom_needle.h — Needle: the event/gesture/hit-test engine (see the architecture doc's
// §10). This is the piece that was previously only a comment ("dispatched by Needle") in
// rin_loom_eval.h: it turns a tap point into (a) the topmost Strand under it that actually carries
// an `onTap` attribute, and (b) a *real* execution of that handler.
//
// Two execution paths, tried in order:
//   1. If `onTap`'s callee names a top-level `fun` in the program, it's run for real through
//      rin::Interpreter::callTopLevelFunction — full language semantics, including `while` loops,
//      recursion, etc. This is the fix for onTap handlers that need actual logic, not just a
//      literal reassignment.
//   2. Otherwise, a handful of built-in Warp operations (increment/decrement/toggle/set/show/hide)
//      are supported directly, so a demo like `onTap=increment(count);` works with no `fun` at all --
//      matching the shortcut the Mirror Loom preview already takes (see
//      tools/mirror-loom-preview/mirror_loom_live_preview.html's onSelectStrand/warpIncrement).
//
// Needle does not itself re-layout or re-paint anything -- it only mutates the WarpScope and
// reports which cell names changed; the caller (see loom::Shuttle::applyWarpChange, wired up in
// loom/rin_loom_c_api.cpp's session API) is what turns that into Patch[]/geometry updates.
#pragma once
#include "rin_loom_strand.h"
#include "rin_loom_tokens.h"
#include "rin_loom_overlay.h" // OverlayLayer / hitTestOverlayLayer -- see dispatchTapWithOverlay below
#include "rin_loom_actions.h" // Action Engine -- built-in Warp-cell verb registry (spec §5/§36)
#include "rin_loom_navigation.h" // NavigationManager -- navigate()/back()/replace()/reload() (spec §19)
#include "rin_loom_paint.h"      // Dye + exportFabricToPNG() -- exportPNG()/screenshot()/exportImage() (spec §21-23)
#include "../rin_interpreter.h"
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <cstring>
#include <cctype>

namespace loom {

// ---- hit-testing ----

// Walks `s` from the point outward: children are checked before the node itself, and in reverse
// order, so the most recently painted (topmost / most specific) Strand under (x, y) is found
// first -- e.g. a Button nested inside a Card returns the Button, not the Card. On a hit, `path`
// is filled leaf-to-root (path.front() is the deepest match, path.back() is `s` itself).
inline bool hitTestPath(const StrandPtr& s, double x, double y, std::vector<StrandPtr>& path) {
    for (auto it = s->children.rbegin(); it != s->children.rend(); ++it) {
        if (hitTestPath(*it, x, y, path)) { path.push_back(s); return true; }
    }
    const Rect& g = s->geometry;
    if (x >= g.x && x <= g.x + g.w && y >= g.y && y <= g.y + g.h) { path.push_back(s); return true; }
    return false;
}

// ---- Value bridging between the (minimal) Loomtime Value and the real rin::Value ----

inline rin::Value loomValueToRin(const Value& v) {
    if (v.kind == Value::Kind::NUMBER) return rin::Value::num(v.number);
    return rin::Value::string(v.str);
}
inline Value rinValueToLoom(const rin::Value& v) {
    switch (v.type) {
        case rin::Value::Type::NUMBER: return Value::num(v.number);
        case rin::Value::Type::BOOL:   return Value::txt(v.boolean ? "true" : "false");
        case rin::Value::Type::STRING: return Value::txt(v.str);
        default:                       return Value::txt(v.toDisplayString());
    }
}

struct TapResult {
    bool handled = false;                  // a Strand under (x,y) carried an onTap attribute
    StrandId targetId = 0;                 // that Strand's id (0 if !handled)
    std::string handlerDescription;        // e.g. "increment(count)" -- for a Loupe/debug overlay
    std::vector<std::string> changedWarpNames; // Warp cells the handler actually changed
    std::string error;                     // set only if a handler was found but failed to run
    bool navigated = false;                // true if a navigate()/back()/replace()/reload() ran
    std::string route;                     // the resulting current route when navigated == true
    bool exported = false;                 // true if an exportPNG()/screenshot()/exportImage() ran
    std::string exportedPath;              // the file it was written to, when exported == true
    std::string openedUrl;                 // Link concepts (docs/link.md): set when a tapped
                                            // Strand's href= resolved to an *external* URL --
                                            // opening it is a host-app/JNI concern (same relationship
                                            // `exportedPath` has to the actual file write), same as
                                            // LoomFabricView.kt's onOpenUrl on the Kotlin preview side.
};

// Link concepts (docs/link.md): true for an href value that names an external resource rather
// than an in-app route -- a URL scheme (http:, https:, mailto:, tel:) present. A bare filename
// like "mu.rin" or a path like "/settings" is never external. Mirrors
// LoomFabricView.kt's isExternalTarget() so the native dispatch path and the Kotlin live-preview
// path agree on the same href values.
inline bool isExternalHref(const std::string& target) {
    static const char* schemes[] = {"http:", "https:", "mailto:", "tel:"};
    for (const char* scheme : schemes) {
        size_t len = std::strlen(scheme);
        if (target.size() >= len) {
            bool match = true;
            for (size_t i = 0; i < len; i++) {
                if (std::tolower(static_cast<unsigned char>(target[i])) != scheme[i]) { match = false; break; }
            }
            if (match) return true;
        }
    }
    return false;
}

// Dispatches a tap at (x, y) against `fabricRoot`. `program` is the last successfully parsed
// top-level statement list (PipelineResult::program) -- used to look up a matching `fun`.
// `nav`, if non-null, wires up navigate()/back()/replace()/reload() (spec §19); omitting it (the
// default) simply means those four calls report as unrecognized, same as any other unknown
// callee -- existing call sites that don't pass a NavigationManager keep working unchanged.
// `exportDye`, if non-null, wires up exportPNG()/screenshot()/exportImage() (spec §21-23) the same
// optional-pointer way `nav` wires up navigation -- omit it and those three simply report as
// unrecognized, so existing call sites compile and behave unchanged.
// `persistentInterp`, if non-null, is used *instead of* a throwaway `rin::Interpreter` for the
// "real user fun" path below. This is the fix for the gap documented in README_CHATBOT.md: a
// fresh `rin::Interpreter` per tap has its own empty chatHistoryStore/chatEventHandlers, so
// sendMessage()/botReply() calls made from inside an onTap handler never accumulated anywhere the
// next tap (or chatHistory()) could see. Callers that own a long-lived session (see
// LoomSession::interp in rin_loom_c_api.cpp) pass the same Interpreter instance on every tap, and
// this function makes sure it is seeded exactly once (its own containers/functions/warp cells
// registered via a real `run()`) so that instance's chat/container state persists across taps
// exactly like it already does for two `sendMessage()` calls in the same script. Passing nullptr
// (the default) preserves the exact old per-tap-fresh-interpreter behavior for every existing
// call site.
inline TapResult dispatchTap(const StrandPtr& fabricRoot, WarpScope& warp,
                              const std::vector<rin::StmtPtr>& program, double x, double y,
                              NavigationManager* nav = nullptr, Dye* exportDye = nullptr,
                              rin::Interpreter* persistentInterp = nullptr,
                              bool* persistentInterpSeeded = nullptr) {
    TapResult result;
    if (!fabricRoot) return result;

    std::vector<StrandPtr> path;
    if (!hitTestPath(fabricRoot, x, y, path)) return result; // nothing under the tap

    // Bubble outward (leaf to root) for the nearest Strand that actually declared onTap=...;
    // A disabled Strand (state=disabled or disabled=true) is treated as if it had no onTap at
    // all -- Needle keeps bubbling past it to whatever's underneath, exactly like a real disabled
    // native button consumes no tap. This is real interaction gating, not just a paint dimming.
    const rin::ExprPtr* onTapExpr = nullptr;
    StrandPtr owner;
    for (auto& s : path) {
        if (resolveState(*s) == StrandState::DISABLED) continue;
        for (auto& a : s->attrs) {
            if (a.key == "onTap" && a.rawExpr) { onTapExpr = &a.rawExpr; owner = s; break; }
        }
        if (onTapExpr) break;
    }

    // Link concepts (docs/link.md): `href=` is a plain string attribute (not an onTap
    // expression), so it's resolved directly rather than through the interpreter path below --
    // it never needed one, it's just sugar over navigate()/an external URL. Only tried when no
    // onTap was found, so an explicit onTap= on the same Link still wins, same precedence
    // LoomFabricView.kt's Kotlin-side navigateTargetForTap()/openUrlTargetForTap() already use.
    if (!onTapExpr) {
        for (auto& s : path) {
            if (resolveState(*s) == StrandState::DISABLED) continue;
            std::string href = s->attrStr("href", "");
            if (href.empty()) continue;
            result.handled = true;
            result.targetId = s->id;
            if (isExternalHref(href)) {
                result.openedUrl = href;
            } else if (nav) {
                nav->navigate(href);
                result.navigated = true;
                result.route = nav->current();
            }
            return result;
        }
        return result; // tapped something, but nothing interactive there
    }

    result.handled = true;
    result.targetId = owner->id;

    std::string callee;
    std::vector<rin::ExprPtr> argExprs;
    if (auto call = std::dynamic_pointer_cast<rin::CallExpr>(*onTapExpr)) {
        callee = call->callee;
        argExprs = call->args;
    } else if (auto var = std::dynamic_pointer_cast<rin::VariableExpr>(*onTapExpr)) {
        callee = var->name; // onTap=someHandler; (no parens) -> treated as a zero-arg call
    } else {
        result.error = "onTap must be a handler name or call, e.g. onTap=increment(count);";
        return result;
    }

    // Evaluate each call argument against the *current* Warp state, and remember which ones were
    // bare references to a Warp cell (so a reassigned same-named parameter can be written back).
    std::vector<rin::Value> args;
    std::vector<std::string> aliases;
    for (auto& argExpr : argExprs) {
        if (auto v = std::dynamic_pointer_cast<rin::VariableExpr>(argExpr)) {
            if (warp.has(v->name)) {
                aliases.push_back(v->name);
                args.push_back(loomValueToRin(warp.get(v->name)));
                continue;
            }
        }
        aliases.push_back("");
        args.push_back(loomValueToRin(evalAttrExpr(argExpr, warp, nullptr)));
    }
    std::ostringstream desc;
    desc << callee << "(";
    for (size_t i = 0; i < aliases.size(); i++) { if (i) desc << ","; desc << (aliases[i].empty() ? "expr" : aliases[i]); }
    desc << ")";
    result.handlerDescription = desc.str();

    bool hasUserFn = false;
    for (auto& st : program) {
        if (auto fn = std::dynamic_pointer_cast<rin::FunctionStmt>(st)) {
            if (fn->name == callee) { hasUserFn = true; break; }
        }
    }

    if (hasUserFn) {
        std::unordered_map<std::string, rin::Value> globals;
        for (auto& kv : warp.cells) globals[kv.first] = loomValueToRin(kv.second);

        // Prefer the caller's persistent Interpreter (keeps container.chatbot/etc. state alive
        // across taps); fall back to a throwaway one, matching every pre-existing call site.
        rin::Interpreter localInterp;
        rin::Interpreter& interp = persistentInterp ? *persistentInterp : localInterp;
        if (persistentInterp && persistentInterpSeeded && !*persistentInterpSeeded) {
            // Seed once: a real run() executes every @container.chatbot=/@container.*= block
            // (registering it in the interpreter's own chatHistoryStore/chatEventHandlers) and
            // hoists top-level funs, exactly like running the .rin file from the CLI would.
            interp.run(program);
            *persistentInterpSeeded = true;
        }
        std::string err;
        if (interp.callTopLevelFunction(program, callee, args, aliases, globals, err)) {
            for (auto& kv : globals) {
                Value updated = rinValueToLoom(kv.second);
                if (!(updated == warp.get(kv.first))) {
                    warp.set(kv.first, updated);
                    result.changedWarpNames.push_back(kv.first);
                }
            }
        } else {
            result.error = err;
        }
        return result;
    }

    // No matching `fun` -- navigate()/back()/replace()/reload() first (they need `nav`, which the
    // Action Engine's (argExprs, WarpScope&) signature can't carry), then fall through to the
    // Action Engine's built-in Warp-cell verb registry (rin_loom_actions.h) for everything else.
    if (callee == "navigate" || callee == "back" || callee == "replace" || callee == "reload") {
        if (!nav) {
            result.error = "'" + callee + "()' requires a NavigationManager (pass one to dispatchTap)";
            return result;
        }
        if (callee == "navigate") {
            if (argExprs.empty()) { result.error = "navigate() needs a route argument"; return result; }
            nav->navigate(evalAttrExpr(argExprs[0], warp, nullptr).asString());
        } else if (callee == "replace") {
            if (argExprs.empty()) { result.error = "replace() needs a route argument"; return result; }
            nav->replace(evalAttrExpr(argExprs[0], warp, nullptr).asString());
        } else if (callee == "back") {
            nav->back(); // no-op (never a crash) when there's nothing to go back to
        }
        // reload() falls straight through to the shared "report current route" below.
        result.navigated = true;
        result.route = nav->current();
        return result;
    }

    // exportPNG("screen.png") / screenshot("screen.png") / exportImage("screen.png") export the
    // whole Fabric; exportPNG(profile, "profile.png") (a leading Strand-name identifier) exports
    // just that subtree (spec §23). Same PNG encoder either way (rin_loom_paint.h's writePNG),
    // just a different Strand handed to Dye::paint() -- see exportFabricToPNG().
    if (callee == "exportPNG" || callee == "screenshot" || callee == "exportImage") {
        if (!exportDye) {
            result.error = "'" + callee + "()' requires a Dye (pass one to dispatchTap)";
            return result;
        }
        if (argExprs.empty()) { result.error = callee + "() needs a file path argument"; return result; }
        StrandPtr target = fabricRoot;
        std::string path;
        if (argExprs.size() >= 2) {
            if (auto v = std::dynamic_pointer_cast<rin::VariableExpr>(argExprs[0])) {
                target = findById(fabricRoot, v->name);
                if (!target) {
                    result.error = "'" + callee + "()': no Strand named '" + v->name + "'";
                    return result;
                }
            }
            path = evalAttrExpr(argExprs[1], warp, nullptr).asString();
        } else {
            path = evalAttrExpr(argExprs[0], warp, nullptr).asString();
        }
        if (path.empty()) { result.error = callee + "(): empty file path"; return result; }
        if (!exportFabricToPNG(*exportDye, target, path)) {
            result.error = "'" + callee + "()' failed to write '" + path + "'";
            return result;
        }
        result.exported = true;
        result.exportedPath = path;
        return result;
    }

    ActionOutcome outcome = ActionRegistry::shared().invoke(callee, argExprs, warp);
    if (outcome.recognized) {
        result.error = outcome.error; // empty on success
        result.changedWarpNames = outcome.changedWarpNames;
        return result;
    }

    result.error = "no top-level function named '" + callee +
                   "' (and it doesn't match a built-in Loom action)";
    return result;
}

// ---- Overlay-aware dispatch (Overlay Engine, rin_loom_overlay.h) ----
//
// Tries the overlay layer BEFORE the normal document hit-test, exactly how a real compositor
// checks its topmost surface first:
//   1. A tap that lands on a modal Dialog's scrim (i.e. anywhere in the viewport outside that
//      Dialog's own box while it's open) is CONSUMED right here. It never reaches hitTestPath
//      against the main fabric at all -- that is the actual "block input to what's behind it" an
//      overlay compositor provides, not just a dimmed-looking rectangle drawn on top. If the
//      Dialog is dismissible (the default), the Warp cell behind its `open=` attribute -- found
//      the same way dispatchTap above already resolves a bare Warp-cell reference in an onTap
//      argument -- is set to false, closing it.
//   2. A tap that lands inside an overlay's own box is dispatched with THAT overlay's subtree as
//      the hit-test root, not the whole fabric -- so its own onTap-bearing children (Cancel/
//      Confirm buttons, etc.) stay reachable even though their on-screen geometry may coincide
//      with content underneath them in the main document, and nothing below the overlay is ever
//      considered for this tap.
//   3. Otherwise (no overlay hit at all -- most taps, most of the time) this falls through to the
//      exact same dispatchTap(fabricRoot, ...) call every session used before the Overlay Engine
//      existed, so ordinary interaction is completely unaffected by any of this.
inline TapResult dispatchTapWithOverlay(const StrandPtr& fabricRoot, WarpScope& warp,
                                         const std::vector<rin::StmtPtr>& program,
                                         OverlayLayer& overlayLayer, double x, double y,
                                         NavigationManager* nav = nullptr, Dye* exportDye = nullptr,
                                         rin::Interpreter* persistentInterp = nullptr,
                                         bool* persistentInterpSeeded = nullptr) {
    OverlayHitResult ohit = hitTestOverlayLayer(overlayLayer, x, y);

    if (ohit.blocked) {
        TapResult result;
        result.handled = true; // handled by the scrim -- deliberately not "nothing was there"
        StrandPtr owner = ohit.scrimOwner ? ohit.scrimOwner->strand : nullptr;
        result.targetId = owner ? owner->id : 0;
        result.handlerDescription = "scrim";

        if (owner && ohit.scrimOwner->dismissOnScrimTap) {
            for (auto& a : owner->attrs) {
                if (a.key != "open") continue;
                if (auto v = std::dynamic_pointer_cast<rin::VariableExpr>(a.rawExpr)) {
                    if (warp.has(v->name)) {
                        warp.set(v->name, Value::txt("false"));
                        result.changedWarpNames.push_back(v->name);
                    }
                }
                break;
            }
        }
        return result;
    }

    const StrandPtr& hitTestRoot = ohit.hit ? ohit.hit : fabricRoot;
    return dispatchTap(hitTestRoot, warp, program, x, y, nav, exportDye,
                        persistentInterp, persistentInterpSeeded);
}

} // namespace loom
