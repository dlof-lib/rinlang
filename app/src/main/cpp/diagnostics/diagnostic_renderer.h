#pragma once
// Rin Diagnostics - diagnostic_renderer.h
//
// يحوّل Diagnostic (بيانات بنيوية بحتة) إلى نص معروض للمستخدم، بأربعة أشكال:
//   plain -> نمط rustc/swiftc الكامل مع سطر الكود والسهم ^^^^ (الافتراضي)
//   short -> سطر واحد "file:line:col: error[E0001]: message" (لمخرجات IDE/grep)
//   json  -> كائن JSON لكل Diagnostic (rin check --format=json)
//   lsp   -> Diagnostic بصيغة Language Server Protocol (severity/range/message/code/...)
#include <string>
#include "diagnostic.h"
#include "source_manager.h"
#include "diagnostic_engine.h"

namespace rin::diag {

enum class OutputFormat { Plain, Short, Json, Lsp };

// يعرض Diagnostic واحد بالشكل rustc/swiftc الكامل (القسم 5 في الطلب):
//
//   error[E0005]: type mismatch
//     --> app.rin:4:15
//
//      4 | let age: int = "hello";
//        |               ^^^^^^^
//
//   reason:
//     ...
//   expected:
//     ...
//   found:
//     ...
//   help:
//     ...
std::string renderPlain(const Diagnostic& d, const SourceManager& sm);

// سطر واحد فقط، بلا سياق كود — مناسب لعرض قوائم مطوّلة من الأخطاء بسرعة.
std::string renderShort(const Diagnostic& d);

// تمثيل JSON لِـ Diagnostic واحد (كائن واحد {...}؛ ليس مصفوفة).
std::string renderJson(const Diagnostic& d);

// تمثيل متوافق مع LSP Diagnostic: severity/range/message/code/source/relatedInformation.
std::string renderLsp(const Diagnostic& d);

// يعرض كل Diagnostics المُجمَّعة في DiagnosticEngine، بالتنسيق المطلوب،
// ويضيف سطر الخلاصة النهائي ("N errors emitted" أو "N errors, M warnings emitted").
// لـ json/lsp تكون النتيجة مصفوفة JSON واحدة (لا خلاصة نصية بعدها).
std::string renderAll(const DiagnosticEngine& engine, const SourceManager& sm, OutputFormat fmt);

} // namespace rin::diag
