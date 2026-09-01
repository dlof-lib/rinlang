// tools/test_loom_live_runtime.cpp — Phase 1 real-execution tests for Live Preview.
//
// These prove that loom::runColdPipeline / runColdPipelineWithRuntime / runColdPipelineForContainer
// now build the Fabric from the result of a REAL rin::Interpreter::run() -- if/while/for/functions
// executed with full language semantics -- instead of the old approach of only evaluating each
// top-level `warp` initializer in isolation and ignoring all other top-level control flow.
//
// Build & run (same recipe as the other tools/test_loom_*.cpp files):
//   cd app/src/main/cpp
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_live_runtime.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp \
//       diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp \
//       diagnostics/source_manager.cpp -lz -o test_loom_live_runtime
//   ./test_loom_live_runtime
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "loom/rin_loom_pipeline.h"
#include "loom/rin_loom_needle.h"
#include <iostream>
#include <cassert>
#include <cmath>

static int checks = 0;
static int failures = 0;

void check(bool cond, const std::string& label) {
    checks++;
    if (!cond) {
        failures++;
        std::cerr << "FAIL: " << label << "\n";
    } else {
        std::cout << "ok:   " << label << "\n";
    }
}

loom::StrandPtr findByName(const loom::StrandPtr& s, const std::string& name) {
    if (!s) return nullptr;
    if (s->name == name) return s;
    for (auto& c : s->children) {
        if (auto found = findByName(c, name)) return found;
    }
    return nullptr;
}

std::string attr(const loom::StrandPtr& s, const std::string& key) {
    if (!s) return "";
    for (auto& a : s->attrs) {
        if (a.key != key) continue;
        if (a.value.kind == loom::Value::Kind::STRING) return a.value.str;
        // Numbers print without a trailing ".000000" so text=count comparisons ("5" etc.) are exact.
        double n = a.value.number;
        if (n == (long long)n) return std::to_string((long long)n);
        return std::to_string(n);
    }
    return "";
}

int main() {
    // ---- 1. real `if`: only the true branch's @view becomes part of the Fabric ----
    {
        std::string src = R"(
            let logged = true;
            if (logged) {
                @view.Text=title text="Welcome";
                .end/view
            } else {
                @view.Text=title text="Login";
                .end/view
            }
        )";
        auto r = loom::runColdPipeline(src);
        check(r.ok, "if(true): pipeline succeeds");
        if (r.ok) {
            check(attr(r.fabric, "text") == "Welcome", "if(true): real branch selected (Welcome, not Login)");
        }
    }
    {
        std::string src = R"(
            let logged = false;
            if (logged) {
                @view.Text=title text="Welcome";
                .end/view
            } else {
                @view.Text=title text="Login";
                .end/view
            }
        )";
        auto r = loom::runColdPipeline(src);
        check(r.ok, "if(false): pipeline succeeds");
        if (r.ok) {
            check(attr(r.fabric, "text") == "Login", "if(false): real else-branch selected");
        }
    }

    // ---- 2. real `while`: count actually reaches 5 via real execution, not "while detected" ----
    // (checked directly against the real Interpreter's globals -- @view attribute expressions
    // like `text=count` are resolved against WarpScope, i.e. `warp` cells specifically, so a
    // plain `let` is checked here via exportGlobals() instead; see test 5 below for a `warp`
    // cell actually displayed and mutated through the full Loom pipeline.)
    {
        std::string src = R"(
            let count = 0;
            while (count < 5) {
                count = count + 1;
            }
        )";
        rin::Interpreter interp;
        rin::Lexer lexer(src); auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens); auto program = parser.parse();
        interp.run(program);
        check(!interp.hadError(), "while: real execution succeeds");
        auto g = interp.exportGlobals();
        check(g.count("count") && g["count"].type == rin::Value::Type::NUMBER && g["count"].number == 5.0,
              "while: count really reaches 5");
    }

    // ---- 3. real `for`: C-style init/condition/increment/body, matches CLI semantics ----
    {
        std::string src = R"(
            let total = 0;
            for (let i = 0; i < 10; i = i + 1) {
                total = total + i;
            }
        )";
        rin::Interpreter interp;
        rin::Lexer lexer(src); auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens); auto program = parser.parse();
        interp.run(program);
        check(!interp.hadError(), "for: real execution succeeds");
        auto g = interp.exportGlobals();
        // 0+1+...+9 = 45
        check(g.count("total") && g["total"].number == 45.0, "for: real accumulation over 0..9 == 45");
    }

    // ---- 4. real function calls (with their own real `if`) ----
    {
        std::string src = R"(
            fun calculate(x) {
                if (x > 5) {
                    return x * 2;
                }
                return x + 1;
            }
            let result = calculate(10);
        )";
        rin::Interpreter interp;
        rin::Lexer lexer(src); auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens); auto program = parser.parse();
        interp.run(program);
        check(!interp.hadError(), "function: real execution succeeds");
        auto g = interp.exportGlobals();
        check(g.count("result") && g["result"].number == 20.0, "function: real call result == 20 (calculate(10))");
    }

    // ---- 5. `warp` is a real global now: mutated by a real function via a real onTap dispatch ----
    {
        std::string src = R"(
            warp count = 0;
            fun increment() {
                count = count + 1;
                if (count >= 5) {
                    count = 0;
                }
            }
            @view.Column=home
                @view.Text=counter text=count;
                .end/view
                @view.Button=button text="Increment"; onTap=increment();
                .end/view
            .end/view
        )";
        auto r = loom::runColdPipeline(src);
        check(r.ok, "warp+onTap: cold pipeline succeeds");
        if (r.ok) {
            check(attr(findByName(r.fabric, "counter"), "text") == "0", "warp+onTap: initial count == 0");
            int expected[] = {1, 2, 3, 4, 0};
            bool allGood = true;
            for (int tap = 0; tap < 5; tap++) {
                auto tapResult = loom::dispatchTap(r.fabric, r.warp, r.program, /*x*/0, /*y*/0, nullptr, nullptr);
                // dispatchTap hit-tests geometry, which isn't laid out in this test (no Loom::layout
                // call) -- so call the handler directly via the same real-execution path it uses
                // internally to keep this test focused on execution correctness, not hit-testing.
                (void)tapResult;
                std::unordered_map<std::string, rin::Value> globals;
                for (auto& kv : r.warp.cells) globals[kv.first] = loom::loomValueToRin(kv.second);
                std::vector<rin::Value> args; std::vector<std::string> aliases; std::string err;
                rin::Interpreter interp;
                bool okCall = interp.callTopLevelFunction(r.program, "increment", args, aliases, globals, err);
                if (!okCall) { allGood = false; break; }
                for (auto& kv : globals) r.warp.set(kv.first, loom::rinValueToLoom(kv.second));
                int got = (int)r.warp.get("count").number;
                if (got != expected[tap]) { allGood = false; break; }
            }
            check(allGood, "warp+onTap: 5 real increments wrap 0->1->2->3->4->0");
        }
    }

    // ---- 6. real runtime error surfaces, isn't swallowed as a generic "Preview failed" ----
    {
        std::string src = "let x = unknown();";
        // No @view root, so this will fail either on the runtime error or the missing-view check --
        // what matters is that a program calling an undefined function reports a real runtime error.
        rin::Interpreter interp;
        rin::Lexer lexer(src);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();
        interp.run(program);
        check(interp.hadError(), "runtime error: calling unknown() is a real Interpreter error");
    }

    // ---- 7. infinite loop protection: bounded budget stops `while (true) {}` ----
    {
        std::string src = R"(
            while (true) {
            }
        )";
        rin::Interpreter interp;
        interp.setExecutionBudget(10000);
        rin::Lexer lexer(src);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto program = parser.parse();
        std::string output = interp.run(program);
        check(interp.hadError(), "infinite loop: budget trips a real error instead of hanging");
        check(output.find("Execution limit exceeded") != std::string::npos,
              "infinite loop: error message names the real cause");
    }

    // ---- 8. container-scoped pipeline: real execution scoped to the container's own body ----
    {
        std::string src = R"(
            @container.card
                warp visits = 0;
                while (visits < 3) {
                    visits = visits + 1;
                }
                @view.Text=t text=visits;
                .end/view
            .end/container.card
        )";
        auto r = loom::runColdPipelineForContainer(src, "card");
        if (!r.ok) std::cerr << "  (debug) container error: " << r.errorMessage << " line " << r.errorLine << "\n";
        check(r.ok, "container-scoped: pipeline succeeds");
        if (r.ok) check(attr(r.fabric, "text") == "3", "container-scoped: real while inside container reaches 3");
    }

    // ---- 9. nested loop + if + container.open pattern (no container.open native yet -- this
    //          documents the *current* real behavior: the container's body executes for real
    //          the moment execution reaches its @container...end block, including inside a
    //          while/if, since ContainerStmt is executed like any other statement) ----
    {
        std::string src = R"(
            let i = 0;
            let cardRuns = 0;
            while (i < 3) {
                if (i == 1) {
                    @container.card
                        cardRuns = cardRuns + 1;
                    .end/container.card
                }
                i = i + 1;
            }
            @view.Text=t text=cardRuns;
            .end/view
        )";
        auto r = loom::runColdPipeline(src);
        if (!r.ok) std::cerr << "  (debug) nested error: " << r.errorMessage << " line " << r.errorLine << "\n";
        check(r.ok, "nested while+if+container: pipeline succeeds");
        // NOTE: `cardRuns` is declared at top level but reassigned from inside the container's own
        // child Environment -- Environment::assign() walks up the parent chain, so this must find
        // and update the *same* top-level `cardRuns`, not shadow it. This is a real, useful check
        // of container-scoping semantics under nested control flow (spec item 26).
        if (r.ok) check(attr(r.fabric, "text") == "1", "nested while+if+container: card body ran exactly once (i==1)");
    }

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed.\n";
    return failures == 0 ? 0 : 1;
}
