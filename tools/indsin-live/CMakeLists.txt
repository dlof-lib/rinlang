# Indsin Live Preview server — cross-platform (Linux / macOS / Windows with MinGW or clang).
#   cmake -S tools/indsin-live -B build-live -DCMAKE_BUILD_TYPE=Release && cmake --build build-live -j
cmake_minimum_required(VERSION 3.16)
project(rin_indsin_live CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../app/src/main/cpp" ABSOLUTE)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
add_executable(rin_indsin_live
  rin_indsin_live.cpp
  ${CORE}/indsin/rin_indsin_c_api.cpp ${CORE}/rin_c_api.cpp ${CORE}/rin_lexer.cpp ${CORE}/rin_parser.cpp ${CORE}/rin_make.cpp
  ${CORE}/rin_container_sql.cpp ${CORE}/rin_artifact.cpp ${CORE}/rin_interpreter.cpp
  ${CORE}/binfmt/elf_format.cpp ${CORE}/binfmt/ar_format.cpp ${CORE}/binfmt/pe_format.cpp ${CORE}/binfmt/macho_format.cpp ${CORE}/binfmt/coff_obj.cpp
  ${CORE}/loader_ui/library_loader_ui.cpp ${CORE}/rin_http.cpp
  ${CORE}/diagnostics/diagnostic.cpp ${CORE}/diagnostics/source_manager.cpp ${CORE}/diagnostics/diagnostic_engine.cpp ${CORE}/diagnostics/diagnostic_renderer.cpp
  ${CORE}/clc/clc_container.cpp ${CORE}/clc/clc_compress.cpp ${CORE}/clc/clc_security.cpp ${CORE}/clc/clc_rin_opt.cpp ${CORE}/clc/clc_zip_import.cpp ${CORE}/clc/sha256.cpp)
target_include_directories(rin_indsin_live PRIVATE ${CORE})
target_link_libraries(rin_indsin_live PRIVATE Threads::Threads ZLIB::ZLIB)
if(WIN32)
  target_link_libraries(rin_indsin_live PRIVATE ws2_32 shell32)
endif()
