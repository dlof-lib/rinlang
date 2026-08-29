// loom/rin_loom_pipeline.h — LoomtimeRuntime: ties the real rin::Lexer/rin::Parser to the
// Loomtime engine. This is the "LoomtimeRuntime sibling" described in the architecture doc's
// Appendix A: it runs alongside rin::Interpreter (not instead of it) and takes over whenever a
// top-level @view root is found, instead of that root being interpreted as ordinary data.
#pragma once
#include "../rin_lexer.h"
#include "../rin_parser.h"
#include "rin_loom.h"
#include "rin_loom_tokens.h"
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
        std::shared_ptr<rin::ViewStmt> root;
        for (auto& stmt : program) {
            if (auto v = std::dynamic_pointer_cast<rin::ViewStmt>(stmt)) { root = v; break; }
        }
        if (!root) throw rin::RinError("no top-level '@view...=name' root found", 1);

        result.viewAst = root;
        result.program = program;
        result.fabric = buildFabric(root, result.warp, result.subs, "", 0);
        applyBannerConveniences(result.fabric, result.warp, result.subs);
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
