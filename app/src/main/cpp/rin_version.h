// rin_version.h — المصدر الرسمي الوحيد لرقم إصدار محرّك Rin (lexer/parser/
// interpreter/C API)، المشترك بين كل الواجهات التي تُبنى فوقه: تطبيق أندرويد
// (jni_bridge.cpp)، مكتبة الربط للغات الأخرى (rin_c_api.cpp، انظر bindings/)،
// وأدوات CLI الثلاث (cli/linux، cli/macos، cli/windows).
//
// القاعدة: أي تغيير في رقم الإصدار يبدأ ويَنتهي هنا فقط. الملفات المذكورة
// فوق لا تُعرّف رقمها الخاص أبداً — كلها تُضمِّن هذا الملف وتقرأ
// RIN_VERSION_STRING (أو الثوابت rin::version::k*). لا تُعدِّل تلك الملفات
// مباشرة لتغيير الإصدار.
//
// ما لا يغطّيه هذا الملف عمداً (لها دورة إصدار مستقلة بتصميم متعمّد، انظر
// docs/VERSIONING.md لسبب كل استثناء):
//   - rinc (المترجِم الأصلي الجزئي في compiler/rinc.cpp وnَسخته المطابقة
//     app/src/main/cpp/rinc.cpp): أداة مطوّر منفصلة تُبنى على جهاز التطوير
//     ولا تُضمَّن في libRinengine.so، وتغطّي عمداً حالياً مجموعة فرعية فقط
//     من اللغة (انظر تعليق rinc.cpp). لها "rinc 0.5.0" الخاص بها.
//   - مكتبات .rin المُضمَّنة مثل lib/movingmask.og.rin: لها رقم إصدار خاص
//     بها مذكور في CHANGELOG.md (مثال: "v1.2.0")، مستقل عن إصدار المحرّك
//     الذي يشغّلها.
//   - حزم RinPM المثبَّتة عبر `rin pkg install` (cli/linux/src/pkg/semver.*):
//     semver خاص بكل حزمة على حدة، لا علاقة له بإصدار المحرّك نفسه.
//
// عند رفع الإصدار: (1) عدّل الثوابت الثلاثة تحت فقط، (2) اجعل ملف VERSION في
// جذر المستودع مطابقاً حرفياً لـ RIN_VERSION_STRING، (3) أضف فقرة جديدة في
// CHANGELOG.md. التفاصيل الكاملة في docs/VERSIONING.md.

#pragma once

#define RIN_VERSION_MAJOR 1
#define RIN_VERSION_MINOR 0
#define RIN_VERSION_PATCH 0

// إصدار "نصّي" بصيغة SemVer (major.minor.patch) — يجب أن يطابق حرفياً محتوى
// ملف VERSION في جذر المستودع.
#define RIN_VERSION_STRING "1.0.0"

// اسم دوري/تسويقي يظهر بجانب الرقم في مخرجات مثل `rin --version` (كما كان
// معمولاً به فعلاً في cli/linux/src/main.cpp قبل هذا الملف). لا علاقة له
// بترقيم SemVer، يمكن تثبيته على قيمة واحدة لعدة إصدارات رقمية.
#define RIN_VERSION_EDITION "2026"

// نفس المعلومات كثوابت C++ (مفضَّلة على الماكروهات في كود C++ جديد) لمن
// يحتاج التعامل مع الأجزاء الرقمية منفردة بدل السلسلة النصية الكاملة.
namespace rin {
namespace version {

constexpr int kMajor = RIN_VERSION_MAJOR;
constexpr int kMinor = RIN_VERSION_MINOR;
constexpr int kPatch = RIN_VERSION_PATCH;
constexpr const char* kString  = RIN_VERSION_STRING;
constexpr const char* kEdition = RIN_VERSION_EDITION;

} // namespace version
} // namespace rin
