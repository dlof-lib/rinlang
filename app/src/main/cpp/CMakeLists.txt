cmake_minimum_required(VERSION 3.22.1)
project("rinengine")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
        rinengine
        SHARED
        jni_bridge.cpp
        # Native Rin Code Editor — C++17 engine + JNI bridge; uses the real Rin Lexer for highlighting.
        editor/rin_editor_engine.cpp
        editor/rin_editor_jni.cpp
        rin_lexer.cpp
        rin_parser.cpp
        rin_make.cpp
        rin_interpreter.cpp
        # loader_ui/: Library Loader UI — اختياري تماماً وغير مفعَّل افتراضياً من rin_interpreter.cpp
        # (انظر setImportUISink/setImportUIMode في rin_interpreter.h)، مستقلة كلياً عن باقي المحرّك.
        loader_ui/library_loader_ui.cpp
        # diagnostics/: نظام Compiler Diagnostics الموحّد (E-codes/W-codes، rendering، suggestion
        # engine) المستخدَم من rin_lexer.cpp/rin_parser.cpp/rin_interpreter.cpp أعلاه — انظر
        # docs/ERROR_SYSTEM.md لتوثيق كامل.
        diagnostics/diagnostic.cpp
        diagnostics/source_manager.cpp
        diagnostics/diagnostic_engine.cpp
        diagnostics/diagnostic_renderer.cpp
        # rin_http.cpp: عميل HTTP حقيقي (natives httpGet/apiCall/...، انظر registerNatives() في
        # rin_interpreter.cpp). على أندرويد تنفيذها الفعلي داخل هذا الملف يمرّ بالكامل عبر
        # rin::http::setAndroidBridge (يُسجَّله jni_bridge.cpp من JNI_OnLoad) الذي يستدعي فعلياً
        # RinHttpBridge.kt عبر JNI؛ لا حاجة لأي مكتبة TLS/curl إضافية هنا على أندرويد تحديداً.
        # rin_json.h مستخدَم داخل rin_interpreter.cpp مباشرة (header-only) فلا يحتاج سطراً هنا.
        rin_http.cpp
        # rin_c_api.cpp + loom/: محرّك العرض Loomtime (rendering engine) — إضافي بحت فوق
        # مفسّر اللغة أعلاه؛ يحوّل جذور @view.<Kind>=name إلى Fabric (شجرة عرض) مُخطَّطة
        # ومرسومة عبر Loom/Dye، ويُعرَّض إلى Kotlin عبر jni_bridge.cpp -> RinEngine.kt.
        rin_c_api.cpp
        loom/rin_loom_c_api.cpp
        # clc/: مكتبة CLC (Rin Compact Library Container، .rcl) — مستقلة تماماً عن باقي المحرّك،
        # مربوطة فقط عبر natives خام في rin_interpreter.cpp (container.open/close، library.import/
        # export؛ انظر registerNatives()/invokeCallee() هناك وdocs/RIN_INTEGRATION.md في مستودع
        # rin-clc الأصلي لتفاصيل صيغة .rcl والتصميم).
        clc/clc_container.cpp
        clc/clc_compress.cpp
        clc/clc_security.cpp
        clc/clc_rin_opt.cpp
        clc/clc_zip_import.cpp
        clc/sha256.cpp
        # المحرر الأصلي هنا C++17 ومربوط عبر JNI، ويستخدم rin::Lexer الحقيقي من نفس مصدر اللغة
        # لتلوين Rin وإكمالها، بحيث لا توجد نسخة ثانية من قواعد اللغة داخل واجهة Android.
)

# rinc: مترجم Rin الأصلي (native compiler) — هدف مستقل تماماً عن مكتبة rinengine
# أعلاه. لا يُستخدَم من التطبيق نفسه (يستدعي مترجم C عبر system() على جهاز
# البناء)، لذا لا يُربط بـ rinengine ولا يُحزَم داخل APK؛ يُبنى فقط كأداة سطر
# أوامر مستقلة (مفيدة عند البناء على جهاز تطوير عادي: يُنتج ./rinc قابلاً
# لتحويل ملفات .rin إلى ملفات تنفيذية أصلية).
add_executable(
        rinc
        rinc.cpp
)

# rincheck: أداة `rin check <file> [--format=plain|short|json|lsp]` — تحليل الملف فقط (lex+parse،
# بدون تنفيذ) مع طباعة كل Diagnostics بالتنسيق المطلوب. هدف مستقل عن rinengine/rinc، انظر
# tools/rin_check.cpp و docs/ERROR_SYSTEM.md (القسم 26/27).
add_executable(
        rincheck
        ../../../../tools/rin_check.cpp
        rin_lexer.cpp
        rin_parser.cpp
        rin_make.cpp
        rin_interpreter.cpp
        loader_ui/library_loader_ui.cpp
        rin_http.cpp
        diagnostics/diagnostic.cpp
        diagnostics/source_manager.cpp
        diagnostics/diagnostic_engine.cpp
        diagnostics/diagnostic_renderer.cpp
        # rin_interpreter.cpp أعلاه يتضمّن clc/clc_container.h (natives library.import/export)،
        # فيحتاج rincheck نفس مصادر clc/ لتُبنى/تُربَط، رغم أن rincheck لا يُنفِّذ فعلياً (lex+parse فقط).
        clc/clc_container.cpp
        clc/clc_compress.cpp
        clc/clc_security.cpp
        clc/clc_rin_opt.cpp
        clc/clc_zip_import.cpp
        clc/sha256.cpp
)

# rincheck sources live under tools/, while Rin headers (rin_lexer.h,
# rin_parser.h, etc.) live under this CMake directory.  Clang does not
# automatically search the target source directories for quoted includes,
# so explicitly expose the Rin C++ source root (and diagnostics headers).
target_include_directories(
        rincheck
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics
)

# loomc: أداة سطر أوامر مستقلة لمحرّك العرض Loomtime — نفس فلسفة rinc أعلاه تماماً:
# هدف مستقل، لا يُربط بـ rinengine ولا يُحزَم داخل APK، للاختبار على جهاز تطوير عادي فقط.
#
# ملاحظة: loom/loomc.cpp يتضمّن rin_loom_pipeline.h، وهذا الأخير يشغّل rin::Interpreter حقيقياً
# (runColdPipelineWithRuntime/runColdPipeline كلاهما ينشئ/يشغّل rin::Interpreter فعلياً، انظر
# تعليقاته الخاصة) وليس مجرّد lex+parse مثل rincheck، لذا يحتاج نفس مصادر وقت التشغيل التي
# يحتاجها rincheck أدناه (rin_interpreter.cpp وكل ما يعتمد عليه) وإلا فشل الربط (linker) بأخطاء
# "undefined symbol" لـ rin::Interpreter::Interpreter()/run() و rin::Value::toDisplayString().
add_executable(
        loomc
        loom/loomc.cpp
        rin_lexer.cpp
        rin_parser.cpp
        rin_make.cpp
        rin_interpreter.cpp
        loader_ui/library_loader_ui.cpp
        rin_http.cpp
        diagnostics/diagnostic.cpp
        diagnostics/source_manager.cpp
        diagnostics/diagnostic_engine.cpp
        diagnostics/diagnostic_renderer.cpp
        clc/clc_container.cpp
        clc/clc_compress.cpp
        clc/clc_security.cpp
        clc/clc_rin_opt.cpp
        clc/clc_zip_import.cpp
        clc/sha256.cpp
)

find_library(log-lib log)
# libz.so + zlib.h جزء أصيل من NDK sysroot على كل ABI (لا حاجة لتحميل/توريد أي مصدر خارجي) —
# يُستخدَم من natives zlibDeflateRaw/zlibInflateRaw في rin_interpreter.cpp لضغط ZIP الحقيقي.
find_library(z-lib z)

target_link_libraries(
        rinengine
        ${log-lib}
        ${z-lib}
)

target_link_libraries(
        rincheck
        ${z-lib}
)

target_link_libraries(
        loomc
        ${z-lib}
)
