// loom/rin_loom_pipeline.h — LoomtimeRuntime: ties the real rin::Lexer/rin::Parser to the
// Loomtime engine. This is the "LoomtimeRuntime sibling" described in the architecture doc's
// Appendix A: it runs alongside rin::Interpreter (not instead of it) and takes over whenever a
// top-level @view root is found, instead of that root being interpreted as ordinary data.
#pragma once
#include "../rin_lexer.h"
#include "../rin_parser.h"
#include "rin_loom.h"
#include "rin_loom_tokens.h"
#include "rin_loom_object.h"
#include <stdexcept>

namespace loom {

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

// Cold pipeline: full source -> Fabric. Seeds every top-level `warp name = expr;` into the
// WarpScope (in source order, so a later warp's initializer may reference an earlier one),
// then builds the Fabric from the first top-level `@view...=name` root found.
inline PipelineResult runColdPipeline(const std::string& source) {
    PipelineResult result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();

        for (auto& stmt : program) {
            if (auto w = std::dynamic_pointer_cast<rin::WarpStmt>(stmt)) {
                Value v = evalAttrExpr(w->initializer, result.warp, nullptr);
                result.warp.set(w->name, v);
            }
        }
        // Rin Loom: register any @theme=... blocks (Pattern Book) before the Fabric is built,
        // so Strand colors resolve against the right active theme from the very first paint.
        registerThemesFromProgram(program, result.warp);
        registerObjectsFromProgram(program, result.warp); // §21: Object Inspector source
        std::shared_ptr<rin::ViewStmt> root;
        for (auto& stmt : program) {
            if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { root = v; break; }
        }
        if (!root) throw rin::RinError("no top-level '@view...=name' root found", 1);

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
inline PipelineResult runColdPipelineForContainer(const std::string& source, const std::string& containerName) {
    PipelineResult result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();

        const std::vector<rin::StmtPtr>* body = findContainerBody(program, containerName);
        if (!body) throw rin::RinError("no container/group/volume named '" + containerName + "' found", 1);

        for (auto& stmt : *body) {
            if (auto w = std::dynamic_pointer_cast<rin::WarpStmt>(stmt)) {
                Value v = evalAttrExpr(w->initializer, result.warp, nullptr);
                result.warp.set(w->name, v);
            }
        }
        registerThemesFromProgram(*body, result.warp);
        registerObjectsFromProgram(*body, result.warp); // §21: Object Inspector source
        std::shared_ptr<rin::ViewStmt> root;
        for (auto& stmt : *body) {
            if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { root = v; break; }
        }
        if (!root) throw rin::RinError("no '@view...=name' root found inside container '" + containerName + "'", 1);

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
            if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { root = v; break; }
        }
        if (!root) throw rin::RinError("no top-level '@view...=name' root found", 1);

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
