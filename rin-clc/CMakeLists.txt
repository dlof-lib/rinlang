# CMakeLists.txt — يبني clc (سطر أوامر CLC 1.0.0) كتنفيذي مستقل.
# راجع build.sh للبناء المباشر، أو README.md لتفاصيل أوسع.
cmake_minimum_required(VERSION 3.15)
project(rin_clc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -O2)

find_package(ZLIB REQUIRED)

set(CORE_SRC
    src/sha256.cpp
    src/clc_compress.cpp
    src/clc_rin_opt.cpp
    src/clc_security.cpp
    src/clc_container.cpp
    src/clc_zip_import.cpp
)

add_executable(clc
    src/cli/main.cpp
    src/cli/self_test.cpp
    ${CORE_SRC}
)

target_include_directories(clc PRIVATE src)
target_link_libraries(clc PRIVATE ZLIB::ZLIB)

set_target_properties(clc PROPERTIES OUTPUT_NAME "clc")

install(TARGETS clc RUNTIME DESTINATION bin)

enable_testing()
add_test(NAME clc_self_test COMMAND clc test)
