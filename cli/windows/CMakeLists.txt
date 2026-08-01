# cli/windows/CMakeLists.txt
# ============================================================================
# يبني rin.exe (مُفسِّر Rin كسطر أوامر) على ويندوز — بلا Android/NDK أو JNI
# إطلاقاً. يُستخدَم مباشرة عبر CMake (MSVC أو MinGW)؛ راجع README.md المرافق
# أو شغّل build.bat لبناء تلقائي.
#
# نفس ملفات المحرّك الأصلية بالضبط (app/src/main/cpp/rin_lexer|parser|interpreter)
# تُبنى هنا كملف تنفيذي مستقل — أي تعديل لاحق على اللغة نفسها في تلك الملفات
# ينعكس هنا تلقائياً دون الحاجة لنسخ أي كود.
# ============================================================================
cmake_minimum_required(VERSION 3.15)
project(rin_cli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    # /utf-8: يجبر MSVC على قراءة ملفات المصدر (وفيها نصوص عربية داخل رسائل
    # الأخطاء والمكتبة القياسية) كـ UTF-8 دوماً، بصرف النظر عن Locale النظام —
    # بدونها قد تتحوّل الأحرف العربية داخل الملف التنفيذي إلى رموز تالفة.
    add_compile_options(/utf-8 /W3)
else()
    add_compile_options(-Wall -O2)
endif()

set(RIN_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)

add_executable(rin
    src/main.cpp
    ${RIN_CORE_DIR}/rin_lexer.cpp
    ${RIN_CORE_DIR}/rin_parser.cpp
    ${RIN_CORE_DIR}/rin_interpreter.cpp
)

target_include_directories(rin PRIVATE ${RIN_CORE_DIR})

set_target_properties(rin PROPERTIES OUTPUT_NAME "rin")
