cmake_minimum_required(VERSION 3.22.1)
project("rinengine")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
        rinengine
        SHARED
        jni_bridge.cpp
        rin_lexer.cpp
        rin_parser.cpp
        rin_interpreter.cpp
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
        rin_interpreter.cpp
        rin_http.cpp
        diagnostics/diagnostic.cpp
        diagnostics/source_manager.cpp
        diagnostics/diagnostic_engine.cpp
        diagnostics/diagnostic_renderer.cpp
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
add_executable(
        loomc
        loom/loomc.cpp
        rin_lexer.cpp
        rin_parser.cpp
        diagnostics/diagnostic.cpp
        diagnostics/source_manager.cpp
        diagnostics/diagnostic_engine.cpp
        diagnostics/diagnostic_renderer.cpp
)

find_library(log-lib log)

target_link_libraries(
        rinengine
        ${log-lib}
)
