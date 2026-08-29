// jni_bridge.cpp
// Exposes the Rin C++ engine to Kotlin through JNI.
// Kotlin side: RinEngine.kt declares the matching `external fun` signatures.
#include <jni.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <memory>
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "rin_http.h"
#include "diagnostics/diagnostic_renderer.h"
#include "loom/rin_loom_c_api.h"

// runSourceNative(source, baseDir) -> baseDir هو جذر حقيقي على القرص (عادة filesDir الخاص بالتطبيق
// على أندرويد) تُبنى فوقه كل عمليات save/file/installation/writeFile/readFile الحقيقية. RinEngine.kt
// يمرّر هذا الباراميتر تلقائياً (فارغ إن لم يُستدعَ RinEngine.init(context) بعد)، لذا لا حاجة لتغيير
// أي كود Kotlin قديم يستدعي RinEngine.runSource(source) بباراميتر واحد.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring baseDirJStr) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);

    std::string baseDir;
    if (baseDirJStr != nullptr) {
        const char* cBaseDir = env->GetStringUTFChars(baseDirJStr, nullptr);
        baseDir = cBaseDir ? cBaseDir : "";
        env->ReleaseStringUTFChars(baseDirJStr, cBaseDir);
    }

    std::string result;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interpreter;
        if (!baseDir.empty()) {
            interpreter.setBasePath(baseDir);
        }
        result = interpreter.run(statements);
        if (result.empty()) {
            result = "(no output)";
        }
    } catch (rin::RinError& e) {
        result = "[Syntax error, line " + std::to_string(e.line) + "]: " + e.message;
    } catch (std::exception& e) {
        result = std::string("[Internal error]: ") + e.what();
    } catch (...) {
        result = "[Unknown internal error]";
    }

    return env->NewStringUTF(result.c_str());
}

// ---------------------------------------------------------------------------
// runSourceStructuredNative(source, baseDir) -> JSON
//
// Additive, backward-compatible sibling of runSourceNative above. Returns a
// structured result instead of a single opaque string, so the Kotlin side
// (RinJobScheduler / RinExecutionManager) can tell SUCCESS from ERROR from a
// *real* execution outcome instead of sniffing whether the printed output
// happens to start with '[' -- which is wrong whenever a program legitimately
// prints something like `print ["hello", "world"];`.
//
// Shape:
//   { "status": "SUCCESS" | "ERROR",
//     "output": "...",                 // everything the program printed, always present
//     "diagnostic": { ... } | null,    // rich diag::Diagnostic JSON (see diagnostic_renderer.cpp)
//     "diagnosticText": "..." | null,  // same diagnostic pre-rendered rustc-style, ready to display
//     "errorMessage": "..." | null,    // plain fallback message when no rich diagnostic exists
//     "errorLine": N }                 // 0 when there is no error
//
// "status" here only ever distinguishes SUCCESS/ERROR for *this* call; TIMEOUT and CANCELLED are
// scheduler-level outcomes (the native call was never interrupted or never returned) and are
// decided on the Kotlin side by RinJobScheduler, same as before.
static std::string jsonEscapeLocal(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

namespace {

// نتيجة تنفيذ structured واحدة، قبل تحويلها إلى JSON -- مستخرجة كي يشترك فيها كل من
// runSourceStructuredNative (بلا بث) وrunSourceStructuredStreamingNative (ببث حي)، بدل تكرار نفس
// منطق lexer/parser/interpreter/catch مرتين (قسم 28: لا تُنشئ نسخًا مكررة).
struct StructuredRunOutcome {
    std::string status = "SUCCESS";
    std::string output;
    std::string diagnosticJson;   // فارغ -> يُصدَّر كـ JSON null
    std::string diagnosticText;   // فارغ -> يُصدَّر كـ JSON null
    std::string errorMessage;     // فارغ -> يُصدَّر كـ JSON null
    int errorLine = 0;
};

// [sink] اختياري: مرَّرها مباشرة كما هي إلى Interpreter::setStreamSink قبل run() (انظر
// rin_interpreter.h/.cpp). فارغة (nullptr) => نفس سلوك runSourceStructuredNative القديم تمامًا،
// بلا أي تغيير في التوقيت أو الناتج.
StructuredRunOutcome runStructuredCore(const std::string& source, const std::string& baseDir,
                                        rin::Interpreter::StreamSink sink) {
    StructuredRunOutcome r;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interpreter;
        if (!baseDir.empty()) {
            interpreter.setBasePath(baseDir);
        }
        if (sink) {
            interpreter.setStreamSink(std::move(sink));
        }
        r.output = interpreter.run(statements);
        if (interpreter.hadError()) {
            r.status = "ERROR";
            if (interpreter.lastDiagnostic()) {
                r.diagnosticJson = rin::diag::renderJson(*interpreter.lastDiagnostic());
                r.diagnosticText = rin::diag::renderPlain(*interpreter.lastDiagnostic(), rin::diag::globalSourceManager());
            }
            if (interpreter.lastErrorMessage()) r.errorMessage = *interpreter.lastErrorMessage();
            r.errorLine = interpreter.lastErrorLine();
        }
        if (r.output.empty() && r.status == "SUCCESS") r.output = "(no output)";
    } catch (rin::RinError& e) {
        // Lexer/parser-stage failure: execution never even started (no top-level statement loop
        // ever ran, so no stream events were ever emitted for this run -- consistent with there
        // being no output at all in this case).
        r.status = "ERROR";
        r.errorMessage = e.message;
        r.errorLine = e.line;
        if (e.diagnostic) {
            r.diagnosticJson = rin::diag::renderJson(*e.diagnostic);
            r.diagnosticText = rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());
        }
        r.output = "";
    } catch (std::exception& e) {
        r.status = "ERROR";
        r.errorMessage = std::string("Internal error: ") + e.what();
        r.output = "";
    } catch (...) {
        r.status = "ERROR";
        r.errorMessage = "Unknown internal error";
        r.output = "";
    }
    return r;
}

std::string structuredOutcomeToJson(const StructuredRunOutcome& r) {
    std::ostringstream json;
    json << "{"
         << "\"status\":\"" << r.status << "\","
         << "\"output\":\"" << jsonEscapeLocal(r.output) << "\","
         << "\"diagnostic\":" << (r.diagnosticJson.empty() ? "null" : r.diagnosticJson) << ","
         << "\"diagnosticText\":" << (r.diagnosticText.empty() ? "null" : ("\"" + jsonEscapeLocal(r.diagnosticText) + "\"")) << ","
         << "\"errorMessage\":" << (r.errorMessage.empty() ? "null" : ("\"" + jsonEscapeLocal(r.errorMessage) + "\""))
         << ",\"errorLine\":" << r.errorLine
         << "}";
    return json.str();
}

// يحوّل jstring/jstring? إلى std::string مرة واحدة (يُستخدم من كلا الدالتين أدناه).
std::string jstringOrEmpty(JNIEnv* env, jstring s) {
    if (s == nullptr) return std::string();
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceStructuredNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring baseDirJStr) {
    std::string source = jstringOrEmpty(env, sourceJStr);
    std::string baseDir = jstringOrEmpty(env, baseDirJStr);
    StructuredRunOutcome outcome = runStructuredCore(source, baseDir, nullptr);
    return env->NewStringUTF(structuredOutcomeToJson(outcome).c_str());
}

// ---------------------------------------------------------------------------
// runSourceStructuredStreamingNative(source, baseDir, listener) -> نفس JSON الذي تُعيده
// runSourceStructuredNative أعلاه تمامًا (نتيجة نهائية واحدة، متوافقة مع RinExecutionResult.parse
// في Kotlin بلا أي تغيير)، لكن مع استدعاء listener.onChunk(sequence, chunk) *أثناء* التنفيذ، مرة
// واحدة لكل statement علوي أضاف ناتجًا جديدًا (انظر Interpreter::run في rin_interpreter.cpp).
//
// أمان الترابط (thread-safety): هذا الاستدعاء *لا* يُنشئ أي ترد (thread) جديد ولا يحتاج
// AttachCurrentThread/GlobalRef -- استدعاءات onChunk كلها تحدث بشكل متزامن (synchronous)، على
// نفس الترد ونفس إطار الاستدعاء الذي دخل منه Kotlin إلى JNI أصلاً، قبل أن تعود هذه الدالة. لذا
// listener (كمرجع محلي/local ref) يبقى صالحًا طوال الوقت بلا حاجة لأي GlobalRef.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceStructuredStreamingNative(JNIEnv* env, jobject /* this */,
                                                                    jstring sourceJStr, jstring baseDirJStr,
                                                                    jobject listener) {
    std::string source = jstringOrEmpty(env, sourceJStr);
    std::string baseDir = jstringOrEmpty(env, baseDirJStr);

    jmethodID onChunkMethod = nullptr;
    if (listener != nullptr) {
        jclass listenerClass = env->GetObjectClass(listener);
        onChunkMethod = env->GetMethodID(listenerClass, "onChunk", "(ILjava/lang/String;)V");
        env->DeleteLocalRef(listenerClass);
        if (onChunkMethod == nullptr) {
            // لا تُسقط الاستثناء الناتج عن GetMethodID الفاشل بصمت خارج هذه الدالة -- امسحه وتابع
            // بلا بث (يتدهور بأمان إلى سلوك غير-متدفق بدل تعطّل التنفيذ بالكامل).
            env->ExceptionClear();
        }
    }

    int seq = 0;
    rin::Interpreter::StreamSink sink;
    if (onChunkMethod != nullptr) {
        sink = [env, listener, onChunkMethod, &seq](const std::string& chunk) {
            ++seq;
            jstring jchunk = env->NewStringUTF(chunk.c_str());
            env->CallVoidMethod(listener, onChunkMethod, static_cast<jint>(seq), jchunk);
            env->DeleteLocalRef(jchunk);
            // خطأ Kotlin/RuntimeException داخل onChunk (مثلاً في مستمع مكتوب بشكل خاطئ) يجب ألا
            // يُسقط التنفيذ الأصلي للغة Rin نفسها -- امسحه وتابع البث/التنفيذ.
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        };
    }

    StructuredRunOutcome outcome = runStructuredCore(source, baseDir, std::move(sink));
    return env->NewStringUTF(structuredOutcomeToJson(outcome).c_str());
}

// ---------------------------------------------------------------------------
// RinFlow — Execution Flow Engine JNI bridge (runFlowNative / cancelFlowNative /
// replayFlowNative). See rin_interpreter.h (namespace rin::flow) for the actual engine; this
// section only serializes its structures to JSON, following the exact same conventions as
// structuredOutcomeToJson()/jsonEscapeLocal() above (additive, does not touch any existing
// jni_bridge function).
// ---------------------------------------------------------------------------
namespace {

std::string flowNodeToJson(const rin::flow::FlowNode& n) {
    std::ostringstream j;
    j << "{"
      << "\"id\":" << n.id << ","
      << "\"type\":\"" << rin::flow::nodeTypeName(n.type) << "\","
      << "\"name\":\"" << jsonEscapeLocal(n.name) << "\","
      << "\"status\":\"" << rin::flow::nodeStatusName(n.status) << "\","
      << "\"input\":" << (n.input.available ? ("{\"preview\":\"" + jsonEscapeLocal(n.input.preview) +
            "\",\"recordCount\":" + std::to_string(n.input.recordCount) +
            ",\"truncated\":" + (n.input.truncated ? "true" : "false") + "}") : "null") << ","
      << "\"output\":" << (n.output.available ? ("{\"preview\":\"" + jsonEscapeLocal(n.output.preview) +
            "\",\"recordCount\":" + std::to_string(n.output.recordCount) +
            ",\"truncated\":" + (n.output.truncated ? "true" : "false") + "}") : "null") << ","
      << "\"startedAt\":" << n.startedAt << ","
      << "\"finishedAt\":" << n.finishedAt << ","
      << "\"durationMs\":" << n.durationMs << ","
      << "\"line\":" << n.line << ","
      << "\"column\":" << n.column << ","
      << "\"error\":" << (n.error.has_value() ? ("{\"code\":\"" + jsonEscapeLocal(n.error->code) +
            "\",\"message\":\"" + jsonEscapeLocal(n.error->message) +
            "\",\"line\":" + std::to_string(n.error->line) +
            ",\"column\":" + std::to_string(n.error->column) + "}") : "null")
      << "}";
    return j.str();
}

std::string flowGraphToJson(const rin::flow::FlowGraph& g) {
    std::ostringstream j;
    j << "{\"nodes\":[";
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        if (i) j << ",";
        j << flowNodeToJson(g.nodes[i]);
    }
    j << "],\"edges\":[";
    for (size_t i = 0; i < g.edges.size(); ++i) {
        if (i) j << ",";
        j << "[" << g.edges[i].first << "," << g.edges[i].second << "]";
    }
    j << "]}";
    return j.str();
}

std::string flowMetricsToJson(const rin::flow::FlowMetrics& m) {
    std::ostringstream j;
    j << "{"
      << "\"totalNodes\":" << m.totalNodes << ","
      << "\"completedNodes\":" << m.completedNodes << ","
      << "\"failedNodes\":" << m.failedNodes << ","
      << "\"skippedNodes\":" << m.skippedNodes << ","
      << "\"cancelledNodes\":" << m.cancelledNodes << ","
      << "\"timeoutNodes\":" << m.timeoutNodes << ","
      << "\"totalDurationMs\":" << m.totalDurationMs << ","
      << "\"totalInputRecords\":" << m.totalInputRecords << ","
      << "\"totalOutputRecords\":" << m.totalOutputRecords
      << "}";
    return j.str();
}

std::string flowEventToJson(const rin::flow::FlowEvent& e) {
    std::ostringstream j;
    j << "{"
      << "\"sequence\":" << e.sequence << ","
      << "\"timestamp\":" << e.timestamp << ","
      << "\"flowId\":\"" << jsonEscapeLocal(e.flowId) << "\","
      << "\"nodeId\":" << e.nodeId << ","
      << "\"type\":\"" << rin::flow::eventTypeName(e.type) << "\","
      << "\"message\":\"" << jsonEscapeLocal(e.message) << "\","
      << "\"line\":" << e.line << ","
      << "\"column\":" << e.column << ","
      << "\"durationMs\":" << e.durationMs
      << "}";
    return j.str();
}

std::string flowRunResultToJson(const rin::Interpreter::FlowRunResult& r) {
    std::ostringstream j;
    j << "{"
      << "\"sessionId\":\"" << jsonEscapeLocal(r.sessionId) << "\","
      << "\"status\":\"" << rin::flow::sessionStatusName(r.status) << "\","
      << "\"output\":\"" << jsonEscapeLocal(r.output) << "\","
      << "\"graph\":" << flowGraphToJson(r.graph) << ","
      << "\"metrics\":" << flowMetricsToJson(r.metrics)
      << "}";
    return j.str();
}

} // namespace

// Registry of Interpreters that ran at least one Flow, kept alive (bounded, LRU-pruned) so that:
//   (a) cancelFlowNative can reach a still-RUNNING session from another thread while runFlowNative
//       is still blocked on its own thread/Java call (RinJobScheduler's worker thread; see its
//       .kt file), and
//   (b) replayFlowNative (section 11) can be called *after* runFlowNative already returned, since
//       Interpreter::replayFlow needs the same Interpreter instance that owns the RinFlowEngine +
//       the original session's captured root-expression/environment.
// Every session id a given Interpreter has ever produced (its original run, plus every replay of
// it or of a replay) maps to that same shared_ptr, so any of those ids can be used to cancel or
// further replay the whole family. Bounded to kMaxKeptInterpreters distinct Interpreters (oldest
// first) so a long-running app session can't leak native memory across thousands of flow runs --
// same "don't accumulate without limit" philosophy as PipelineTracer::kMaxEvents.
namespace {
std::mutex g_liveFlowMu;
std::unordered_map<std::string, std::shared_ptr<rin::Interpreter>> g_liveFlowInterpreters;
std::vector<std::string> g_liveFlowOrder; // primary (non-replay) session ids, oldest first
constexpr size_t kMaxKeptInterpreters = 30;

void registerFlowInterpreter(const std::string& sessionId, const std::shared_ptr<rin::Interpreter>& interp) {
    std::lock_guard<std::mutex> lk(g_liveFlowMu);
    g_liveFlowInterpreters[sessionId] = interp;
    g_liveFlowOrder.push_back(sessionId);
    while (g_liveFlowOrder.size() > kMaxKeptInterpreters) {
        auto oldestId = g_liveFlowOrder.front();
        g_liveFlowOrder.erase(g_liveFlowOrder.begin());
        auto it = g_liveFlowInterpreters.find(oldestId);
        // لا نحذف Interpreter ما تزال إحدى جلساته RUNNING فعلياً (نفس منطق RinFlowEngine::createSession
        // في rin_interpreter.cpp: لا تفقد جلسة نشطة أثناء التقليم).
        if (it != g_liveFlowInterpreters.end()) {
            auto session = it->second->getFlowSession(oldestId);
            if (session && session->status == rin::flow::SessionStatus::RUNNING) {
                g_liveFlowOrder.push_back(oldestId); // أعِدها لآخر الطابور وتوقّف عن التقليم الآن
                break;
            }
            g_liveFlowInterpreters.erase(it);
        }
    }
}

std::shared_ptr<rin::Interpreter> lookupFlowInterpreter(const std::string& sessionId) {
    std::lock_guard<std::mutex> lk(g_liveFlowMu);
    auto it = g_liveFlowInterpreters.find(sessionId);
    return it == g_liveFlowInterpreters.end() ? nullptr : it->second;
}

std::string flowInternalErrorJson(const std::string& message, int line) {
    std::ostringstream j;
    j << "{\"sessionId\":\"\",\"status\":\"ERROR\",\"output\":\"\",\"graph\":{\"nodes\":[],\"edges\":[]},"
      << "\"metrics\":{\"totalNodes\":0,\"completedNodes\":0,\"failedNodes\":0,\"skippedNodes\":0,"
      << "\"cancelledNodes\":0,\"timeoutNodes\":0,\"totalDurationMs\":0,\"totalInputRecords\":0,"
      << "\"totalOutputRecords\":0},\"parseError\":\"" << jsonEscapeLocal(message) << "\","
      << "\"parseErrorLine\":" << line << "}";
    return j.str();
}

rin::flow::EventSink makeListenerSink(JNIEnv* env, jobject listener) {
    if (listener == nullptr) return nullptr;
    jclass listenerClass = env->GetObjectClass(listener);
    jmethodID onFlowEventMethod = env->GetMethodID(listenerClass, "onFlowEvent", "(Ljava/lang/String;)V");
    env->DeleteLocalRef(listenerClass);
    if (onFlowEventMethod == nullptr) { env->ExceptionClear(); return nullptr; }
    return [env, listener, onFlowEventMethod](const rin::flow::FlowEvent& e) {
        jstring jjson = env->NewStringUTF(flowEventToJson(e).c_str());
        env->CallVoidMethod(listener, onFlowEventMethod, jjson);
        env->DeleteLocalRef(jjson);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };
}

} // namespace

// runFlowNative(source, baseDir, timeoutMs, listener) -> JSON (rin::Interpreter::FlowRunResult
// shape above). [listener], if non-null, receives listener.onFlowEvent(json) synchronously for
// every rin::flow::FlowEvent as the flow actually executes -- same synchronous, no-new-thread,
// no-GlobalRef-needed contract as runSourceStructuredStreamingNative above (see its comment).
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runFlowNative(JNIEnv* env, jobject /* this */,
                                                jstring sourceJStr, jstring baseDirJStr,
                                                jlong timeoutMs, jobject listener) {
    std::string source = jstringOrEmpty(env, sourceJStr);
    std::string baseDir = jstringOrEmpty(env, baseDirJStr);
    rin::flow::EventSink sink = makeListenerSink(env, listener);

    std::string resultJson;
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();

        auto interpreter = std::make_shared<rin::Interpreter>();
        if (!baseDir.empty()) interpreter->setBasePath(baseDir);
        rin::flow::FlowRunOptions opts;
        opts.timeoutMs = static_cast<long long>(timeoutMs);

        // نسجّل الـ Interpreter فور معرفة sessionId (أول حدث FLOW_STARTED) لا بعد انتهاء التشغيل،
        // حتى يعمل cancelFlowNative من ترد آخر أثناء تنفيذ flow طويل، ويبقى مسجَّلاً بعد العودة
        // حتى تعمل replayFlowNative لاحقاً (انظر تعليق السجل أعلاه).
        std::string sessionId;
        rin::flow::EventSink wrappedSink = [&](const rin::flow::FlowEvent& e) {
            if (e.type == rin::flow::EventType::FLOW_STARTED && sessionId.empty()) {
                sessionId = e.flowId;
                registerFlowInterpreter(sessionId, interpreter);
            }
            if (sink) sink(e);
        };
        auto result = interpreter->runProgramAsFlow(statements, opts, wrappedSink);
        resultJson = flowRunResultToJson(result);
    } catch (rin::RinError& e) {
        resultJson = flowInternalErrorJson(e.message, e.line); // فشل في مرحلة lexer/parser: لا جلسة Flow بدأت أصلاً
    } catch (std::exception& e) {
        resultJson = flowInternalErrorJson(std::string("Internal error: ") + e.what(), 0);
    }
    return env->NewStringUTF(resultJson.c_str());
}

// cancelFlowNative(sessionId) -> true if a RUNNING flow session with this id was found and its
// cancellation flag was raised (see rin::flow::FlowSession::cancelFlag). Cooperative: the flow
// only actually stops at the next |> stage boundary it checks (see
// Interpreter::evaluatePipelineFlow), exactly like RinJobScheduler's own documented limitation
// for whole-program timeouts.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dlof_rinlang_RinEngine_cancelFlowNative(JNIEnv* env, jobject /* this */, jstring sessionIdJStr) {
    std::string sessionId = jstringOrEmpty(env, sessionIdJStr);
    auto interp = lookupFlowInterpreter(sessionId);
    if (!interp) return JNI_FALSE;
    return interp->cancelFlow(sessionId) ? JNI_TRUE : JNI_FALSE;
}

// replayFlowNative(previousSessionId, timeoutMs, listener) -> JSON, same shape as runFlowNative's
// result but for a brand-new session that re-executes the last `|>` chain the referenced session
// ran, in a fresh FlowSession (section 11: Replay never touches the original session -- see
// Interpreter::replayFlow in rin_interpreter.cpp). Returns a JSON object with only
// {"sessionId":"","status":"ERROR","parseError":"..."} if [previousSessionId] is unknown (already
// pruned -- see kMaxKeptInterpreters -- or never ran a `|>` chain at all).
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_replayFlowNative(JNIEnv* env, jobject /* this */,
                                                   jstring previousSessionIdJStr,
                                                   jlong timeoutMs, jobject listener) {
    std::string previousSessionId = jstringOrEmpty(env, previousSessionIdJStr);
    auto interp = lookupFlowInterpreter(previousSessionId);
    if (!interp) {
        return env->NewStringUTF(flowInternalErrorJson(
            "Unknown or expired flow session id (cannot replay): " + previousSessionId, 0).c_str());
    }
    rin::flow::EventSink sink = makeListenerSink(env, listener);

    std::string newSessionId;
    rin::flow::EventSink wrappedSink = [&](const rin::flow::FlowEvent& e) {
        if (e.type == rin::flow::EventType::FLOW_STARTED && newSessionId.empty()) {
            newSessionId = e.flowId;
            registerFlowInterpreter(newSessionId, interp); // نفس الـ Interpreter، جلسة/id جديدان
        }
        if (sink) sink(e);
    };

    rin::flow::FlowRunOptions opts;
    opts.timeoutMs = static_cast<long long>(timeoutMs);
    auto result = interp->replayFlow(previousSessionId, opts, wrappedSink);
    if (!result) {
        return env->NewStringUTF(flowInternalErrorJson(
            "Session " + previousSessionId + " never executed a |> pipeline; nothing to replay.", 0).c_str());
    }
    return env->NewStringUTF(flowRunResultToJson(*result).c_str());
}


extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_engineVersion(JNIEnv* env, jobject /* this */) {
    return env->NewStringUTF("Rin Engine 1.1 (C++17) — save/file/installation حقيقية على القرص، RinFlow حقيقي");
}



// renderViewNative(source, rootWidth) -> Loomtime: يحلّل @view.<Kind>=name، يبني الـ Fabric،
// يُخطِّطه (Loom) عند العرض rootWidth (بالبكسل)، ويُعيد تفريغ JSON كامل (kind/name/سطر المصدر/
// هندسة/سمات مُحلَّلة، تكرارياً) يستهلكه جانب Kotlin/Canvas لرسم الواجهة فعلياً. عند فشل التحليل
// يُعاد JSON بالشكل {"error": "...", "line": N} بدل رمي استثناء عبر حدود JNI.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_renderViewNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jint rootWidth) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);

    char* json = rin_loom_render_json(source.c_str(), (int)rootWidth);
    jstring result = env->NewStringUTF(json ? json : "{\"error\":\"null result\",\"line\":0}");
    rin_free_string(json);
    return result;
}

// renderContainerViewNative(source, containerName, rootWidth) -> نفس renderViewNative أعلاه، لكن
// يبني الـ Fabric من @view المُعرَّف داخل الحاوية containerName بعينها (وليس جذر البرنامج العلوي)،
// وهو الوجه الجديد الذي يجعل Loomtime مربوطة فعلياً بـ container: كل @container يحمل @view/warp/
// @theme خاصة به يصبح شاشة/عنصر واجهة مستقلاً قابلاً للعرض باسمه، مع warp/theme الخاصين بتلك
// الحاوية فقط. نفس شكل JSON الناتج (أو {"error":"...", "line":N} عند الفشل).
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_renderContainerViewNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring containerNameJStr, jint rootWidth) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);
    const char* cName = env->GetStringUTFChars(containerNameJStr, nullptr);
    std::string containerName(cName ? cName : "");
    env->ReleaseStringUTFChars(containerNameJStr, cName);

    char* json = rin_loom_render_container_json(source.c_str(), containerName.c_str(), (int)rootWidth);
    jstring result = env->NewStringUTF(json ? json : "{\"error\":\"null result\",\"line\":0}");
    rin_free_string(json);
    return result;
}

// ---- Loomtime session (Needle): a persistent Fabric+Warp session so a live-preview tap can
// actually run its onTap handler (real fun/while loop or a built-in Warp op) and see the result,
// instead of renderViewNative's stateless one-shot render. The native session pointer is boxed as
// a jlong handle on the Kotlin side (see RinEngine.kt's LoomSession wrapper) -- standard JNI
// pattern for opaque native resources that must outlive a single call.

extern "C" JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionCreateNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jint rootWidth) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);
    void* session = rin_loom_session_create(source.c_str(), (int)rootWidth);
    return reinterpret_cast<jlong>(session);
}

// loomSessionCreateForContainerNative(source, containerName, rootWidth) -> نفس الجلسة أعلاه، لكن
// حالتها (Fabric+Warp) مبنية من @view/warp/@theme داخل الحاوية containerName بعينها، فيمكن لأي
// tap لاحق (loomSessionTapNative) أن يعمل بشكل طبيعي على warp/onTap الخاصين بتلك الحاوية فقط.
extern "C" JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionCreateForContainerNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring containerNameJStr, jint rootWidth) {
    const char* cSource = env->GetStringUTFChars(sourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(sourceJStr, cSource);
    const char* cName = env->GetStringUTFChars(containerNameJStr, nullptr);
    std::string containerName(cName ? cName : "");
    env->ReleaseStringUTFChars(containerNameJStr, cName);
    void* session = rin_loom_session_create_for_container(source.c_str(), containerName.c_str(), (int)rootWidth);
    return reinterpret_cast<jlong>(session);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionRenderJsonNative(JNIEnv* env, jobject /* this */, jlong handle) {
    char* json = rin_loom_session_render_json(reinterpret_cast<void*>(handle));
    jstring result = env->NewStringUTF(json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionTapNative(JNIEnv* env, jobject /* this */, jlong handle, jdouble x, jdouble y) {
    char* json = rin_loom_session_tap(reinterpret_cast<void*>(handle), (double)x, (double)y);
    jstring result = env->NewStringUTF(json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionUpdateSourceNative(JNIEnv* env, jobject /* this */, jlong handle, jstring newSourceJStr) {
    const char* cSource = env->GetStringUTFChars(newSourceJStr, nullptr);
    std::string source(cSource ? cSource : "");
    env->ReleaseStringUTFChars(newSourceJStr, cSource);
    char* json = rin_loom_session_update_source(reinterpret_cast<void*>(handle), source.c_str());
    jstring result = env->NewStringUTF(json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinEngine_loomSessionFreeNative(JNIEnv* /* env */, jobject /* this */, jlong handle) {
    rin_loom_session_free(reinterpret_cast<void*>(handle));
}

// ================= جسر HTTP الحقيقي: JNI_OnLoad + native -> Kotlin (RinHttpBridge) =================
// لماذا هنا تحديداً وليس داخل rin_http.cpp؟ rin_http.h/.cpp مصمَّمان عمداً بلا أي اعتماد على
// <jni.h> (انظر تعليق rin_http.h) حتى يبقيا قابلين للبناء كأداة سطر أوامر عادية بلا NDK. كل ما
// يخص JNI فعلياً — بما فيه تخزين JavaVM* واستدعاء RinHttpBridge.request(...) في Kotlin —
// محصور بالكامل هنا، ويُسجَّل لمرة واحدة عند تحميل المكتبة عبر JNI_OnLoad (يُستدعى تلقائياً من
// نظام أندرويد فور System.loadLibrary("rinengine") في RinEngine.kt، قبل أي كود Rin يعمل).

namespace {

JavaVM* g_javaVm = nullptr;
jclass g_httpBridgeClass = nullptr;          // global ref لصف Kotlin com.dlof.rinlang.RinHttpBridge
jmethodID g_httpBridgeRequestMethod = nullptr; // MethodID لـ RinHttpBridge.request(...) الثابتة (static)

// FindClass لا يعمل بأمان إلا من الترد (thread) الذي استُدعي منه System.loadLibrary أصلاً (أي هنا
// داخل JNI_OnLoad نفسه) لأنه وقتها فقط يملك سياق مُحمِّل الأصناف (ClassLoader) الخاص بالتطبيق؛ أي
// استدعاء FindClass لاحقاً من ترد خلفي (worker thread) مُرفَق عبر AttachCurrentThread سيفشل غالباً
// لأنه يستخدم مُحمِّل الأصناف الجذري (bootstrap classloader) الذي لا يعرف أصناف التطبيق. لذا نجلب
// الصف والتوقيع مرة واحدة هنا ونُبقيهما كـ global ref صالحين من أي ترد لاحقاً.
bool ensureHttpBridgeAttached(JNIEnv* env) {
    jclass local = env->FindClass("com/dlof/rinlang/RinHttpBridge");
    if (local == nullptr) {
        env->ExceptionClear();
        return false;
    }
    g_httpBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (g_httpBridgeClass == nullptr) return false;

    g_httpBridgeRequestMethod = env->GetStaticMethodID(
        g_httpBridgeClass, "request",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;");
    if (g_httpBridgeRequestMethod == nullptr) {
        env->ExceptionClear();
        env->DeleteGlobalRef(g_httpBridgeClass);
        g_httpBridgeClass = nullptr;
        return false;
    }
    return true;
}

// يُعيد JNIEnv* صالحاً للترد الحالي، مُرفِقاً هذا الترد بـ JavaVM أولاً إن لم يكن مُرفَقاً بعد
// (طلبات httpGet/apiCall... قد تُنفَّذ من ترد خلفي مثل worker الخاص بـ LoomPreviewManager أو
// RinJobScheduler، وليس بالضرورة الترد الذي استدعى JNI_OnLoad). [didAttach] يُعاد true إن قمنا نحن
// بالإرفاق، حتى يُفصَل (Detach) الترد بعد الاستدعاء ولا يبقى مُرفَقاً بلا داعٍ.
JNIEnv* attachEnv(bool* didAttach) {
    *didAttach = false;
    if (g_javaVm == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    jint status = g_javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) return env;
    if (status == JNI_EDETACHED) {
        if (g_javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        *didAttach = true;
        return env;
    }
    return nullptr; // JNI_EVERSION أو خطأ آخر غير قابل للتعافي
}

std::string jstringToStd(JNIEnv* env, jstring s) {
    if (s == nullptr) return std::string();
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars ? chars : "");
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return out;
}

jobjectArray buildStringArray(JNIEnv* env, const std::vector<std::string>& items) {
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(items.size()), stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    for (size_t i = 0; i < items.size(); i++) {
        jstring js = env->NewStringUTF(items[i].c_str());
        env->SetObjectArrayElement(arr, static_cast<jsize>(i), js);
        env->DeleteLocalRef(js);
    }
    return arr;
}

// التنفيذ الفعلي المُسجَّل عبر rin::http::setAndroidBridge (انظر rin_http.h): يبني وسائط JNI،
// يستدعي RinHttpBridge.request(...) الحقيقية في Kotlin (java.net.HttpURLConnection حقيقي)،
// ويحوّل ناتجها (String[4]: ok/status/body/error) إلى rin::http::HttpResult.
rin::http::HttpResult callKotlinHttpBridge(const std::string& method, const std::string& url,
                                            const rin::http::HeaderList& headers, const std::string& body,
                                            int timeoutMs) {
    rin::http::HttpResult result;

    if (g_httpBridgeClass == nullptr || g_httpBridgeRequestMethod == nullptr) {
        result.ok = false;
        result.error = "جسر HTTP الخاص بأندرويد غير مُهيَّأ (تعذّر إيجاد RinHttpBridge.kt عند تحميل المكتبة)";
        return result;
    }

    bool didAttach = false;
    JNIEnv* env = attachEnv(&didAttach);
    if (env == nullptr) {
        result.ok = false;
        result.error = "تعذّر الوصول إلى JNIEnv لتنفيذ طلب HTTP حقيقي من هذا الترد";
        return result;
    }

    std::vector<std::string> keys;
    std::vector<std::string> values;
    keys.reserve(headers.size());
    values.reserve(headers.size());
    for (auto& h : headers) { keys.push_back(h.first); values.push_back(h.second); }

    jstring jMethod = env->NewStringUTF(method.c_str());
    jstring jUrl = env->NewStringUTF(url.c_str());
    jobjectArray jKeys = buildStringArray(env, keys);
    jobjectArray jValues = buildStringArray(env, values);
    jstring jBody = env->NewStringUTF(body.c_str());

    auto jResult = static_cast<jobjectArray>(env->CallStaticObjectMethod(
        g_httpBridgeClass, g_httpBridgeRequestMethod, jMethod, jUrl, jKeys, jValues, jBody, (jint)timeoutMs));

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        result.ok = false;
        result.error = "استثناء غير متوقَّع في RinHttpBridge.request (انظر logcat)";
    } else if (jResult == nullptr || env->GetArrayLength(jResult) < 4) {
        result.ok = false;
        result.error = "رد غير صالح من RinHttpBridge.request";
    } else {
        auto jOk = (jstring)env->GetObjectArrayElement(jResult, 0);
        auto jStatus = (jstring)env->GetObjectArrayElement(jResult, 1);
        auto jRespBody = (jstring)env->GetObjectArrayElement(jResult, 2);
        auto jError = (jstring)env->GetObjectArrayElement(jResult, 3);

        std::string okStr = jstringToStd(env, jOk);
        std::string statusStr = jstringToStd(env, jStatus);
        result.body = jstringToStd(env, jRespBody);
        result.error = jstringToStd(env, jError);
        result.ok = (okStr == "1");
        try { result.status = std::stol(statusStr); } catch (...) { result.status = 0; }

        env->DeleteLocalRef(jOk);
        env->DeleteLocalRef(jStatus);
        env->DeleteLocalRef(jRespBody);
        env->DeleteLocalRef(jError);
    }

    if (jResult) env->DeleteLocalRef(jResult);
    env->DeleteLocalRef(jMethod);
    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(jKeys);
    env->DeleteLocalRef(jValues);
    env->DeleteLocalRef(jBody);

    if (didAttach) g_javaVm->DetachCurrentThread();
    return result;
}

} // namespace (anonymous)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    g_javaVm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_VERSION_1_6; // نادر جداً؛ لن يُسجَّل جسر HTTP لكن باقي المحرّك يعمل طبيعياً
    }
    if (ensureHttpBridgeAttached(env)) {
        rin::http::setAndroidBridge(callKotlinHttpBridge);
    }
    // else: RinHttpBridge.kt غير موجود بعد في هذه الحزمة/هذا البناء — httpGet/apiCall... ستُعيد
    // خطأً واضحاً بدل الانهيار (انظر رسالة "جسر HTTP ... غير مُهيَّأ بعد" في rin_http.cpp).
    return JNI_VERSION_1_6;
}
