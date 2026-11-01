// rin_c_api.h
// =====================================================================
// واجهة C مسطّحة (Flat C ABI) لمحرّك لغة Rin.
// الهدف: استدعاء Rin من أي لغة برمجة (Python, Node.js, Rust, Go, C#, ...)
// عبر بناء المحرّك كمكتبة مشتركة (librin.so / librin.dylib / rin.dll)
// وتحميلها من تلك اللغات (ctypes / ffi / P/Invoke / cgo ... إلخ).
//
// لماذا C وليس ++C مباشرة؟ لأن أغلب لغات البرمجة تعرف كيف تتصل بـ C ABI
// مباشرة (لا name-mangling، لا كائنات C++، أنواع بسيطة فقط)، بينما C++ ABI
// يختلف بين المُصرِّفات ولا يصلح للربط بين لغات مختلفة.
// =====================================================================
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #define RIN_API __declspec(dllexport)
#else
  #define RIN_API __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------------
// rin_run: يحلّل لفظياً وينحو ويفسّر برنامج Rin كاملاً، ويُرجع كل ما طبعه
// (أو رسالة خطأ مُنسَّقة تبدأ بـ "[Syntax error" أو "[Internal error").
//
// - source:   نص برنامج Rin (سلسلة UTF-8 منتهية بـ NUL).
// - basePath: جذر حقيقي على القرص لعمليات save/file/installation/writeFile/readFile
//             (يمكن تمرير "" لاستخدام مجلد العمل الحالي CWD).
//
// القيمة المُرجَعة مُخصَّصة (malloc) على الكومة (heap) داخل المكتبة، ويجب
// تحريرها عبر rin_free_string بعد الانتهاء منها لتفادي تسريب الذاكرة —
// هذا ضروري لأن كل لغة مضيفة لها مخصِّص ذاكرة (allocator) مختلف عن الآخر.
RIN_API char* rin_run(const char* source, const char* basePath);

// يحرِّر سلسلة أُعيدت من rin_run (أو أي دالة أخرى في هذه الواجهة تُعيد char*).
RIN_API void rin_free_string(char* s);

// نسخة/معلومات المحرّك، لأغراض التشخيص والتوافق بين اللغات المضيفة.
RIN_API const char* rin_engine_version(void);

// ---------------------------------------------------------------------
// واجهة "جلسة" (session) اختيارية: تسمح بتشغيل عدة برامج Rin متتالية
// مع الاحتفاظ بنفس basePath دون إعادة تمريره في كل مرة. مفيدة عند بناء
// REPL أو خدمة (server) بلغة أخرى تستدعي Rin بشكل متكرر.
typedef struct RinSession RinSession;

// ينشئ جلسة جديدة بجذر basePath ثابت لكل عملياتها. أعِد النتيجة إلى
// rin_session_free عند الانتهاء.
RIN_API RinSession* rin_session_create(const char* basePath);

// يشغّل مصدر Rin ضمن جلسة موجودة، ويُرجع كل ما طُبع (يجب تحريرها بـ rin_free_string).
RIN_API char* rin_session_run(RinSession* session, const char* source);

// يحرّر الجلسة وموارد المحرّك المرتبطة بها.
RIN_API void rin_session_free(RinSession* session);

#ifdef __cplusplus
}
#endif
