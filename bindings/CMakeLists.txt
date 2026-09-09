# bindings/CMakeLists.txt
# يبني محرّك Rin كمكتبة مشتركة عامة (librin.so على لينكس، librin.dylib على
# macOS، rin.dll على ويندوز) لا علاقة لها بأندرويد/NDK إطلاقاً — تُستخدَم من
# أي لغة برمجة أخرى عبر bindings/python أو bindings/node أو مباشرة عبر C ABI.
#
# البناء:
#   cd bindings
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
# الناتج: build/librin.so (أو المكافئ حسب نظام التشغيل).
cmake_minimum_required(VERSION 3.10)
project(rin_engine_shared CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(RIN_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)

# مطلوبة لـ zlibDeflateRaw/zlibInflateRaw في rin_interpreter.cpp (ضغط ZIP حقيقي).
find_package(ZLIB REQUIRED)

add_library(rin SHARED
    ${RIN_CPP_DIR}/rin_lexer.cpp
    ${RIN_CPP_DIR}/rin_parser.cpp
    # rin_make.cpp: validateMakeUnit/validateContainerPolicy — يستدعيهما execute() في
    # rin_interpreter.cpp أدناه لكل @make/@container، فيفشل الربط (undefined reference) بدونه
    # (نفس الخلل الذي كان في cli/linux/CMakeLists.txt وتم إصلاحه هناك).
    ${RIN_CPP_DIR}/rin_make.cpp
    ${RIN_CPP_DIR}/rin_interpreter.cpp
    # loader_ui/: يُضمَّن مباشرة من rin_interpreter.h (loaderui::LoadSession في @import) — ليس
    # اختيارياً على مستوى الربط رغم أنه معطَّل افتراضياً على مستوى السلوك (sink = nullptr).
    ${RIN_CPP_DIR}/loader_ui/library_loader_ui.cpp
    # rin_http.cpp: عميل HTTP حقيقي (natives httpGet/apiCall/...، انظر registerNatives() في
    # rin_interpreter.cpp). خارج أندرويد يُستخدَم مسار libcurl الافتراضي في هذا الملف مباشرة
    # (لا حاجة لـ setAndroidBridge هنا كما في jni_bridge.cpp).
    ${RIN_CPP_DIR}/rin_http.cpp
    # diagnostics/: نظام الأخطاء الكامل (rin_interpreter.cpp يتضمّن diagnostic_renderer.h/
    # source_manager.h مباشرة أعلى الملف) — رابع ملفات .cpp هنا كلها مطلوبة للربط.
    ${RIN_CPP_DIR}/diagnostics/diagnostic.cpp
    ${RIN_CPP_DIR}/diagnostics/source_manager.cpp
    ${RIN_CPP_DIR}/diagnostics/diagnostic_engine.cpp
    ${RIN_CPP_DIR}/diagnostics/diagnostic_renderer.cpp
    # clc/ — مكتبة CLC (Rin Compact Library Container، .rcl): rin_interpreter.cpp يستدعي
    # clc::readContainerInfo/readContainerInfoFromMemory/packDirectory/extractOneFileToMemory
    # مباشرة ضمن registerNatives()/executeLibraryContainerBytes()، فيفشل الربط بدونها.
    ${RIN_CPP_DIR}/clc/clc_container.cpp
    ${RIN_CPP_DIR}/clc/clc_compress.cpp
    ${RIN_CPP_DIR}/clc/clc_security.cpp
    ${RIN_CPP_DIR}/clc/clc_rin_opt.cpp
    ${RIN_CPP_DIR}/clc/clc_zip_import.cpp
    ${RIN_CPP_DIR}/clc/sha256.cpp
    ${RIN_CPP_DIR}/rin_c_api.cpp
    ${RIN_CPP_DIR}/loom/rin_loom_c_api.cpp
)

target_include_directories(rin PUBLIC ${RIN_CPP_DIR})
target_link_libraries(rin PRIVATE ZLIB::ZLIB)

set_target_properties(rin PROPERTIES
    OUTPUT_NAME "rin"
    CXX_VISIBILITY_PRESET hidden
)
