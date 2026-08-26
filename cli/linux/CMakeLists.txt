# cli/linux/CMakeLists.txt
# ============================================================================
# يبني rin (مُفسِّر Rin كسطر أوامر) على لينكس — بلا Android/NDK أو JNI إطلاقاً.
# راجع README.md المرافق أو شغّل build.sh للبناء المباشر.
#
# نفس ملفات المحرّك الأصلية بالضبط (app/src/main/cpp/rin_lexer|parser|interpreter)
# تُبنى هنا كملف تنفيذي مستقل — أي تعديل لاحق على اللغة نفسها في تلك الملفات
# ينعكس هنا تلقائياً دون الحاجة لنسخ أي كود.
# ============================================================================
cmake_minimum_required(VERSION 3.15)
project(rin_cli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -O2)

set(RIN_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)

add_executable(rin
    src/main.cpp
    ${RIN_CORE_DIR}/rin_lexer.cpp
    ${RIN_CORE_DIR}/rin_parser.cpp
    ${RIN_CORE_DIR}/rin_interpreter.cpp
    ${RIN_CORE_DIR}/rin_http.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic.cpp
    ${RIN_CORE_DIR}/diagnostics/source_manager.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic_engine.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic_renderer.cpp
)

target_include_directories(rin PRIVATE ${RIN_CORE_DIR})

set_target_properties(rin PROPERTIES OUTPUT_NAME "rin")

install(TARGETS rin RUNTIME DESTINATION bin)
