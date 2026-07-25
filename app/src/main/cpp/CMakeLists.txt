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
)

find_library(log-lib log)

target_link_libraries(
        rinengine
        ${log-lib}
)
