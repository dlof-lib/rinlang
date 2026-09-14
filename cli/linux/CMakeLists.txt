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

# مطلوبة لـ zlibDeflateRaw/zlibInflateRaw في rin_interpreter.cpp (ضغط ZIP حقيقي).
# على أوبنتو/دبيان: sudo apt install zlib1g-dev — عادة مثبّتة مسبقاً على أغلب توزيعات لينكس.
find_package(ZLIB REQUIRED)

# ---- اختياري: BZIP2/liblzma(XZ)/Zstandard — natives bz2Compress/xzCompress/zstdCompress ----
# (تُستخدَم من lib/archivekit.og.rin لدعم .bz2/.xz/.zst و.tar.bz2/.tar.xz/.tar.zst حقيقياً).
# خلافاً لـ zlib أعلاه، الثلاثة اختيارية تماماً: إن لم تتوفر إحداها يُبنى rin بدونها بلا أي
# خطأ، وتبقى natives المقابلة لها ببساطة غير مسجَّلة (راجع تعليق RIN_HAVE_BZ2/RIN_HAVE_ZSTD/
# RIN_HAVE_LZMA في rin_interpreter.cpp لسبب هذا الاختيار على منصّات أخرى مثل Android/WASM).
# لتفعيل الثلاثة معاً على أوبنتو/دبيان: sudo apt install libbz2-dev libzstd1 liblzma5
# (Zstandard/LZMA لا يحتاجان رأسيهما الحقيقيين — rin_interpreter.cpp يُصرِّح بتوقيعات الواجهة
# العامة المستقرة لكل منهما يدوياً؛ يكفي وجود المكتبة وقت الربط والتشغيل).

find_path(RIN_BZ2_INCLUDE_DIR bzlib.h)
find_library(RIN_BZ2_LIB NAMES bz2)
if (RIN_BZ2_INCLUDE_DIR AND RIN_BZ2_LIB)
    message(STATUS "rin: BZIP2 موجودة -> .bz2/.tar.bz2 مدعومة (RIN_HAVE_BZ2): ${RIN_BZ2_LIB}")
else()
    message(STATUS "rin: BZIP2 (bzlib.h/libbz2) غير موجودة -> .bz2/.tar.bz2 غير مدعومة في هذا البناء")
endif()

find_library(RIN_ZSTD_LIB NAMES zstd libzstd.so.1 libzstd)
if (NOT RIN_ZSTD_LIB)
    file(GLOB _rin_zstd_candidates /usr/lib/*/libzstd.so.1 /usr/lib/libzstd.so.1 /lib/*/libzstd.so.1 /lib/libzstd.so.1)
    if (_rin_zstd_candidates)
        list(GET _rin_zstd_candidates 0 RIN_ZSTD_LIB)
    endif()
endif()
if (RIN_ZSTD_LIB)
    message(STATUS "rin: Zstandard موجودة -> .zst/.tar.zst مدعومة (RIN_HAVE_ZSTD): ${RIN_ZSTD_LIB}")
else()
    message(STATUS "rin: Zstandard (libzstd) غير موجودة -> .zst/.tar.zst غير مدعومة في هذا البناء")
endif()

find_library(RIN_LZMA_LIB NAMES lzma liblzma.so.5 liblzma)
if (NOT RIN_LZMA_LIB)
    file(GLOB _rin_lzma_candidates /usr/lib/*/liblzma.so.5 /usr/lib/liblzma.so.5 /lib/*/liblzma.so.5 /lib/liblzma.so.5)
    if (_rin_lzma_candidates)
        list(GET _rin_lzma_candidates 0 RIN_LZMA_LIB)
    endif()
endif()
if (RIN_LZMA_LIB)
    message(STATUS "rin: liblzma موجودة -> .xz/.tar.xz مدعومة (RIN_HAVE_LZMA): ${RIN_LZMA_LIB}")
else()
    message(STATUS "rin: liblzma غير موجودة -> .xz/.tar.xz غير مدعومة في هذا البناء")
endif()

set(RIN_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)

add_executable(rin
    src/main.cpp
    ${RIN_CORE_DIR}/rin_lexer.cpp
    ${RIN_CORE_DIR}/rin_parser.cpp
    # rin_make.cpp: validateMakeUnit/validateContainerPolicy — يستدعيهما execute() في
    # rin_interpreter.cpp أدناه لكل @make/@container، فيفشل الربط (undefined reference) بدونه.
    ${RIN_CORE_DIR}/rin_make.cpp
    ${RIN_CORE_DIR}/rin_interpreter.cpp
    ${RIN_CORE_DIR}/loader_ui/library_loader_ui.cpp
    ${RIN_CORE_DIR}/rin_http.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic.cpp
    ${RIN_CORE_DIR}/diagnostics/source_manager.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic_engine.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic_renderer.cpp
    # clc/ — مكتبة CLC (Rin Compact Library Container، .rcl): rin_interpreter.cpp يستدعي
    # clc::readContainerInfo/readContainerInfoFromMemory/packDirectory/extractOneFileToMemory
    # مباشرة ضمن registerNatives()/executeLibraryContainerBytes()، فيفشل الربط بدونها.
    ${RIN_CORE_DIR}/clc/clc_container.cpp
    ${RIN_CORE_DIR}/clc/clc_compress.cpp
    ${RIN_CORE_DIR}/clc/clc_security.cpp
    ${RIN_CORE_DIR}/clc/clc_rin_opt.cpp
    ${RIN_CORE_DIR}/clc/clc_zip_import.cpp
    ${RIN_CORE_DIR}/clc/sha256.cpp
    # RinPM — Rin Package Manager (src/pkg/*). طبقة مستقلة تماماً عن محرك اللغة؛
    # تتصل به فقط عبر نقطة تكامل واحدة داخل rin_interpreter.cpp (حل @import عبر الحزم).
    src/pkg/semver.cpp
    src/pkg/toml_lite.cpp
    src/pkg/sha256.cpp
    src/pkg/json_lite.cpp
    src/pkg/manifest.cpp
    src/pkg/lockfile.cpp
    src/pkg/archive.cpp
    src/pkg/cache.cpp
    src/pkg/registry.cpp
    src/pkg/resolver.cpp
    src/pkg/package_ops.cpp
    src/pkg/auth.cpp
    src/pkg/cli_pkg.cpp
)

target_include_directories(rin PRIVATE ${RIN_CORE_DIR})
target_link_libraries(rin PRIVATE ZLIB::ZLIB)

if (RIN_BZ2_INCLUDE_DIR AND RIN_BZ2_LIB)
    target_compile_definitions(rin PRIVATE RIN_HAVE_BZ2)
    target_include_directories(rin PRIVATE ${RIN_BZ2_INCLUDE_DIR})
    target_link_libraries(rin PRIVATE ${RIN_BZ2_LIB})
endif()
if (RIN_ZSTD_LIB)
    target_compile_definitions(rin PRIVATE RIN_HAVE_ZSTD)
    target_link_libraries(rin PRIVATE ${RIN_ZSTD_LIB})
endif()
if (RIN_LZMA_LIB)
    target_compile_definitions(rin PRIVATE RIN_HAVE_LZMA)
    target_link_libraries(rin PRIVATE ${RIN_LZMA_LIB})
endif()

set_target_properties(rin PROPERTIES OUTPUT_NAME "rin")

install(TARGETS rin RUNTIME DESTINATION bin)
