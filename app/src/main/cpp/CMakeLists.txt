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

# rinc: مترجم Rin الأصلي (native compiler) — هدف مستقل تماماً عن مكتبة rinengine
# أعلاه. لا يُستخدَم من التطبيق نفسه (يستدعي مترجم C عبر system() على جهاز
# البناء)، لذا لا يُربط بـ rinengine ولا يُحزَم داخل APK؛ يُبنى فقط كأداة سطر
# أوامر مستقلة (مفيدة عند البناء على جهاز تطوير عادي: يُنتج ./rinc قابلاً
# لتحويل ملفات .rin إلى ملفات تنفيذية أصلية).
add_executable(
        rinc
        rinc.cpp
)

find_library(log-lib log)

target_link_libraries(
        rinengine
        ${log-lib}
)
