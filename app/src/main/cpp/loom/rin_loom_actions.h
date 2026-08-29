// loom/rin_loom_actions.h — Loom Action Engine: a single registry of named, Warp-cell-driven
// actions (increment/decrement/toggle/set/show/hide/enable/disable/select/unselect/focus/blur/
// play/pause/stop/seek/scrollTo/submit/reset/seq), used by Needle (rin_loom_needle.h) as the
// built-in fallback when an onTap handler doesn't name a real top-level `fun`.
//
// This exists so the built-in verbs are ONE table instead of an `if (callee == "...")` chain that
// grows forever (see the spec's own §36: "لا أريد مجرد if component == Button ... أريد
// architecture قابلة للتوسع"). Everything here shares the same shape: read the first argument as
// a bound Warp-cell name (the same convention `open=`/`visible=`/`value=` attributes already use),
// compute a new Value for it, write it back. That is deliberately the whole model -- these are
// state transitions on a reactive cell, not component-specific methods, which is what lets a
// single verb like show()/hide() work identically for a Dialog, a Drawer, or a Banner without any
// per-component code (see the note on show()/hide() in the registry below).
//
// Actions that need session-level state beyond a single Warp cell -- navigate/back/replace/reload
// (a page history) and download (network progress) -- are NOT here; they live in
// rin_loom_navigation.h and are wired into Needle separately, since they need a NavigationManager
// reference that a pure (argExprs, WarpScope&) action signature can't carry. remove()/append()/
// prepend() (structural tree edits) and focus()/blur() as *real* input-focus (vs. a modelled Warp
// flag) are also out of scope here -- seeded but not implemented, and reported as unrecognized
// rather than faked; see the architecture doc for the current honest status of each verb.
#pragma once
#include "rin_loom_eval.h"
#include "../rin_ast.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace loom {

// Result of invoking one action: which Warp cell(s) changed (usually zero or one), and whether
// the action name was even recognized at all -- callers (Needle) use `recognized` to decide
// whether to keep trying other resolution paths (e.g. a top-level `fun`) or report a clear error.
struct ActionOutcome {
    bool recognized = false;
    std::vector<std::string> changedWarpNames;
    std::string error;
};

using ActionFn = std::function<ActionOutcome(const std::vector<rin::ExprPtr>& argExprs, WarpScope& warp)>;

// Forward declaration -- seq() (composite/"block" actions, spec §6) needs to call back into the
// registry for each of its own arguments, so it's a normal registered action, not special-cased
// dispatch code.
class ActionRegistry;
ActionOutcome invokeAction(ActionRegistry& reg, const std::string& name,
                            const std::vector<rin::ExprPtr>& argExprs, WarpScope& warp);

class ActionRegistry {
public:
    // register(name, fn): the extensibility point spec §36 asks for -- a new Loom Action never
    // requires touching Needle's dispatch code, only a call here (or, for a Bolt/plugin-provided
    // action, a call from wherever plugins register themselves).
    void registerAction(const std::string& name, ActionFn fn) { actions_[name] = std::move(fn); }
    bool has(const std::string& name) const { return actions_.count(name) != 0; }

    ActionOutcome invoke(const std::string& name, const std::vector<rin::ExprPtr>& argExprs, WarpScope& warp) {
        auto it = actions_.find(name);
        if (it == actions_.end()) return ActionOutcome{}; // recognized=false, not an error by itself
        return it->second(argExprs, warp);
    }

    // The process-wide registry every Needle call uses by default. A caller that needs an
    // isolated registry (tests, or a future per-session Bolt plugin sandbox) can still construct
    // its own ActionRegistry and call registerBuiltins() on it directly.
    static ActionRegistry& shared() {
        static ActionRegistry r;
        static bool seeded = (r.registerBuiltins(), true);
        (void)seeded;
        return r;
    }

    void registerBuiltins();

private:
    std::unordered_map<std::string, ActionFn> actions_;
};

// ---- shared argument helpers -----------------------------------------------------------------

// arg 0 of nearly every built-in action here is a bare Warp-cell reference, e.g. `toggle(drawer)`
// -- this resolves that name, or "" if arg 0 isn't a plain identifier / isn't a live Warp cell
// (both treated the same: "no target", never a crash).
inline std::string actionCellArg(const std::vector<rin::ExprPtr>& argExprs, size_t i, const WarpScope& warp) {
    if (i >= argExprs.size()) return "";
    auto v = std::dynamic_pointer_cast<rin::VariableExpr>(argExprs[i]);
    if (!v || !warp.has(v->name)) return "";
    return v->name;
}
inline double actionNumberArg(const std::vector<rin::ExprPtr>& argExprs, size_t i, WarpScope& warp, double def) {
    if (i >= argExprs.size()) return def;
    return evalAttrExpr(argExprs[i], warp, nullptr).asNumber(def);
}

// A boolean-flag action: sets `cell` to a fixed "true"/"false" regardless of its current value.
// This is the shared implementation behind show/hide, enable/disable, select/unselect, focus/blur,
// and play/pause/stop's on/off half -- one function, six verb pairs, instead of six near-identical
// if-branches.
inline ActionFn boolFlagAction(bool toValue) {
    return [toValue](const std::vector<rin::ExprPtr>& argExprs, WarpScope& warp) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(argExprs, 0, warp);
        if (cell.empty()) { out.error = "expected a Warp-cell argument, e.g. show(myDialog)"; return out; }
        out.recognized = true;
        warp.set(cell, Value::txt(toValue ? "true" : "false"));
        out.changedWarpNames.push_back(cell);
        return out;
    };
}

inline void ActionRegistry::registerBuiltins() {
    registerAction("increment", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty()) { out.error = "increment() needs a Warp-cell argument"; return out; }
        out.recognized = true;
        w.set(cell, Value::num(w.get(cell).asNumber() + actionNumberArg(a, 1, w, 1)));
        out.changedWarpNames.push_back(cell);
        return out;
    });
    registerAction("decrement", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty()) { out.error = "decrement() needs a Warp-cell argument"; return out; }
        out.recognized = true;
        w.set(cell, Value::num(w.get(cell).asNumber() - actionNumberArg(a, 1, w, 1)));
        out.changedWarpNames.push_back(cell);
        return out;
    });
    registerAction("toggle", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty()) { out.error = "toggle() needs a Warp-cell argument"; return out; }
        out.recognized = true;
        w.set(cell, Value::txt(w.get(cell).asString() == "true" ? "false" : "true"));
        out.changedWarpNames.push_back(cell);
        return out;
    });
    registerAction("set", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty() || a.size() < 2) { out.error = "set() needs set(cell, value)"; return out; }
        out.recognized = true;
        w.set(cell, evalAttrExpr(a[1], w, nullptr));
        out.changedWarpNames.push_back(cell);
        return out;
    });
    // seek(cell, value) / scrollTo(cell, value): same shape as set(), named for their Video/Audio
    // position and Scroll offset use sites (spec §12/§40) so `.rin` source reads naturally.
    registerAction("seek", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty() || a.size() < 2) { out.error = "seek() needs seek(cell, value)"; return out; }
        out.recognized = true;
        w.set(cell, Value::num(actionNumberArg(a, 1, w, 0)));
        out.changedWarpNames.push_back(cell);
        return out;
    });
    registerAction("scrollTo", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty() || a.size() < 2) { out.error = "scrollTo() needs scrollTo(cell, value)"; return out; }
        out.recognized = true;
        w.set(cell, Value::num(actionNumberArg(a, 1, w, 0)));
        out.changedWarpNames.push_back(cell);
        return out;
    });
    // reset(cell[, default]): explicit default if given, else "" / 0 matching the cell's current
    // Value kind -- never invented from a component's declared type, since Needle only sees Warp
    // cells, not component schemas.
    registerAction("reset", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty()) { out.error = "reset() needs a Warp-cell argument"; return out; }
        out.recognized = true;
        if (a.size() >= 2) {
            w.set(cell, evalAttrExpr(a[1], w, nullptr));
        } else {
            Value cur = w.get(cell);
            w.set(cell, cur.kind == Value::Kind::NUMBER ? Value::num(0) : Value::txt(""));
        }
        out.changedWarpNames.push_back(cell);
        return out;
    });
    // submit(cell): marks `<cell>_submitted` true rather than inventing a network/IO side effect
    // Needle has no business performing -- a real submit handler is a top-level `fun` (already
    // tried before any built-in action runs), this is only the no-`fun`-written-yet fallback.
    registerAction("submit", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty()) { out.error = "submit() needs a Warp-cell argument"; return out; }
        out.recognized = true;
        std::string flag = cell + "_submitted";
        w.set(flag, Value::txt("true"));
        out.changedWarpNames.push_back(flag);
        return out;
    });

    // show/hide, enable/disable, select/unselect, focus/blur: six verb pairs, one shared
    // implementation (boolFlagAction above). Each just sets the named cell to a fixed literal --
    // the *meaning* ("visible", "enabled", "selected", "focused") comes entirely from which
    // attribute (`visible=`, `enabled=`, `selected=`, `focused=`) the .rin source binds that same
    // cell to, exactly like show()/hide() already worked with open=/visible=.
    registerAction("show", boolFlagAction(true));
    registerAction("hide", boolFlagAction(false));
    registerAction("enable", boolFlagAction(true));
    registerAction("disable", boolFlagAction(false));
    registerAction("select", boolFlagAction(true));
    registerAction("unselect", boolFlagAction(false));
    registerAction("focus", boolFlagAction(true));
    registerAction("blur", boolFlagAction(false));
    // play/pause/stop: model a Video/Audio's `playing=` boolean cell (a real position/duration
    // model belongs to the native media player, not Needle -- see docs/loomtime for the honest
    // scope note). stop() additionally resets `<cell>_position` to 0 when that cell exists, so a
    // `seek()`-driven position binding returns to the start the way a real stop() would.
    registerAction("play", boolFlagAction(true));
    registerAction("pause", boolFlagAction(false));
    registerAction("stop", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        std::string cell = actionCellArg(a, 0, w);
        if (cell.empty()) { out.error = "stop() needs a Warp-cell argument"; return out; }
        out.recognized = true;
        w.set(cell, Value::txt("false"));
        out.changedWarpNames.push_back(cell);
        std::string posCell = cell + "_position";
        if (w.has(posCell)) { w.set(posCell, Value::num(0)); out.changedWarpNames.push_back(posCell); }
        return out;
    });

    // seq(a(), b(), c()): the onTap "block" workaround for spec §6 -- .rin's attribute grammar
    // only accepts a single expression per key (`onTap=<expr>;`), and a real `{ stmt; stmt; }`
    // block there is a parser-grammar change out of scope for this pass (see the architecture
    // doc's "not yet done" list). A CallExpr's argument list is already comma-separated Rin
    // expressions, so seq(...) reuses that -- zero grammar changes -- to express
    // `onTap: seq(set(progress,50), show(banner), navigate("next.rin"));` as ordinary, already-
    // parseable Rin. Each argument must itself be a call to a registered action name; anything
    // else in the list is a hard error (not silently skipped), so a typo doesn't fail invisibly.
    registerAction("seq", [](const std::vector<rin::ExprPtr>& a, WarpScope& w) -> ActionOutcome {
        ActionOutcome out;
        out.recognized = true;
        for (auto& step : a) {
            auto call = std::dynamic_pointer_cast<rin::CallExpr>(step);
            if (!call) { out.error = "seq() arguments must be action calls, e.g. seq(show(x), hide(y))"; return out; }
            ActionOutcome stepOut = ActionRegistry::shared().invoke(call->callee, call->args, w);
            if (!stepOut.recognized) { out.error = "seq(): unrecognized action '" + call->callee + "'"; return out; }
            if (!stepOut.error.empty()) { out.error = stepOut.error; return out; }
            for (auto& n : stepOut.changedWarpNames) out.changedWarpNames.push_back(n);
        }
        return out;
    });
}

} // namespace loom
