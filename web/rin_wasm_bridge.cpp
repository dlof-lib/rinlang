// web/rin_wasm_bridge.cpp
// =============================================================================
// جسر WebAssembly إلى محرّك Rin الرسمي (rin_lexer.cpp + rin_parser.cpp +
// rin_interpreter.cpp — نفس الملفات الثلاثة حرفياً المستخدمة في tools/rin_run.cpp
// وفي تطبيق أندرويد عبر jni_bridge.cpp). هذا الملف لا يعيد تنفيذ أي جزء من
// اللغة؛ هو فقط "غلاف" (glue) يُصدِّر دالة C واحدة يستطيع JavaScript استدعاءها.
//
// التدفق الكامل الذي يحدث داخل المتصفح عند تحميل الصفحة:
//   1) JS يجلب site.rin كنص خام عبر fetch() (لا معالجة، لا تحويل — نفس الملف
//      المصدري بالضبط الذي يُنفَّذ على الخادوم بمترجم g++ الأصلي).
//   2) JS يستدعي rin_run_source(source) هنا.
//   3) هنا نُشغّل Lexer -> Parser -> Interpreter تماماً كما تفعل tools/rin_run.cpp.
//   4) داخل التنفيذ، عبارة Rin: writeFile("dist/index.html", page) تكتب فعلياً
//      إلى نظام ملفات Emscripten الوهمي (MEMFS) داخل ذاكرة WASM.
//   5) JS بعدها يقرأ dist/index.html من MEMFS عبر Module.FS.readFile(...)
//      ويُدرجه في DOM ليعرضه المتصفح — الناتج HTML، لكن المصدر الوحيد هو Rin.
// =============================================================================
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <emscripten.h>
#include <string>

static std::string g_lastResult;

extern "C" {

// يُعيد سلسلة نصية:
//   - في حال النجاح: نص أي print() صدر أثناء التنفيذ (نفس ما يطبعه rin_run على الطرفية).
//   - في حال الفشل: تبدأ بـ "__RIN_ERROR__:<line>:<message>" ليقرأها JS ويعرضها بوضوح.
// الناتج الفعلي (HTML) لا يُعاد عبر هذه الدالة؛ يُقرأ لاحقاً من MEMFS لأن writeFile
// هي نفسها الآلية الرسمية الوحيدة في اللغة لإخراج ملف — لا نضيف مسار مختصر موازياً لها.
EMSCRIPTEN_KEEPALIVE
const char* rin_run_source(const char* source) {
    try {
        rin::Lexer lexer(std::string(source ? source : ""));
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens);
        auto statements = parser.parse();
        rin::Interpreter interp;
        g_lastResult = interp.run(statements);
    } catch (rin::RinError& e) {
        g_lastResult = "__RIN_ERROR__:" + std::to_string(e.line) + ":" + e.message;
    } catch (std::exception& e) {
        g_lastResult = std::string("__RIN_ERROR__:0:") + e.what();
    } catch (...) {
        g_lastResult = "__RIN_ERROR__:0:خطأ غير معروف أثناء تنفيذ Rin";
    }
    return g_lastResult.c_str();
}

} // extern "C"
