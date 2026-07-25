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

add_library(rin SHARED
    ${RIN_CPP_DIR}/rin_lexer.cpp
    ${RIN_CPP_DIR}/rin_parser.cpp
    ${RIN_CPP_DIR}/rin_interpreter.cpp
    ${RIN_CPP_DIR}/rin_c_api.cpp
)

target_include_directories(rin PUBLIC ${RIN_CPP_DIR})

set_target_properties(rin PROPERTIES
    OUTPUT_NAME "rin"
    CXX_VISIBILITY_PRESET hidden
)
