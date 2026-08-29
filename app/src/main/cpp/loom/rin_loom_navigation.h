// loom/rin_loom_navigation.h — Navigation Manager (spec §19): a small, crash-safe history stack
// behind navigate()/back()/replace()/reload(). This owns *history bookkeeping* only -- it does
// not read a .rin file or swap the live Fabric itself, because Loomtime's rendering pipeline
// (rin_loom_pipeline.h) is a pure `source -> Fabric` function with no notion of "the current
// page"; on Android that swap already happens one layer up (LoomPreviewActivity/RinEngine.kt).
// NavigationManager gives that caller a single, tested place to ask "what does navigate("x.rin")
// do to the history stack, and what route should I load now" instead of every call site
// reimplementing its own index bookkeeping (which is exactly how "index out of range" bugs like
// the ones spec §28 calls out happen in practice).
#pragma once
#include <string>
#include <vector>

namespace loom {

struct NavigationManager {
    std::vector<std::string> history;
    int index = -1; // -1 == no route navigated to yet

    // navigate("x.rin"): pushes a new route. If the user had gone back() and then navigates
    // somewhere new, everything "forward" of the current position is discarded first -- the
    // ordinary browser-history rule, and the reason index is always kept in [-1, history.size()-1]
    // afterward (never past the end, never a stale forward branch left dangling).
    void navigate(const std::string& route) {
        if (index + 1 < static_cast<int>(history.size())) {
            history.resize(index + 1);
        }
        history.push_back(route);
        index = static_cast<int>(history.size()) - 1;
    }

    // back(): moves one step back if possible; a no-op (never an out-of-range access) at the
    // start of history. Returns true if it actually moved.
    bool back() {
        if (index <= 0) return false;
        index--;
        return true;
    }

    // replace("x.rin"): swaps the *current* history entry in place -- no new back-stack entry,
    // matching a real router's replace() (e.g. a login redirect you don't want back() to return
    // to). A no-op at index == -1 (nothing to replace yet) is treated as navigate() instead, so
    // replace() before any navigate() still does something sensible rather than silently failing.
    void replace(const std::string& route) {
        if (index < 0) { navigate(route); return; }
        history[index] = route;
    }

    // reload(): re-reports the current route with no history-stack change at all. Returns "" (not
    // a crash) if nothing has been navigated to yet -- the caller treats an empty route as "there
    // is nothing to reload".
    std::string reload() const { return current(); }

    // current(): the route the app should have loaded right now, given the stack above. "" when
    // history is empty -- always bounds-checked, never history[index] without the guard.
    std::string current() const {
        if (index < 0 || index >= static_cast<int>(history.size())) return "";
        return history[static_cast<size_t>(index)];
    }

    bool canGoBack() const { return index > 0; }
};

} // namespace loom
