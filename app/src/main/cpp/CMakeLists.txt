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
        # rin_c_api.cpp + loom/: محرّك العرض Loomtime (rendering engine) — إضافي بحت فوق
        # مفسّر اللغة أعلاه؛ يحوّل جذور @view.<Kind>=name إلى Fabric (شجرة عرض) مُخطَّطة
        # ومرسومة عبر Loom/Dye، ويُعرَّض إلى Kotlin عبر jni_bridge.cpp -> RinEngine.kt.
        rin_c_api.cpp
        loom/rin_loom_c_api.cpp
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

# loomc: أداة سطر أوامر مستقلة لمحرّك العرض Loomtime — نفس فلسفة rinc أعلاه تماماً:
# هدف مستقل، لا يُربط بـ rinengine ولا يُحزَم داخل APK، للاختبار على جهاز تطوير عادي فقط.
add_executable(
        loomc
        loom/loomc.cpp
        rin_lexer.cpp
        rin_parser.cpp
)

find_library(log-lib log)

target_link_libraries(
        rinengine
        ${log-lib}
)
