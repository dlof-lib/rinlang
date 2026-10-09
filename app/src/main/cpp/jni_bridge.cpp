// jni_bridge.cpp
// Exposes the Rin C++ engine to Kotlin through JNI.
// Kotlin side: RinEngine.kt declares the matching `external fun` signatures.
#include <jni.h>
#define RIN_JNI_UTF_WITH_JNI 1
#include "rin_jni_utf.h" // تحويل UTF-8 <-> jstring آمن (انظر التعليق في الملف)
#include "rin_version.h"
#include <string>
#include <cctype>
#include <sstream>
#include <vector>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "rin_artifact.h"
#include "rin_http.h"
#include "rin_media.h" // rin::media::setBridge (make.video/audio/image/ocr على أندرويد)
#include "diagnostics/diagnostic_renderer.h"
#include "indsin/rin_indsin_c_api.h"

// runSourceNative(source, baseDir) -> baseDir هو جذر حقيقي على القرص (عادة filesDir الخاص بالتطبيق
// على أندرويد) تُبنى فوقه كل عمليات save/file/installation/writeFile/readFile الحقيقية. RinEngine.kt
// يمرّر هذا الباراميتر تلقائياً (فارغ إن لم يُستدعَ RinEngine.init(context) بعد)، لذا لا حاجة لتغيير
// أي كود Kotlin قديم يستدعي RinEngine.runSource(source) بباراميتر واحد.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring baseDirJStr) {
    std::string source = rin_jni::toStd(env, sourceJStr);

    std::string baseDir;
    if (baseDirJStr != nullptr) {
        baseDir = rin_jni::toStd(env, baseDirJStr);
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

    return rin_jni::newJString(env, result);
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
                                        rin::Interpreter::StreamSink sink,
                                        rin::Interpreter::InputProvider inputProvider = nullptr) {
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
        if (inputProvider) {
            interpreter.setInputProvider(std::move(inputProvider));
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
    return rin_jni::toStd(env, s);
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceStructuredNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring baseDirJStr) {
    std::string source = jstringOrEmpty(env, sourceJStr);
    std::string baseDir = jstringOrEmpty(env, baseDirJStr);
    StructuredRunOutcome outcome = runStructuredCore(source, baseDir, nullptr);
    return rin_jni::newJString(env, structuredOutcomeToJson(outcome));
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
            jstring jchunk = rin_jni::newJString(env, chunk);
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
    return rin_jni::newJString(env, structuredOutcomeToJson(outcome));
}

// runSourceInteractiveNative(source, baseDir, listener, inputHandler) -> نفس JSON النتيجة الذي تعيده
// runSourceStructuredStreamingNative تماماً، لكن مع دعم دوال الإدخال في لغة Rin
// (input / inputNumber / confirm / choose): كلما احتاج البرنامج إدخالاً يستدعي المحرك
// inputHandler.onInput(prompt) *بشكل متزامن على نفس الترد* (نفس عقد onChunk: لا AttachCurrentThread
// ولا GlobalRef)، فتحجب الدالة ترد التشغيل حتى يجيب المستخدم (أو يُلغي: تعيد Kotlin null).
// [listener] و[inputHandler] كلاهما اختياري (null => يتصرف كأنه غير موجود).
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_runSourceInteractiveNative(JNIEnv* env, jobject /* this */,
                                                            jstring sourceJStr, jstring baseDirJStr,
                                                            jobject listener, jobject inputHandler) {
    std::string source = jstringOrEmpty(env, sourceJStr);
    std::string baseDir = jstringOrEmpty(env, baseDirJStr);

    jmethodID onChunkMethod = nullptr;
    if (listener != nullptr) {
        jclass listenerClass = env->GetObjectClass(listener);
        onChunkMethod = env->GetMethodID(listenerClass, "onChunk", "(ILjava/lang/String;)V");
        env->DeleteLocalRef(listenerClass);
        if (onChunkMethod == nullptr) env->ExceptionClear();
    }
    jmethodID onInputMethod = nullptr;
    if (inputHandler != nullptr) {
        jclass handlerClass = env->GetObjectClass(inputHandler);
        onInputMethod = env->GetMethodID(handlerClass, "onInput", "(Ljava/lang/String;)Ljava/lang/String;");
        env->DeleteLocalRef(handlerClass);
        if (onInputMethod == nullptr) env->ExceptionClear();
    }

    int seq = 0;
    rin::Interpreter::StreamSink sink;
    if (onChunkMethod != nullptr) {
        sink = [env, listener, onChunkMethod, &seq](const std::string& chunk) {
            ++seq;
            jstring jchunk = rin_jni::newJString(env, chunk);
            env->CallVoidMethod(listener, onChunkMethod, static_cast<jint>(seq), jchunk);
            env->DeleteLocalRef(jchunk);
            if (env->ExceptionCheck()) env->ExceptionClear();
        };
    }

    rin::Interpreter::InputProvider provider;
    if (onInputMethod != nullptr) {
        provider = [env, inputHandler, onInputMethod](const std::string& prompt, std::string& answer) -> bool {
            jstring jprompt = rin_jni::newJString(env, prompt);
            jobject res = env->CallObjectMethod(inputHandler, onInputMethod, jprompt);
            env->DeleteLocalRef(jprompt);
            if (env->ExceptionCheck()) {  // استثناء داخل المعالج => عامله كإلغاء (لا تُسقط المحرك)
                env->ExceptionClear();
                return false;
            }
            if (res == nullptr) return false;  // null = ألغى المستخدم الإدخال
            answer = jstringOrEmpty(env, static_cast<jstring>(res));
            env->DeleteLocalRef(res);
            return true;
        };
    }

    StructuredRunOutcome outcome = runStructuredCore(source, baseDir, std::move(sink), std::move(provider));
    return rin_jni::newJString(env, structuredOutcomeToJson(outcome));
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
        jstring jjson = rin_jni::newJString(env, flowEventToJson(e));
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
    return rin_jni::newJString(env, resultJson);
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
        return rin_jni::newJString(env, flowInternalErrorJson(
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
        return rin_jni::newJString(env, flowInternalErrorJson(
            "Session " + previousSessionId + " never executed a |> pipeline; nothing to replay.", 0).c_str());
    }
    return rin_jni::newJString(env, flowRunResultToJson(*result));
}


extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_engineVersion(JNIEnv* env, jobject /* this */) {
    static const std::string kVersionString =
        std::string("Rin Engine ") + RIN_VERSION_STRING +
        " (C++17) — save/file/installation حقيقية على القرص، RinFlow حقيقي";
    return rin_jni::newJString(env, kVersionString);
}



// renderViewNative(source, rootWidth) -> Indsintime: يحلّل @view.<Kind>=name، يبني الـ Fabric،
// يُخطِّطه (Indsin) عند العرض rootWidth (بالبكسل)، ويُعيد تفريغ JSON كامل (kind/name/سطر المصدر/
// هندسة/سمات مُحلَّلة، تكرارياً) يستهلكه جانب Kotlin/Canvas لرسم الواجهة فعلياً. عند فشل التحليل
// يُعاد JSON بالشكل {"error": "...", "line": N} بدل رمي استثناء عبر حدود JNI.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_renderViewNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jint rootWidth) {
    std::string source = rin_jni::toStd(env, sourceJStr);

    char* json = rin_indsin_render_json(source.c_str(), (int)rootWidth);
    jstring result = rin_jni::newJString(env, json ? json : "{\"error\":\"null result\",\"line\":0}");
    rin_free_string(json);
    return result;
}

// renderContainerViewNative(source, containerName, rootWidth) -> نفس renderViewNative أعلاه، لكن
// يبني الـ Fabric من @view المُعرَّف داخل الحاوية containerName بعينها (وليس جذر البرنامج العلوي)،
// وهو الوجه الجديد الذي يجعل Indsintime مربوطة فعلياً بـ container: كل @container يحمل @view/warp/
// @theme خاصة به يصبح شاشة/عنصر واجهة مستقلاً قابلاً للعرض باسمه، مع warp/theme الخاصين بتلك
// الحاوية فقط. نفس شكل JSON الناتج (أو {"error":"...", "line":N} عند الفشل).
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_renderContainerViewNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring containerNameJStr, jint rootWidth) {
    std::string source = rin_jni::toStd(env, sourceJStr);
    std::string containerName = rin_jni::toStd(env, containerNameJStr);

    char* json = rin_indsin_render_container_json(source.c_str(), containerName.c_str(), (int)rootWidth);
    jstring result = rin_jni::newJString(env, json ? json : "{\"error\":\"null result\",\"line\":0}");
    rin_free_string(json);
    return result;
}

// ---- Indsintime session (Needle): a persistent Fabric+Warp session so a live-preview tap can
// actually run its onTap handler (real fun/while loop or a built-in Warp op) and see the result,
// instead of renderViewNative's stateless one-shot render. The native session pointer is boxed as
// a jlong handle on the Kotlin side (see RinEngine.kt's IndsinSession wrapper) -- standard JNI
// pattern for opaque native resources that must outlive a single call.

extern "C" JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionCreateNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jint rootWidth) {
    std::string source = rin_jni::toStd(env, sourceJStr);
    void* session = rin_indsin_session_create(source.c_str(), (int)rootWidth);
    return reinterpret_cast<jlong>(session);
}

// indsinSessionCreateForContainerNative(source, containerName, rootWidth) -> نفس الجلسة أعلاه، لكن
// حالتها (Fabric+Warp) مبنية من @view/warp/@theme داخل الحاوية containerName بعينها، فيمكن لأي
// tap لاحق (indsinSessionTapNative) أن يعمل بشكل طبيعي على warp/onTap الخاصين بتلك الحاوية فقط.
extern "C" JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionCreateForContainerNative(JNIEnv* env, jobject /* this */, jstring sourceJStr, jstring containerNameJStr, jint rootWidth) {
    std::string source = rin_jni::toStd(env, sourceJStr);
    std::string containerName = rin_jni::toStd(env, containerNameJStr);
    void* session = rin_indsin_session_create_for_container(source.c_str(), containerName.c_str(), (int)rootWidth);
    return reinterpret_cast<jlong>(session);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionRenderJsonNative(JNIEnv* env, jobject /* this */, jlong handle) {
    char* json = rin_indsin_session_render_json(reinterpret_cast<void*>(handle));
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionTapNative(JNIEnv* env, jobject /* this */, jlong handle, jdouble x, jdouble y) {
    char* json = rin_indsin_session_tap(reinterpret_cast<void*>(handle), (double)x, (double)y);
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

// Events & Effects: long-press / double-tap / hover / tick — same envelope-return + free()
// pattern as indsinSessionTapNative above, just against rin_indsin_session_long_press/_double_tap/
// _hover/_tick instead of rin_indsin_session_tap. See those functions' own doc comments in
// rin_indsin_c_api.h for exactly what each returns.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionLongPressNative(JNIEnv* env, jobject /* this */, jlong handle, jdouble x, jdouble y) {
    char* json = rin_indsin_session_long_press(reinterpret_cast<void*>(handle), (double)x, (double)y);
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionDoubleTapNative(JNIEnv* env, jobject /* this */, jlong handle, jdouble x, jdouble y) {
    char* json = rin_indsin_session_double_tap(reinterpret_cast<void*>(handle), (double)x, (double)y);
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionHoverNative(JNIEnv* env, jobject /* this */, jlong handle, jdouble x, jdouble y, jboolean entering) {
    char* json = rin_indsin_session_hover(reinterpret_cast<void*>(handle), (double)x, (double)y, entering ? 1 : 0);
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}


// ---- Indsin Media (rin_indsin_media.h): نتائج المنتقي/الرفع القادمة من المضيف ----
static std::string mediaJStr(JNIEnv* env, jstring s) {
    return rin_jni::toStd(env, s);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionMediaPickedNative(JNIEnv* env, jobject /* this */, jlong handle, jstring cell,
                                                               jstring itemsJson, jstring kind, jboolean multiple,
                                                               jdouble maxMb, jboolean append) {
    std::string c = mediaJStr(env, cell), items = mediaJStr(env, itemsJson), k = mediaJStr(env, kind);
    char* json = rin_indsin_session_media_picked(reinterpret_cast<void*>(handle), c.c_str(), items.c_str(), k.c_str(),
                                                 multiple ? 1 : 0, (double)maxMb, append ? 1 : 0);
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionMediaProgressNative(JNIEnv* env, jobject /* this */, jlong handle, jstring cell,
                                                                 jstring status, jdouble progress, jstring detail) {
    std::string c = mediaJStr(env, cell), st = mediaJStr(env, status), d = mediaJStr(env, detail);
    char* json = rin_indsin_session_media_progress(reinterpret_cast<void*>(handle), c.c_str(), st.c_str(), (double)progress, d.c_str());
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionTickNative(JNIEnv* env, jobject /* this */, jlong handle) {
    char* json = rin_indsin_session_tick(reinterpret_cast<void*>(handle));
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionUpdateSourceNative(JNIEnv* env, jobject /* this */, jlong handle, jstring newSourceJStr) {
    std::string source = rin_jni::toStd(env, newSourceJStr);
    char* json = rin_indsin_session_update_source(reinterpret_cast<void*>(handle), source.c_str());
    jstring result = rin_jni::newJString(env, json ? json : "{\"ok\":false,\"error\":\"null result\"}");
    rin_free_string(json);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionFreeNative(JNIEnv* /* env */, jobject /* this */, jlong handle) {
    rin_indsin_session_free(reinterpret_cast<void*>(handle));
}

// indsinSessionSetViewportNative(handle, viewportHeight) -> Overlay Engine (rin_indsin_overlay.h):
// Dialog centering / Tooltip clamping needs the *real* on-screen viewport height (not the 844
// native-side default) to re-home overlays correctly against the actual device -- see
// rin_indsin_session_set_viewport's own doc comment in rin_indsin_c_api.h/.cpp. Kotlin should call
// this once it knows the preview surface's real measured height (dp), same moment it already
// measures the surface's width for rootWidth (see IndsinPreviewActivity.fitDeviceWidth()).
extern "C" JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionSetViewportNative(JNIEnv* /* env */, jobject /* this */, jlong handle, jint viewportHeight) {
    rin_indsin_session_set_viewport(reinterpret_cast<void*>(handle), (int)viewportHeight);
}

// ---- Design System v2 / Audit / Introspection (rin_indsin_system.h, _audit.h, _query.h) ----------
// ثماني دوال C جديدة (انظر docs/indsin_expansion.md §8) تُرجع كلها JSON/نصاً مُخصَّصاً بـ malloc.
// نمط واحد موحَّد: نقرأ النص المُمرَّر بأمان (null -> "")، نستدعي الدالة C، ننسخ الناتج إلى jstring
// ثم نحرّره دائماً بـ rin_free_string -- حتى عند فشل NewStringUTF -- فلا يتسرّب شيء عبر JNI.
// ملاحظة: NewStringUTF يتوقع Modified-UTF8؛ ناتج المحرّك JSON بـ ASCII/UTF-8 عادي (الحروف غير
// ASCII تأتي مُهرَّبة أو UTF-8 صالحاً بلا رموز إضافية، ونفس الافتراض تعتمده كل دوال هذا الملف).

static std::string indsinJStringToStd(JNIEnv* env, jstring s) {
    if (!s) return std::string();
    return rin_jni::toStd(env, s);
}

// يحوّل نتيجة C المملوكة (malloc) إلى jstring ويحرّرها. [fallback] JSON خطأ آمن عند null.
static jstring indsinTakeResult(JNIEnv* env, char* owned, const char* fallback) {
    jstring result = rin_jni::newJString(env, owned ? owned : fallback);
    if (owned) rin_free_string(owned);
    return result;
}

static const char* const kIndsinNullJson = "{\"error\":\"null result\"}";

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionAuditJsonNative(JNIEnv* env, jobject /* this */, jlong handle) {
    return indsinTakeResult(env, rin_indsin_session_audit_json(reinterpret_cast<void*>(handle)), kIndsinNullJson);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionStatsJsonNative(JNIEnv* env, jobject /* this */, jlong handle) {
    return indsinTakeResult(env, rin_indsin_session_stats_json(reinterpret_cast<void*>(handle)), kIndsinNullJson);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinSessionOutlineNative(JNIEnv* env, jobject /* this */, jlong handle) {
    // المخطط نص عادي لا JSON؛ الـ fallback الفارغ يعني "لا شيء لعرضه".
    return indsinTakeResult(env, rin_indsin_session_outline(reinterpret_cast<void*>(handle)), "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinTokensJsonNative(JNIEnv* env, jobject /* this */) {
    return indsinTakeResult(env, rin_indsin_tokens_json(), kIndsinNullJson);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinCatalogJsonNative(JNIEnv* env, jobject /* this */) {
    return indsinTakeResult(env, rin_indsin_catalog_json(), "[]");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinPaletteJsonNative(JNIEnv* env, jobject /* this */, jstring seedJStr) {
    std::string seed = indsinJStringToStd(env, seedJStr);
    return indsinTakeResult(env, rin_indsin_palette_json(seed.c_str()), kIndsinNullJson);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinThemeFromSeedJsonNative(JNIEnv* env, jobject /* this */, jstring seedJStr, jboolean dark) {
    std::string seed = indsinJStringToStd(env, seedJStr);
    return indsinTakeResult(env, rin_indsin_theme_from_seed_json(seed.c_str(), dark ? 1 : 0), kIndsinNullJson);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_indsinValidateThemeJsonNative(JNIEnv* env, jobject /* this */, jstring nameJStr) {
    std::string name = indsinJStringToStd(env, nameJStr);
    return indsinTakeResult(env, rin_indsin_validate_theme_json(name.c_str()), kIndsinNullJson);
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
jmethodID g_httpBridgeRequestBinaryGetMethod = nullptr; // MethodID لـ RinHttpBridge.requestBinaryGet(...) (fetchImage/fetchIcon)
JNIEnv* attachEnv(bool* didAttach);
std::string jstringToStd(JNIEnv* env, jstring s);
jobjectArray buildStringArray(JNIEnv* env, const std::vector<std::string>& items);
jclass g_artifactBridgeClass = nullptr;
jmethodID g_artifactQrMethod = nullptr;
jclass g_mediaBridgeClass = nullptr;           // com.dlof.rinlang.RinMediaBridge (make.video/audio/image/ocr)
jmethodID g_mediaBridgeCallMethod = nullptr;

bool ensureArtifactBridgeAttached(JNIEnv* env) {
    jclass local = env->FindClass("com/dlof/rinlang/RinArtifactBridge");
    if (local == nullptr) { env->ExceptionClear(); return false; }
    g_artifactBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!g_artifactBridgeClass) return false;
    g_artifactQrMethod = env->GetStaticMethodID(g_artifactBridgeClass, "generateQrSvg", "(Ljava/lang/String;II)Ljava/lang/String;");
    if (!g_artifactQrMethod) { env->ExceptionClear(); env->DeleteGlobalRef(g_artifactBridgeClass); g_artifactBridgeClass = nullptr; return false; }
    return true;
}

std::string callKotlinQrBridge(const std::string& data, int size, int margin) {
    if (!g_javaVm || !g_artifactBridgeClass || !g_artifactQrMethod) throw std::runtime_error("Rin Android QR bridge is not initialized");
    bool didAttach = false;
    JNIEnv* env = attachEnv(&didAttach);
    if (!env) throw std::runtime_error("could not attach JNI environment for QR generation");
    jstring jdata = rin_jni::newJString(env, data);
    jobject obj = env->CallStaticObjectMethod(g_artifactBridgeClass, g_artifactQrMethod, jdata, (jint)size, (jint)margin);
    env->DeleteLocalRef(jdata);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); if (didAttach) g_javaVm->DetachCurrentThread(); throw std::runtime_error("Rin QR encoder failed"); }
    std::string out = jstringToStd(env, (jstring)obj);
    if (obj) env->DeleteLocalRef(obj);
    if (didAttach) g_javaVm->DetachCurrentThread();
    return out;
}

bool ensureMediaBridgeAttached(JNIEnv* env) {
    jclass local = env->FindClass("com/dlof/rinlang/RinMediaBridge");
    if (local == nullptr) { env->ExceptionClear(); return false; }
    g_mediaBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!g_mediaBridgeClass) return false;
    g_mediaBridgeCallMethod = env->GetStaticMethodID(g_mediaBridgeClass, "call",
        "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)[Ljava/lang/String;");
    if (!g_mediaBridgeCallMethod) { env->ExceptionClear(); env->DeleteGlobalRef(g_mediaBridgeClass); g_mediaBridgeClass = nullptr; return false; }
    return true;
}

// rin::media::setBridge: RinMediaBridge.call(op, keys, values) -> String[] [handled, ok, error, k1, v1, ...]
rin::media::BridgeResult callKotlinMediaBridge(const std::string& op, const rin::media::KV& args) {
    rin::media::BridgeResult r;
    if (!g_javaVm || !g_mediaBridgeClass || !g_mediaBridgeCallMethod) return r;   // handled=false
    bool didAttach = false;
    JNIEnv* env = attachEnv(&didAttach);
    if (!env) { r.handled = true; r.error = "تعذّر الوصول إلى JNIEnv لجسر الوسائط"; return r; }
    std::vector<std::string> keys, vals;
    for (auto& kv : args) { keys.push_back(kv.first); vals.push_back(kv.second); }
    jstring jOp = rin_jni::newJString(env, op);
    jobjectArray jKeys = buildStringArray(env, keys);
    jobjectArray jVals = buildStringArray(env, vals);
    auto jRes = static_cast<jobjectArray>(env->CallStaticObjectMethod(g_mediaBridgeClass, g_mediaBridgeCallMethod, jOp, jKeys, jVals));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe(); env->ExceptionClear();
        r.handled = true; r.error = "استثناء غير متوقَّع في RinMediaBridge.call (انظر logcat)";
    } else if (jRes == nullptr || env->GetArrayLength(jRes) < 3) {
        r.handled = true; r.error = "رد غير صالح من RinMediaBridge.call";
    } else {
        jsize n = env->GetArrayLength(jRes);
        auto at = [&](jsize i) { auto js = (jstring)env->GetObjectArrayElement(jRes, i); std::string s = jstringToStd(env, js); if (js) env->DeleteLocalRef(js); return s; };
        r.handled = (at(0) == "1");
        r.ok = (at(1) == "1");
        r.error = at(2);
        for (jsize i = 3; i + 1 < n; i += 2) r.fields.push_back({at(i), at(i + 1)});
    }
    if (jRes) env->DeleteLocalRef(jRes);
    env->DeleteLocalRef(jOp); env->DeleteLocalRef(jKeys); env->DeleteLocalRef(jVals);
    if (didAttach) g_javaVm->DetachCurrentThread();
    return r;
}

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

    // requestBinaryGet(...) اختياري (fetchImage/fetchIcon فقط): غيابه لا يُسقط تسجيل الجسر
    // النصي العادي أعلاه، فقط تبقى fetchImage/fetchIcon معطَّلتين بخطأ واضح (انظر performBinaryGet
    // في rin_http.cpp) إن كان RinHttpBridge.kt المرفَق أقدم من هذه الإضافة.
    g_httpBridgeRequestBinaryGetMethod = env->GetStaticMethodID(
        g_httpBridgeClass, "requestBinaryGet", "(Ljava/lang/String;I)[Ljava/lang/Object;");
    if (g_httpBridgeRequestBinaryGetMethod == nullptr) env->ExceptionClear();
    return true;
}

// يُعيد JNIEnv* صالحاً للترد الحالي، مُرفِقاً هذا الترد بـ JavaVM أولاً إن لم يكن مُرفَقاً بعد
// (طلبات httpGet/apiCall... قد تُنفَّذ من ترد خلفي مثل worker الخاص بـ IndsinPreviewManager أو
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
    return rin_jni::toStd(env, s);
}

jobjectArray buildStringArray(JNIEnv* env, const std::vector<std::string>& items) {
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(items.size()), stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    for (size_t i = 0; i < items.size(); i++) {
        jstring js = rin_jni::newJString(env, items[i]);
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

    jstring jMethod = rin_jni::newJString(env, method);
    jstring jUrl = rin_jni::newJString(env, url);
    jobjectArray jKeys = buildStringArray(env, keys);
    jobjectArray jValues = buildStringArray(env, values);
    jstring jBody = rin_jni::newJString(env, body);

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

// نظير callKotlinHttpBridge أعلاه، لكن يستدعي RinHttpBridge.requestBinaryGet(...) ويقرأ العنصر
// الرابع كـ jbyteArray خام (GetByteArrayRegion) بدل jstring — فتبقى بايتات الصورة/الأيقونة كما
// وصلت فعلياً من الخادوم بلا أي تحويل نصي وسيط يُفسدها (انظر شرح كامل في rin_http.h).
rin::http::HttpResult callKotlinHttpBridgeBinaryGet(const std::string& url, int timeoutMs) {
    rin::http::HttpResult result;

    if (g_httpBridgeClass == nullptr || g_httpBridgeRequestBinaryGetMethod == nullptr) {
        result.ok = false;
        result.error = "جسر تنزيل الصور الثنائي غير مُهيَّأ (RinHttpBridge.kt المرفَق لا يحتوي requestBinaryGet — أعد بناء التطبيق)";
        return result;
    }

    bool didAttach = false;
    JNIEnv* env = attachEnv(&didAttach);
    if (env == nullptr) {
        result.ok = false;
        result.error = "تعذّر الوصول إلى JNIEnv لتنفيذ تنزيل صورة حقيقي من هذا الترد";
        return result;
    }

    jstring jUrl = rin_jni::newJString(env, url);
    auto jResult = static_cast<jobjectArray>(env->CallStaticObjectMethod(
        g_httpBridgeClass, g_httpBridgeRequestBinaryGetMethod, jUrl, (jint)timeoutMs));

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        result.ok = false;
        result.error = "استثناء غير متوقَّع في RinHttpBridge.requestBinaryGet (انظر logcat)";
    } else if (jResult == nullptr || env->GetArrayLength(jResult) < 4) {
        result.ok = false;
        result.error = "رد غير صالح من RinHttpBridge.requestBinaryGet";
    } else {
        auto jOk = (jstring)env->GetObjectArrayElement(jResult, 0);
        auto jStatus = (jstring)env->GetObjectArrayElement(jResult, 1);
        auto jError = (jstring)env->GetObjectArrayElement(jResult, 2);
        auto jBytes = (jbyteArray)env->GetObjectArrayElement(jResult, 3);

        std::string okStr = jstringToStd(env, jOk);
        std::string statusStr = jstringToStd(env, jStatus);
        result.error = jstringToStd(env, jError);
        result.ok = (okStr == "1");
        try { result.status = std::stol(statusStr); } catch (...) { result.status = 0; }

        if (jBytes != nullptr) {
            jsize len = env->GetArrayLength(jBytes);
            result.body.resize(static_cast<size_t>(len));
            if (len > 0) {
                env->GetByteArrayRegion(jBytes, 0, len, reinterpret_cast<jbyte*>(&result.body[0]));
            }
            env->DeleteLocalRef(jBytes);
        }

        env->DeleteLocalRef(jOk);
        env->DeleteLocalRef(jStatus);
        env->DeleteLocalRef(jError);
    }

    if (jResult) env->DeleteLocalRef(jResult);
    env->DeleteLocalRef(jUrl);

    if (didAttach) g_javaVm->DetachCurrentThread();
    return result;
}

} // namespace (anonymous)

// ================= جلسة RinHTML الحيّة (HtmlRunActivity) =================
// نفس فكرة web/rinhtml_bridge.cpp (WASM) بالضبط لكن على JNI: جلسة واحدة = برنامج مُحلَّل (AST) +
// مفسِّر حيّ + آخر قيم عامة معروفة. صفحة index.html المعروضة في WebView تستدعي دوال container.rin
// الحقيقية (rin-click="add()") عبر Interpreter::callTopLevelFunction، ثم تقرأ الحالة الجديدة
// (rin-text="count"). لا يُعاد تنفيذ البرنامج من الصفر عند كل نقرة، وكل دلالات اللغة محفوظة.
// غلاف (glue) بحت: لا يضيف أي دلالة جديدة إلى Rin نفسها.
namespace {

struct HtmlSession {
    std::vector<rin::StmtPtr> program;
    std::unique_ptr<rin::Interpreter> interp;
    std::unordered_map<std::string, rin::Value> globals;
    std::string bootOutput;
};

std::string g_htmlLastError;

std::string htmlJsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: if (static_cast<unsigned char>(c) >= 0x20) out += c;
        }
    }
    return out;
}

std::string htmlNumToJson(double n) {
    if (n == static_cast<long long>(n) && n < 1e15 && n > -1e15) {
        return std::to_string(static_cast<long long>(n));
    }
    std::ostringstream o;
    o << n;
    return o.str();
}

// قيم الحالة المنقولة للصفحة: أرقام/نصوص/منطقية/null، وكذلك المصفوفات (→ JSON array) والقواميس
// والكائنات (→ JSON object، مفاتيحها نصوص) بعمق أقصى 8 مستويات (ما بعده يُقطع إلى null) لتفادي الدوران.
// تُستعمل في الصفحة عبر rin-for / rin-text="item.name" / rin-if...
std::string htmlValueToJson(const rin::Value& v, int depth = 0) {
    if (depth > 8) return "null";
    switch (v.type) {
        case rin::Value::Type::NUMBER: return htmlNumToJson(v.number);
        case rin::Value::Type::STRING: return "\"" + htmlJsonEscape(v.str) + "\"";
        case rin::Value::Type::BOOL:   return v.boolean ? "true" : "false";
        case rin::Value::Type::NIL:    return "null";
        case rin::Value::Type::FUNCTION: return "null";
        case rin::Value::Type::ARRAY: {
            std::string o = "[";
            if (v.array) {
                for (size_t i = 0; i < v.array->size(); i++) {
                    if (i) o += ",";
                    o += htmlValueToJson((*v.array)[i], depth + 1);
                }
            }
            return o + "]";
        }
        case rin::Value::Type::MAP: {
            std::string o = "{";
            bool first = true;
            if (v.map) {
                for (auto& kv : *v.map) {
                    if (!first) o += ",";
                    first = false;
                    std::string key = kv.first.type == rin::Value::Type::STRING ? kv.first.str : kv.first.toDisplayString();
                    o += "\"" + htmlJsonEscape(key) + "\":" + htmlValueToJson(kv.second, depth + 1);
                }
            }
            return o + "}";
        }
        case rin::Value::Type::INSTANCE: {
            std::string o = "{";
            if (v.instance) {
                bool first = true;
                for (auto& name : v.instance->fieldOrder) {
                    auto it = v.instance->fields.find(name);
                    if (it == v.instance->fields.end() || it->second.type == rin::Value::Type::FUNCTION) continue;
                    if (!first) o += ",";
                    first = false;
                    o += "\"" + htmlJsonEscape(name) + "\":" + htmlValueToJson(it->second, depth + 1);
                }
            }
            return o + "}";
        }
        default: return "\"" + htmlJsonEscape(v.toDisplayString()) + "\"";
    }
}

std::string htmlGlobalsToJson(const std::unordered_map<std::string, rin::Value>& g) {
    std::ostringstream o;
    o << "{";
    bool first = true;
    for (auto& kv : g) {
        if (kv.second.type == rin::Value::Type::FUNCTION) continue; // الدوال ليست "حالة"
        if (!first) o << ",";
        first = false;
        o << "\"" << htmlJsonEscape(kv.first) << "\":" << htmlValueToJson(kv.second);
    }
    o << "}";
    return o.str();
}

// قارئ JSON مصغّر: أرقام / "نص" / true / false / null / مصفوفات / كائنات (عمق ≤ 8).
struct HtmlJsonReader {
    const std::string& s;
    size_t i = 0;
    explicit HtmlJsonReader(const std::string& src) : s(src) {}
    void skipWs() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; }
    rin::Value parseValue(int depth = 0) {
        skipWs();
        if (i >= s.size() || depth > 8) return rin::Value::nil();
        char c = s[i];
        if (c == '[') {
            auto arr = std::make_shared<rin::ArrayData>(parseArray(depth + 1));
            return rin::Value::makeArray(arr);
        }
        if (c == '{') return parseObject(depth + 1);
        if (c == '"') return parseString();
        if (c == 't' && s.compare(i, 4, "true") == 0) { i += 4; return rin::Value::boolean_(true); }
        if (c == 'f' && s.compare(i, 5, "false") == 0) { i += 5; return rin::Value::boolean_(false); }
        if (c == 'n' && s.compare(i, 4, "null") == 0) { i += 4; return rin::Value::nil(); }
        return parseNumber();
    }
    rin::Value parseString() {
        std::string out;
        i++;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    default: out += e;
                }
            } else out += c;
        }
        if (i < s.size()) i++;
        return rin::Value::string(out);
    }
    rin::Value parseNumber() {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                                 s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) i++;
        if (i == start) { i++; return rin::Value::nil(); }
        try { return rin::Value::num(std::stod(s.substr(start, i - start))); }
        catch (...) { return rin::Value::nil(); }
    }
    rin::Value parseObject(int depth) {
        auto m = std::make_shared<rin::MapData>();
        i++; // {
        skipWs();
        if (i < s.size() && s[i] == '}') { i++; return rin::Value::makeMap(m); }
        while (i < s.size()) {
            skipWs();
            if (i >= s.size() || s[i] != '"') break;
            rin::Value key = parseString();
            skipWs();
            if (i < s.size() && s[i] == ':') i++;
            rin::Value val = parseValue(depth);
            m->emplace_back(key, val);
            skipWs();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; }
            break;
        }
        return rin::Value::makeMap(m);
    }
    std::vector<rin::Value> parseArray(int depth = 0) {
        std::vector<rin::Value> out;
        skipWs();
        if (i >= s.size() || s[i] != '[') return out;
        i++;
        skipWs();
        if (i < s.size() && s[i] == ']') { i++; return out; }
        while (i < s.size()) {
            out.push_back(parseValue(depth));
            skipWs();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; }
            break;
        }
        return out;
    }
};

std::string jstr(JNIEnv* env, jstring js) {
    if (!js) return "";
    return rin_jni::toStd(env, js);
}

} // namespace

// يعيد مؤشر الجلسة (jlong) أو 0 عند الفشل (انظر htmlLastErrorNative).
extern "C" JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinEngine_htmlCreateNative(JNIEnv* env, jobject, jstring sourceJ, jstring baseDirJ) {
    std::string source = jstr(env, sourceJ);
    std::string baseDir = jstr(env, baseDirJ);
    auto sess = std::make_unique<HtmlSession>();
    try {
        rin::Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        sess->program = parser.parse();
        sess->interp = std::make_unique<rin::Interpreter>();
        if (!baseDir.empty()) sess->interp->setBasePath(baseDir);
        sess->bootOutput = sess->interp->run(sess->program);
        if (sess->interp->hadError()) {
            g_htmlLastError = sess->interp->lastErrorMessage().value_or("خطأ غير معروف أثناء التشغيل الأولي");
            return 0;
        }
        sess->globals = sess->interp->exportGlobals();
    } catch (rin::RinError& e) {
        g_htmlLastError = "[Syntax error, line " + std::to_string(e.line) + "]: " + e.message;
        return 0;
    } catch (std::exception& e) {
        g_htmlLastError = e.what();
        return 0;
    } catch (...) {
        g_htmlLastError = "خطأ غير معروف أثناء إنشاء جلسة HTML";
        return 0;
    }
    return reinterpret_cast<jlong>(sess.release());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_htmlLastErrorNative(JNIEnv* env, jobject) {
    return rin_jni::newJString(env, g_htmlLastError);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_htmlBootOutputNative(JNIEnv* env, jobject, jlong handle) {
    auto* s = reinterpret_cast<HtmlSession*>(handle);
    return rin_jni::newJString(env, s ? s->bootOutput.c_str() : "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_htmlGlobalsNative(JNIEnv* env, jobject, jlong handle) {
    auto* s = reinterpret_cast<HtmlSession*>(handle);
    std::string j = s ? htmlGlobalsToJson(s->globals) : "{}";
    return rin_jni::newJString(env, j);
}

// {"ok":true,"globals":{...}} أو {"ok":false,"error":"...","globals":{...}}
extern "C" JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinEngine_htmlCallNative(JNIEnv* env, jobject, jlong handle, jstring fnJ, jstring argsJ) {
    auto* s = reinterpret_cast<HtmlSession*>(handle);
    if (!s) return rin_jni::newJString(env, "{\"ok\":false,\"error\":\"invalid session\",\"globals\":{}}");
    std::string fn = jstr(env, fnJ);
    std::string argsJson = jstr(env, argsJ);
    HtmlJsonReader reader(argsJson);
    std::vector<rin::Value> args = reader.parseArray();
    std::vector<std::string> aliases(args.size(), std::string());
    std::unordered_map<std::string, rin::Value> attempt = s->globals;
    std::string err;
    bool ok = false;
    try {
        ok = s->interp->callTopLevelFunction(s->program, fn, args, aliases, attempt, err);
    } catch (rin::RinError& e) {
        ok = false;
        err = std::to_string(e.line) + ": " + e.message;
    } catch (std::exception& e) {
        ok = false;
        err = e.what();
    }
    if (ok) s->globals = attempt;
    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false");
    if (!ok) o << ",\"error\":\"" << htmlJsonEscape(err.empty() ? ("لا توجد دالة باسم '" + fn + "'") : err) << "\"";
    o << ",\"globals\":" << htmlGlobalsToJson(s->globals) << "}";
    std::string out = o.str();
    return rin_jni::newJString(env, out);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinEngine_htmlSetGlobalNative(JNIEnv* env, jobject, jlong handle, jstring nameJ, jstring valueJ) {
    auto* s = reinterpret_cast<HtmlSession*>(handle);
    if (!s) return;
    std::string name = jstr(env, nameJ);
    std::string valueJson = jstr(env, valueJ);
    if (name.empty()) return;
    HtmlJsonReader reader(valueJson);
    s->globals[name] = reader.parseValue();
}

extern "C" JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinEngine_htmlFreeNative(JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<HtmlSession*>(handle);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    g_javaVm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_VERSION_1_6; // نادر جداً؛ لن يُسجَّل جسر HTTP لكن باقي المحرّك يعمل طبيعياً
    }
    if (ensureHttpBridgeAttached(env)) {
        rin::http::setAndroidBridge(callKotlinHttpBridge);
        if (g_httpBridgeRequestBinaryGetMethod != nullptr) {
            rin::http::setAndroidBinaryGetBridge(callKotlinHttpBridgeBinaryGet);
        }
    }
    if (ensureArtifactBridgeAttached(env)) {
        rin::artifact::setQrBridge(callKotlinQrBridge);
    }
    if (ensureMediaBridgeAttached(env)) {
        rin::media::setBridge(callKotlinMediaBridge);
    }
    // else: RinHttpBridge.kt غير موجود بعد في هذه الحزمة/هذا البناء — httpGet/apiCall... ستُعيد
    // خطأً واضحاً بدل الانهيار (انظر رسالة "جسر HTTP ... غير مُهيَّأ بعد" في rin_http.cpp).
    return JNI_VERSION_1_6;
}
