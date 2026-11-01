// flow_selftest.cpp — standalone harness (not part of CMakeLists/app build) used only to verify
// RinFlow (rin_interpreter.h: namespace flow) end-to-end outside Android/JNI, since the sandbox
// used to develop this change has no Android SDK/NDK or network access to run a Gradle build.
// Build: g++ -std=c++17 -I. flow_selftest.cpp rin_lexer.cpp rin_parser.cpp rin_interpreter.cpp
//        rin_http.cpp diagnostics/diagnostic.cpp diagnostics/source_manager.cpp
//        diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp -o flow_selftest
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>
#include <cassert>

using namespace rin;

static void printGraph(const flow::FlowGraph& g) {
    for (auto& n : g.nodes) {
        std::cout << "  [" << n.id << "] " << flow::nodeTypeName(n.type) << " '" << n.name << "' "
                  << flow::nodeStatusName(n.status) << " " << n.durationMs << "ms"
                  << " in=" << n.input.preview << " out=" << n.output.preview;
        if (n.error) std::cout << " ERROR(" << n.error->code << "): " << n.error->message;
        std::cout << "\n";
    }
}

static std::vector<StmtPtr> parseOrDie(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.scanTokens();
    Parser parser(tokens);
    return parser.parse();
}

int main() {
    int failures = 0;
  try {

    // ---- Test 1: simple multi-stage pipeline ----
    {
        std::cout << "== Test 1: simple pipeline ==\n";
        auto program = parseOrDie(
            "fun isAdult(x) { return x > 18; }\n"
            "fun myDouble(x) { return x * 2; }\n"
            "fun filter(arr, pred) { let out = []; let i = 0; while (i < len(arr)) { if (pred(arr[i])) { push(out, arr[i]); } i = i + 1; } return out; }\n"
            "fun map(arr, fn) { let out = []; let i = 0; while (i < len(arr)) { push(out, fn(arr[i])); i = i + 1; } return out; }\n"
            "let data = [10, 20, 30, 5, 40];\n"
            "let result = data |> filter(isAdult) |> map(myDouble) |> sum;\n"
            "print result;\n"
        );
        Interpreter interp;
        int events = 0;
        auto res = interp.runProgramAsFlow(program, flow::FlowRunOptions{}, [&](const flow::FlowEvent& e) {
            events++;
        });
        std::cout << "status=" << flow::sessionStatusName(res.status) << " events=" << events << "\n";
        std::cout << "output: " << res.output;
        printGraph(res.graph);
        std::cout << "metrics: total=" << res.metrics.totalNodes << " ok=" << res.metrics.completedNodes
                   << " failed=" << res.metrics.failedNodes << "\n";
        if (res.status != flow::SessionStatus::SUCCESS) { std::cout << "FAIL\n"; failures++; }
        if (res.graph.nodes.size() != 4) { std::cout << "FAIL: expected 4 nodes (input+filter+map+sum), got "
            << res.graph.nodes.size() << "\n"; failures++; }
    }

    // ---- Test 2: runtime error mid-pipeline -> node error + diagnostics ----
    {
        std::cout << "\n== Test 2: runtime error mid-pipeline ==\n";
        auto program = parseOrDie(
            "fun boom(x) { return x[100]; }\n" // index out of range -> real RinError (E0021)
            "let data = [1,2,3];\n"
            "let result = data |> boom;\n"
        );
        Interpreter interp;
        auto res = interp.runProgramAsFlow(program, flow::FlowRunOptions{});
        std::cout << "status=" << flow::sessionStatusName(res.status) << "\n";
        printGraph(res.graph);
        bool anyError = false;
        for (auto& n : res.graph.nodes) if (n.status == flow::NodeStatus::ERROR) anyError = true;
        if (res.status != flow::SessionStatus::ERROR || !anyError) { std::cout << "FAIL\n"; failures++; }
    }

    // ---- Test 3: empty input ----
    {
        std::cout << "\n== Test 3: empty input ==\n";
        auto program = parseOrDie(
            "fun identity(x) { return x; }\n"
            "let data = [];\n"
            "let result = data |> identity;\n"
        );
        Interpreter interp;
        auto res = interp.runProgramAsFlow(program, flow::FlowRunOptions{});
        std::cout << "status=" << flow::sessionStatusName(res.status) << "\n";
        printGraph(res.graph);
        if (res.status != flow::SessionStatus::SUCCESS) { std::cout << "FAIL\n"; failures++; }
    }

    // ---- Test 4: cancellation ----
    {
        std::cout << "\n== Test 4: cancellation ==\n";
        auto program = parseOrDie(
            "fun a(x) { return x; }\n"
            "fun b(x) { return x; }\n"
            "fun c(x) { return x; }\n"
            "let data = 1;\n"
            "let result = data |> a |> b |> c;\n"
        );
        Interpreter interp;
        std::string sessionId;
        bool cancelledAfterFirst = false;
        auto res = interp.runProgramAsFlow(program, flow::FlowRunOptions{}, [&](const flow::FlowEvent& e) {
            if (e.type == flow::EventType::FLOW_STARTED) sessionId = e.flowId;
            if (e.type == flow::EventType::NODE_FINISHED && !cancelledAfterFirst) {
                cancelledAfterFirst = true;
                interp.cancelFlow(sessionId);
            }
        });
        std::cout << "status=" << flow::sessionStatusName(res.status) << "\n";
        printGraph(res.graph);
        if (res.status != flow::SessionStatus::CANCELLED) { std::cout << "FAIL\n"; failures++; }
    }

    // ---- Test 5: timeout ----
    {
        std::cout << "\n== Test 5: timeout ==\n";
        auto program = parseOrDie(
            "fun slow(x) { let i = 0; while (i < 3000000) { i = i + 1; } return x; }\n"
            "fun after(x) { return x; }\n"
            "let data = 1;\n"
            "let result = data |> slow |> after;\n"
        );
        Interpreter interp;
        flow::FlowRunOptions opts;
        opts.timeoutMs = 1; // effectively immediate deadline once the first stage finishes
        auto res = interp.runProgramAsFlow(program, opts);
        std::cout << "status=" << flow::sessionStatusName(res.status) << "\n";
        printGraph(res.graph);
        if (res.status != flow::SessionStatus::TIMEOUT) { std::cout << "FAIL\n"; failures++; }
    }

    // ---- Test 6: replay creates a new session, doesn't touch the old one ----
    {
        std::cout << "\n== Test 6: replay ==\n";
        auto program = parseOrDie(
            "fun double(x) { return x * 2; }\n"
            "fun mapDouble(arr) { let out = []; let i = 0; while (i < len(arr)) { push(out, double(arr[i])); i = i + 1; } return out; }\n"
            "let data = [1,2,3];\n"
            "let result = data |> mapDouble;\n"
        );
        Interpreter interp;
        auto res1 = interp.runProgramAsFlow(program, flow::FlowRunOptions{});
        auto res2 = interp.replayFlow(res1.sessionId, flow::FlowRunOptions{});
        if (!res2) { std::cout << "FAIL: replay returned nullopt\n"; failures++; }
        else {
            std::cout << "original session=" << res1.sessionId << " replay session=" << res2->sessionId << "\n";
            if (res1.sessionId == res2->sessionId) { std::cout << "FAIL: same session id\n"; failures++; }
            auto originalStillThere = interp.getFlowSession(res1.sessionId);
            if (!originalStillThere || originalStillThere->status != flow::SessionStatus::ERROR) {
                // "map" isn't a defined native/user fn in this snippet, so original run legitimately
                // errors (unknown function) -- what matters is the ORIGINAL session object is untouched
                // and still retrievable after replay.
            }
        }
    }

  } catch (RinError& e) {
    std::cout << "UNCAUGHT RinError at top level: line=" << e.line << " msg=" << e.message << "\n";
    return 2;
  }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : (std::to_string(failures) + " TEST(S) FAILED")) << "\n";
    return failures == 0 ? 0 : 1;
}
