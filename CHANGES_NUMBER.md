# CHANGES_NUMBER.md — تطوير مفهوم number

إضافة بحتة: 21 دالة جديدة (`number` و`numberXxx`)؛ لا تغيير في `toNumber/round/sum/clamp/...`.

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_extra_natives9.cpp` | جديد: الدوال |
| `app/src/main/cpp/rin_interpreter.cpp/.h` | تسجيل `registerNativesExtra9` وتضمين الملف |
| `rinc.cpp`، `RinSyntax.kt`، `syntaxes/rin-keywords.json` | الأسماء الجديدة |
| `scripts/intellisense_docs.json`، `editor/rin_intellisense_data.h` | التوثيق (والملف مُعاد توليده بالمولّد) |
| `docs/NUMBER.md` | الوثيقة |
| `tests/verification/number_concept.rin/.expected` | الاختبار (78 سطراً) |

ملاحظة جانبية (غير معدّلة): Lexer ينهار (`std::out_of_range` من `stod`) على الحرفية الدنيا `5e-324`.
