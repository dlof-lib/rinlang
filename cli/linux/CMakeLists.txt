# cli/linux/CMakeLists.txt
# ============================================================================
# يبني rin (مُفسِّر Rin كسطر أوامر) على لينكس — بلا Android/NDK أو JNI إطلاقاً.
# راجع README.md المرافق أو شغّل build.sh للبناء المباشر.
#
# نفس ملفات المحرّك الأصلية بالضبط (app/src/main/cpp/rin_lexer|parser|interpreter)
# تُبنى هنا كملف تنفيذي مستقل — أي تعديل لاحق على اللغة نفسها في تلك الملفات
# ينعكس هنا تلقائياً دون الحاجة لنسخ أي كود.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(rin_cli CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -O2)

# مطلوبة لـ zlibDeflateRaw/zlibInflateRaw في rin_interpreter.cpp (ضغط ZIP حقيقي).
# على أوبنتو/دبيان: sudo apt install zlib1g-dev — عادة مثبّتة مسبقاً على أغلب توزيعات لينكس.
find_package(ZLIB REQUIRED)

set(RIN_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)

add_executable(rin
    src/main.cpp
    # الطرفية التفاعلية الكاملة (rin terminal / rin بلا وسائط): محرّر سطر + تاريخ + إكمال + تلوين.
    # تعتمد فقط على termios/POSIX وعلى واجهة المفسّر؛ لا مكتبات خارجية. انظر docs/terminal.md
    src/terminal/rin_terminal.cpp
    ${RIN_CORE_DIR}/rin_lexer.cpp
    ${RIN_CORE_DIR}/rin_parser.cpp
    # rin_make.cpp: validateMakeUnit/validateContainerPolicy — يستدعيهما execute() في
    # rin_interpreter.cpp أدناه لكل @make/@container، فيفشل الربط (undefined reference) بدونه.
    ${RIN_CORE_DIR}/rin_make.cpp
    ${RIN_CORE_DIR}/rin_container_sql.cpp
    ${RIN_CORE_DIR}/rin_artifact.cpp
    ${RIN_CORE_DIR}/rin_interpreter.cpp
    ${RIN_CORE_DIR}/toolchain/rin_toolchain.cpp
    ${RIN_CORE_DIR}/binfmt/elf_format.cpp
    ${RIN_CORE_DIR}/binfmt/ar_format.cpp
    ${RIN_CORE_DIR}/binfmt/pe_format.cpp
    ${RIN_CORE_DIR}/binfmt/macho_format.cpp
    ${RIN_CORE_DIR}/binfmt/coff_obj.cpp
    ${RIN_CORE_DIR}/loader_ui/library_loader_ui.cpp
    ${RIN_CORE_DIR}/rin_http.cpp
    # indsin داخل الطرفية (:indsin / rin indsin): نفس واجهة C للمحرّك (جلسة + rasterizer) التي تستخدمها
    # مضيفات أندرويد وسطح المكتب؛ لا منطق رسم مكرّر. انظر docs/terminal.md
    ${RIN_CORE_DIR}/rin_c_api.cpp
    ${RIN_CORE_DIR}/indsin/rin_indsin_c_api.cpp
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

target_include_directories(rin PRIVATE ${RIN_CORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../rin_vm/include)
find_package(Threads REQUIRED)  # خيط تحريك مؤشر التقدّم (loader_ui::ProgressBar)
target_link_libraries(rin PRIVATE Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS})  # dlopen لجسر cpp.* (Rin 1.0)

set_target_properties(rin PROPERTIES OUTPUT_NAME "rin")

# RIN_AVA — أداة بناء بواجهة g++ (rin_ava prog.rin -o prog). سائق مستقل بلا اعتماديات خارجية؛
# يفحص بـ `rin check` ويبني تنفيذياً بنسخ rin + إلحاق البرنامج (انظر src/rin_ava_payload.h).
add_executable(rin_ava ${CMAKE_CURRENT_SOURCE_DIR}/../rin_ava/src/rin_ava.cpp)
target_include_directories(rin_ava PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../rin_vm/include)
set_target_properties(rin_ava PROPERTIES OUTPUT_NAME "rin_ava")

# خلفية go في rin_ava: مفسّر Go المدمج (gorin/cmd/rin) كملف gorin-embedded بجوار rin وrin_ava.
# اختياري تماماً: إن لم يوجد Go (أو فشل بناؤه) يستمر البناء وتبقى بقية الخلفيات تعمل؛
# وrin_ava نفسه يستطيع بناءه عند أول استخدام لـ -fgo إن وُجد go ومصدر gorin/.
find_program(GO_EXECUTABLE go)
set(GORIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../gorin)
if (GO_EXECUTABLE AND EXISTS ${GORIN_DIR}/go.mod)
    add_custom_target(gorin_embedded ALL
        COMMAND sh -c "'${GO_EXECUTABLE}' build -o '${CMAKE_CURRENT_BINARY_DIR}/gorin-embedded' ./cmd/rin || echo 'rin_ava: gorin-embedded skipped (go build failed)'"
        WORKING_DIRECTORY ${GORIN_DIR}
        COMMENT "Building gorin-embedded (Go interpreter for rin_ava -fgo)"
        VERBATIM)
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/gorin-embedded DESTINATION bin OPTIONAL)
endif()

# rinc — المترجم الأصلي (Rin → C): مطلوب لخلفية native/auto في rin_ava. ملف واحد بلا اعتماديات؛
# يوضع بجوار rin_ava فيجده تلقائياً.
add_executable(rinc ${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/rinc.cpp)
set_target_properties(rinc PROPERTIES OUTPUT_NAME "rinc")

# rin_vm — أداة حزم صور VM (rin_vm pack/info/unpack) فوق مكتبة cli/rin_vm/include/rin_vm.h
add_executable(rin_vm ${CMAKE_CURRENT_SOURCE_DIR}/../rin_vm/src/rin_vm_tool.cpp)
target_include_directories(rin_vm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../rin_vm/include)

# rinvm — آلة Rin الافتراضية الكاملة: المحرّك الكامل + مكتبة rin_vm في ملف واحد صغير (بلا طرفية/RinPM).
# تشغّل صور .rvm وتحزمها، وتصلح runtime لـ rin_ava -fvm (rin_ava يفضّلها على rin إن وُجدت بجواره).
add_executable(rinvm
    ${CMAKE_CURRENT_SOURCE_DIR}/../rin_vm/src/rinvm_main.cpp
    ${RIN_CORE_DIR}/rin_lexer.cpp
    ${RIN_CORE_DIR}/rin_parser.cpp
    ${RIN_CORE_DIR}/rin_make.cpp
    ${RIN_CORE_DIR}/rin_container_sql.cpp
    ${RIN_CORE_DIR}/rin_artifact.cpp
    ${RIN_CORE_DIR}/rin_interpreter.cpp
    ${RIN_CORE_DIR}/toolchain/rin_toolchain.cpp
    ${RIN_CORE_DIR}/binfmt/elf_format.cpp
    ${RIN_CORE_DIR}/binfmt/ar_format.cpp
    ${RIN_CORE_DIR}/binfmt/pe_format.cpp
    ${RIN_CORE_DIR}/binfmt/macho_format.cpp
    ${RIN_CORE_DIR}/binfmt/coff_obj.cpp
    ${RIN_CORE_DIR}/loader_ui/library_loader_ui.cpp
    ${RIN_CORE_DIR}/rin_http.cpp
    ${RIN_CORE_DIR}/rin_c_api.cpp
    ${RIN_CORE_DIR}/indsin/rin_indsin_c_api.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic.cpp
    ${RIN_CORE_DIR}/diagnostics/source_manager.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic_engine.cpp
    ${RIN_CORE_DIR}/diagnostics/diagnostic_renderer.cpp
    ${RIN_CORE_DIR}/clc/clc_container.cpp
    ${RIN_CORE_DIR}/clc/clc_compress.cpp
    ${RIN_CORE_DIR}/clc/clc_security.cpp
    ${RIN_CORE_DIR}/clc/clc_rin_opt.cpp
    ${RIN_CORE_DIR}/clc/clc_zip_import.cpp
    ${RIN_CORE_DIR}/clc/sha256.cpp
)
target_include_directories(rinvm PRIVATE ${RIN_CORE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../rin_vm/include)
target_link_libraries(rinvm PRIVATE Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS})  # loader_ui يستعمل std::thread

install(TARGETS rin rin_ava rinc rin_vm rinvm RUNTIME DESTINATION bin)
