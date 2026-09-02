// loom/rin_loom_pipeline.h — LoomtimeRuntime: ties the real rin::Lexer/rin::Parser to the
// Loomtime engine. This is the "LoomtimeRuntime sibling" described in the architecture doc's
// Appendix A: it runs alongside rin::Interpreter (not instead of it) and takes over whenever a
// top-level @view root is found, instead of that root being interpreted as ordinary data.
#pragma once
#include "../rin_lexer.h"
#include "../rin_parser.h"
#include "../rin_interpreter.h" // real-execution bridge -- see runColdPipelineWithRuntime() below
#include "rin_loom.h"
#include "rin_loom_tokens.h"
#include "rin_loom_object.h"
#include <stdexcept>
#include <unordered_set>

namespace loom {

// Converts an already-evaluated rin::Value -- the *actual* post-execution runtime value of a
// `warp` cell after a real rin::Interpreter::run(), not a re-evaluated initializer -- into the
// small Loomtime Value type WarpScope stores. Deliberately not shared with rin_loom_needle.h's
// identically-shaped rinValueToLoom(): that header is a *higher* layer built on top of this one
// (rin_loom_c_api.cpp includes both), and pulling it in here just to reuse five lines would
// invert that dependency for no benefit.
inline Value runtimeValueToLoom(const rin::Value& v) {
    switch (v.type) {
        case rin::Value::Type::NUMBER: return Value::num(v.number);
        case rin::Value::Type::BOOL:   return Value::txt(v.boolean ? "true" : "false");
        case rin::Value::Type::STRING: return Value::txt(v.str);
        default:                       return Value::txt(v.toDisplayString());
    }
}

struct PipelineResult {
    StrandPtr fabric;
    WarpScope warp;
    WarpSubscriptions subs;
    std::shared_ptr<rin::ViewStmt> viewAst; // kept for Shuttle re-diffing on the next hot edit
    // Every top-level statement from the last successful parse -- kept (not just the warp/view
    // roots the pipeline itself cares about) so Needle (rin_loom_needle.h) can find a top-level
    // `fun` declaration by name when dispatching an `onTap` handler and actually run it, loops and
    // all, through rin::Interpreter::callTopLevelFunction instead of the read-only attribute
    // evaluator in rin_loom_eval.h.
    std::vector<rin::StmtPtr> program;
    bool ok = false;
    std::string errorMessage;
    int errorLine = 0;
};

// Cold pipeline: full source -> Fabric, via a *real* rin::Interpreter run.
//
// Editor -> Lexer -> Parser -> Runtime Execution -> Runtime State -> Loom Fabric -> Layout -> Render.
//
// The entire program runs through rin::Interpreter::run() -- `let`, `warp` (now a real global
// declaration, see rin_interpreter.cpp's WarpStmt handling), `if`/`else`, `while`, `for`, `break`/
// `continue`, `fun` + calls + recursion, `@container` bodies, `@import`, expressions, native
// functions -- exactly like running the same source as a normal Rin program, before Loom ever
// looks at it. Loom's only job afterwards is to read back the *result* of that real execution
// (via exportGlobals()) to seed each `warp` cell with its true post-execution value, and to build
// the Fabric from the first top-level `@view...=name` root. This replaces the old
// "evalAttrExpr(initializer) per warp, ignore everything else at top level" shortcut, which never
// actually ran the program -- only its `warp` initializers, in isolation, with no real if/while/
// for/function semantics behind them.
//
// `interp` is the caller's Interpreter -- pass a fresh one for a true cold run, or a session's
// persistent Interpreter (see LoomSession in rin_loom_c_api.cpp) so state (containers, chat
// history, etc.) can carry across a full reload the same way it already does across taps.
// `instructionBudget` bounds total statements executed across `interp`'s lifetime (0 = unlimited);
// Live Preview should always pass a real budget (see rin::Interpreter::setExecutionBudget()) so
// that a program the user is mid-typing, e.g. `while (true) {}`, can't hang the preview thread --
// it fails with a RuntimeError instead, same as any other runtime error.
// New Rin Elements/Container/Loop architecture helpers.
// Element is behavior-neutral markup; Container owns handlers; Loop owns visual canvas attributes.
inline void attachContainerUiBindings(const std::vector<rin::StmtPtr>& stmts,
                                      const std::vector<rin::StmtPtr>& allProgram) {
    std::unordered_map<std::string, std::vector<std::shared_ptr<rin::UiBindingStmt>>> bindings;
    std::function<void(const std::vector<rin::StmtPtr>&)> collect = [&](const auto& xs) {
        for (auto& st : xs) {
            if (auto b = std::dynamic_pointer_cast<rin::UiBindingStmt>(st)) bindings[b->target].push_back(b);
            if (auto c = std::dynamic_pointer_cast<rin::ContainerStmt>(st)) collect(c->body);
            if (auto block = std::dynamic_pointer_cast<rin::BlockStmt>(st)) collect(block->statements);
            if (auto i = std::dynamic_pointer_cast<rin::IfStmt>(st)) { if(i->thenBranch) collect({i->thenBranch}); if(i->elseBranch) collect({i->elseBranch}); }
            if (auto w = std::dynamic_pointer_cast<rin::WhileStmt>(st)) if(w->body) collect({w->body});
            if (auto f = std::dynamic_pointer_cast<rin::ForStmt>(st)) if(f->body) collect({f->body});
        }
    };
    collect(stmts);
    std::function<void(const std::shared_ptr<rin::ViewStmt>&)> apply = [&](const auto& node) {
        if (!node) return;
        auto it = bindings.find(node->name);
        if (it != bindings.end()) {
            for (auto& b : it->second) {
                std::string ev=b->event;
                if (ev=="click" || ev=="tap") node->attrs.push_back({"onTap", b->handler, b->line});
                else node->attrs.push_back({"on"+ev, b->handler, b->line});
            }
        }
        for (auto& c : node->children) apply(c);
    };
    for (auto& st : allProgram) if (auto v=std::dynamic_pointer_cast<rin::ViewStmt>(st)) apply(v);
}

inline void sanitizeElements(std::shared_ptr<rin::ViewStmt>& node) {
    if (!node) return;
    if (node->role == rin::UiRole::ELEMENT) {
        static const std::unordered_set<std::string> visual = {
            "x","y","width","height","min_width","max_width","min_height","max_height",
            "color","background","border","radius","padding","margin","opacity","shadow",
            "font","font_size","text_size","size","align","valign"
        };
        std::vector<rin::ViewAttr> kept;
        for (auto& a: node->attrs) if (!visual.count(a.key)) kept.push_back(a);
        node->attrs.swap(kept);
    }
    for (auto& c: node->children) sanitizeElements(c);
}

inline PipelineResult runColdPipelineWithRuntime(const std::string& source, rin::Interpreter& interp,
                                                  long long instructionBudget = 1000000) {
    PipelineResult result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();

        // Real branch-aware view selection (spec §4/§10): whichever @view real execution actually
        // walks into -- including one nested inside a real `if`/`while`/`for` -- is recorded here
        // as it happens, so "which view is the root" is a fact observed from real control flow,
        // not a second static guess. Only the first one reached is kept (matches the old static
        // scan's behavior for the common case of a single top-level @view).
        std::shared_ptr<rin::ViewStmt> reachedRoot;
        interp.setViewReachedCallback([&reachedRoot](const std::shared_ptr<rin::ViewStmt>& v) {
            if (!reachedRoot && v->role != rin::UiRole::ELEMENT) reachedRoot = v;
        });

        interp.setExecutionBudget(instructionBudget);
        interp.run(program);
        if (interp.hadError()) {
            if (interp.lastDiagnostic()) throw rin::RinError(*interp.lastDiagnostic());
            throw rin::RinError(interp.lastErrorMessage().value_or("runtime error"), interp.lastErrorLine());
        }

        // Seed every top-level `warp name = expr;` from its *actual* post-execution global value
        // (falling back to a fresh evalAttrExpr() only in the defensive case that, somehow, the
        // name never made it into globals -- it always should, since WarpStmt is now handled by
        // execute() exactly like LetStmt).
        auto liveGlobals = interp.exportGlobals();
        for (auto& stmt : program) {
            if (auto w = std::dynamic_pointer_cast<rin::WarpStmt>(stmt)) {
                auto it = liveGlobals.find(w->name);
                Value v = (it != liveGlobals.end()) ? runtimeValueToLoom(it->second)
                                                     : evalAttrExpr(w->initializer, result.warp, nullptr);
                result.warp.set(w->name, v);
            }
        }
        // Rin Loom: register any @theme=... blocks (Pattern Book) before the Fabric is built,
        // so Strand colors resolve against the right active theme from the very first paint.
        registerThemesFromProgram(program, result.warp);
        registerObjectsFromProgram(program, result.warp); // §21: Object Inspector source

        std::shared_ptr<rin::ViewStmt> root = reachedRoot;
        if (!root) {
            // Defensive fallback only -- e.g. a program with a top-level @view that for some
            // reason execute() didn't reach (shouldn't happen: top-level statements always run
            // through execute()). Kept so a bug in the callback path degrades to the old static
            // scan instead of always failing outright.
            for (auto& stmt : program) {
                if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { if (v->role != rin::UiRole::ELEMENT) { root = v; break; } }
            }
        }
        if (!root) throw rin::RinError("no top-level '@view...=name' root found", 1);

        attachContainerUiBindings(program, program);
        sanitizeElements(root);
        result.viewAst = root;
        result.program = program;
        result.fabric = buildFabric(root, result.warp, result.subs, "", 0);
        applyBannerConveniences(result.fabric, result.warp, result.subs);
        applyObjectConveniences(result.fabric); // §21: source= -> title + field rows
        result.ok = true;
    } catch (rin::RinError& e) {
        result.ok = false; result.errorMessage = e.message; result.errorLine = e.line;
    } catch (std::exception& e) {
        result.ok = false; result.errorMessage = e.what(); result.errorLine = 0;
    }
    return result;
}

// Backward-compatible source-only overload: builds its own throwaway Interpreter, so every
// pre-existing call site (rin_loom_render_json, tests, tools...) keeps compiling and behaving the
// same -- just backed by real execution now instead of the old warp-only evaluator, which is a
// strict superset: any program that worked before still produces the same Fabric, and programs
// that also use top-level if/while/for/functions now actually run them instead of being ignored.
inline PipelineResult runColdPipeline(const std::string& source) {
    rin::Interpreter interp;
    return runColdPipelineWithRuntime(source, interp);
}

// Recursively collects every ViewStmt reachable inside `stmts` -- walking into if/while/for/block/
// plus-condition bodies (but not descending into a nested @container's own body, since that's a
// separate scope with its own view) -- as a set of raw pointers. Used to check "did the ViewStmt
// real execution just reached actually belong to *this* container" when a container-scoped cold
// pipeline runs the whole program (needed so top-level funs stay callable) but must still only
// ever pick a view that's really inside the named container, not some unrelated sibling's.
inline void collectViewStmts(const std::vector<rin::StmtPtr>& stmts, std::unordered_set<const void*>& out) {
    for (auto& stmt : stmts) {
        if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { out.insert(v.get()); continue; }
        if (auto b = std::dynamic_pointer_cast<rin::BlockStmt>(stmt)) { collectViewStmts(b->statements, out); continue; }
        if (auto i = std::dynamic_pointer_cast<rin::IfStmt>(stmt)) {
            if (i->thenBranch) collectViewStmts({i->thenBranch}, out);
            if (i->elseBranch) collectViewStmts({i->elseBranch}, out);
            continue;
        }
        if (auto w = std::dynamic_pointer_cast<rin::WhileStmt>(stmt)) { if (w->body) collectViewStmts({w->body}, out); continue; }
        if (auto f = std::dynamic_pointer_cast<rin::ForStmt>(stmt)) { if (f->body) collectViewStmts({f->body}, out); continue; }
        if (auto p = std::dynamic_pointer_cast<rin::PlusConditionStmt>(stmt)) {
            if (p->trueBranch) collectViewStmts(p->trueBranch->statements, out);
            if (p->falseBranch) collectViewStmts(p->falseBranch->statements, out);
            continue;
        }
        // Deliberately NOT recursing into ContainerStmt/ContainerGroupStmt/VolumeStmt bodies --
        // those are a different container's own scope (see runColdPipelineForContainerWithRuntime's
        // use of this function, which calls it once per target container's own direct body).
    }
}

// Finds the body (statement list) of a named @container / Containers.Group / Volume anywhere in
// a parsed program, searching nested groups/volumes recursively (a container can sit inside a
// Containers.Group, which can itself sit inside another Containers.Group, etc.). Returns nullptr
// if no container/group/volume with that name exists.
inline const std::vector<rin::StmtPtr>* findContainerBody(const std::vector<rin::StmtPtr>& stmts,
                                                            const std::string& containerName) {
    for (auto& stmt : stmts) {
        if (auto c = std::dynamic_pointer_cast<rin::ContainerStmt>(stmt)) {
            if (c->name == containerName) return &c->body;
        } else if (auto g = std::dynamic_pointer_cast<rin::ContainerGroupStmt>(stmt)) {
            if (g->name == containerName) return &g->body;
            if (auto found = findContainerBody(g->body, containerName)) return found;
        } else if (auto v = std::dynamic_pointer_cast<rin::VolumeStmt>(stmt)) {
            if (v->name == containerName) return &v->body;
            if (auto found = findContainerBody(v->body, containerName)) return found;
        }
    }
    return nullptr;
}

// Container-scoped cold pipeline: full source -> Fabric built from the @view root that lives
// *inside* the named @container (not the top-level program). This is what makes Rin Loom actually
// tied to `container` rather than being a wholly separate top-level-only system: every container
// that carries its own @view/warp/@theme becomes an independently addressable screen/component,
// scoped to that container's own warp state and theme, exactly like runColdPipeline is scoped to
// the whole program. Warp/theme seeding only looks at the container's direct body (same flat,
// non-recursive semantics as runColdPipeline's top-level scan) so a container's UI state stays
// its own and doesn't accidentally pick up an unrelated sibling container's warp/theme.
inline PipelineResult runColdPipelineForContainerWithRuntime(const std::string& source, const std::string& containerName,
                                                              rin::Interpreter& interp,
                                                              long long instructionBudget = 1000000) {
    PipelineResult result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();

        const std::vector<rin::StmtPtr>* body = findContainerBody(program, containerName);
        if (!body) throw rin::RinError("no container/group/volume named '" + containerName + "' found", 1);

        // Real execution: run the *whole* program (not just the container's body) so top-level
        // `fun` declarations the container's own view/handlers may call are hoisted and callable,
        // and so the container's ContainerStmt is itself executed for real (its body runs inside
        // its own Environment via execute()'s ContainerStmt case -- see rin_interpreter.cpp) --
        // exactly the same real run every other container in the program also gets, not a special
        // "just this container" mini-interpretation.
        // Only accept a "view reached" that's really inside *this* container's own body (real
        // execution also runs the rest of the program, including any sibling container/top-level
        // view -- see the comment above interp.run() below) -- see collectViewStmts().
        std::unordered_set<const void*> ownViews;
        collectViewStmts(*body, ownViews);
        std::shared_ptr<rin::ViewStmt> reachedRoot;
        interp.setViewReachedCallback([&reachedRoot, &ownViews](const std::shared_ptr<rin::ViewStmt>& v) {
            if (!reachedRoot && ownViews.count(v.get()) && v->role != rin::UiRole::ELEMENT) reachedRoot = v;
        });

        interp.setExecutionBudget(instructionBudget);
        interp.run(program);
        if (interp.hadError()) {
            if (interp.lastDiagnostic()) throw rin::RinError(*interp.lastDiagnostic());
            throw rin::RinError(interp.lastErrorMessage().value_or("runtime error"), interp.lastErrorLine());
        }

        // `warp` cells declared directly inside this container's body live in the container's own
        // Environment (see ContainerStmt in execute()), not globals -- exportContainerGlobals()
        // reads that scope specifically. A container declared inside a Containers.Group/Volume is
        // still found by name here the same way findContainerBody() finds it above.
        auto liveContainerVars = interp.exportContainerGlobals(containerName);
        for (auto& stmt : *body) {
            if (auto w = std::dynamic_pointer_cast<rin::WarpStmt>(stmt)) {
                auto it = liveContainerVars.find(w->name);
                Value v = (it != liveContainerVars.end()) ? runtimeValueToLoom(it->second)
                                                            : evalAttrExpr(w->initializer, result.warp, nullptr);
                result.warp.set(w->name, v);
            }
        }
        registerThemesFromProgram(*body, result.warp);
        registerObjectsFromProgram(*body, result.warp); // §21: Object Inspector source
        std::shared_ptr<rin::ViewStmt> root = reachedRoot;
        if (!root) {
            for (auto& stmt : *body) { // defensive fallback -- see runColdPipelineWithRuntime's equivalent
                if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { if (v->role != rin::UiRole::ELEMENT) { root = v; break; } }
            }
        }
        if (!root) throw rin::RinError("no '@view...=name' root found inside container '" + containerName + "'", 1);

        attachContainerUiBindings(*body, program);
        sanitizeElements(root);
        result.viewAst = root;
        result.program = program; // البرنامج الكامل يبقى محفوظاً (Needle قد يحتاج دوال أعلى المستوى)
        result.fabric = buildFabric(root, result.warp, result.subs, "", 0);
        applyBannerConveniences(result.fabric, result.warp, result.subs);
        applyObjectConveniences(result.fabric); // §21: source= -> title + field rows
        result.ok = true;
    } catch (rin::RinError& e) {
        result.ok = false; result.errorMessage = e.message; result.errorLine = e.line;
    } catch (std::exception& e) {
        result.ok = false; result.errorMessage = e.what(); result.errorLine = 0;
    }
    return result;
}

// Backward-compatible source-only overload -- see runColdPipeline()'s equivalent comment above.
inline PipelineResult runColdPipelineForContainer(const std::string& source, const std::string& containerName) {
    rin::Interpreter interp;
    return runColdPipelineForContainerWithRuntime(source, containerName, interp);
}

// Hot pipeline (Shuttle-scoped): re-parses the edited source and diffs it against the previous
// Fabric in place. On a parse/semantic error, the previous Fabric is left completely untouched
// (Snag containment — see architecture doc §16) and the caller keeps showing the last-good frame.
inline std::vector<Patch> runHotPipeline(PipelineResult& state, const std::string& editedSource,
                                          std::string& errorMessage, int& errorLine) {
    try {
        rin::Lexer lexer(editedSource);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();

        for (auto& stmt : program) {
            if (auto w = std::dynamic_pointer_cast<rin::WarpStmt>(stmt)) {
                if (!state.warp.has(w->name)) { // only seed newly-added warps; don't clobber runtime state
                    Value v = evalAttrExpr(w->initializer, state.warp, nullptr);
                    state.warp.set(w->name, v);
                }
            }
        }
        registerThemesFromProgram(program, state.warp);
        registerObjectsFromProgram(program, state.warp); // §21: keep registry fresh on hot edits;
        // note this does NOT re-synthesize an already-built Object Strand's rows -- same documented
        // limitation as applyBannerConveniences above (only the next cold build/Run picks it up).
        std::shared_ptr<rin::ViewStmt> root;
        for (auto& stmt : program) {
            if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { if (v->role != rin::UiRole::ELEMENT) { root = v; break; } }
        }
        if (!root) throw rin::RinError("no top-level '@view/@loop...=name' root found", 1);
        attachContainerUiBindings(program, program);
        sanitizeElements(root);

        Shuttle shuttle;
        shuttle.diff(state.fabric, root, state.warp, state.subs, "", 0);
        state.viewAst = root;
        state.program = program;
        return shuttle.patches;
    } catch (rin::RinError& e) {
        errorMessage = e.message; errorLine = e.line;
        return {};
    } catch (std::exception& e) {
        errorMessage = e.what(); errorLine = 0;
        return {};
    }
}

} // namespace loom
