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
//   2. Otherwise, a handful of built-in Warp operations (increment/decrement/toggle/set) are
//      supported directly, so a demo like `onTap=increment(count);` works with no `fun` at all --
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
#include "../rin_interpreter.h"
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>

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
};

// Dispatches a tap at (x, y) against `fabricRoot`. `program` is the last successfully parsed
// top-level statement list (PipelineResult::program) -- used to look up a matching `fun`.
inline TapResult dispatchTap(const StrandPtr& fabricRoot, WarpScope& warp,
                              const std::vector<rin::StmtPtr>& program, double x, double y) {
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
    if (!onTapExpr) return result; // tapped something, but nothing interactive there

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

        rin::Interpreter interp;
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

    // No matching `fun` -- fall back to a few built-in Warp operations so a simple demo works
    // without writing one, matching the Mirror Loom preview's own onTap shortcut.
    auto argCellName = [&](size_t i) -> std::string {
        if (i >= argExprs.size()) return "";
        auto v = std::dynamic_pointer_cast<rin::VariableExpr>(argExprs[i]);
        return v ? v->name : "";
    };
    auto argNumber = [&](size_t i, double def) -> double {
        if (i >= argExprs.size()) return def;
        return evalAttrExpr(argExprs[i], warp, nullptr).asNumber(def);
    };

    std::string cell = argCellName(0);
    if (!cell.empty() && warp.has(cell)) {
        Value cur = warp.get(cell);
        Value updated = cur;
        bool recognized = true;
        if (callee == "increment")      updated = Value::num(cur.asNumber() + argNumber(1, 1));
        else if (callee == "decrement") updated = Value::num(cur.asNumber() - argNumber(1, 1));
        else if (callee == "toggle")    updated = Value::txt(cur.asString() == "true" ? "false" : "true");
        else if (callee == "set" && argExprs.size() >= 2) updated = evalAttrExpr(argExprs[1], warp, nullptr);
        else recognized = false;

        if (recognized) {
            warp.set(cell, updated);
            result.changedWarpNames.push_back(cell);
            return result;
        }
    }

    result.error = "no top-level function named '" + callee +
                   "' (and it doesn't match a built-in like increment/decrement/toggle/set)";
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
                                         OverlayLayer& overlayLayer, double x, double y) {
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
    return dispatchTap(hitTestRoot, warp, program, x, y);
}

} // namespace loom
