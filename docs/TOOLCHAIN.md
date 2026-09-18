# Rin Professional Language Toolchain

هذه الطبقة تجعل مسار تطوير Rin واضحاً ومتكاملاً، مع قاعدة مهمة: **Lexer + Parser + AST الأصليّة هي المصدر الوحيد للنحو**. لا توجد Grammar ثانية للـIDE أو الـVM.

## خط الأنابيب

```text
.rin source
   │
   ▼
Lexer / Tokenizer
   │ tokens + source locations
   ▼
Parser
   │
   ▼
AST
   │
   ├──────────────► Diagnostics / Error Recovery
   │
   ▼
Semantic Analyzer
   │ names / scopes / control-flow rules
   ▼
Type Checker
   │ Any / Number / Int / String / Bool / Array / Map / Function / Nil
   │
   ├──────────────► Interpreter / Runtime
   │
   └──────────────► Bytecode Compiler
                         │
                         ▼
                     Optimizer
                         │
                         ▼
                    .rbc Bytecode
                         │
                         ▼
                       VM
```

## المكوّنات الموجودة في المشروع

| النظام | Rin implementation |
|---|---|
| Lexer / Tokenizer | `app/src/main/cpp/rin_lexer.*` |
| Parser | `app/src/main/cpp/rin_parser.*` |
| AST | `app/src/main/cpp/rin_ast.h` |
| Semantic Analyzer | `app/src/main/cpp/toolchain/rin_toolchain.*` |
| Type Checker | `app/src/main/cpp/toolchain/rin_toolchain.*` |
| Interpreter | `app/src/main/cpp/rin_interpreter.*` |
| Native Compiler | `compiler/rinc.cpp` |
| Code Generator | `compiler/rinc.cpp` |
| Bytecode Compiler | `toolchain/rin_toolchain.*` |
| Optimizer | `toolchain/rin_toolchain.*` |
| Bytecode VM | `toolchain/rin_toolchain.*` |
| Runtime | `rin_interpreter.*` + native runtime |
| Diagnostics | `app/src/main/cpp/diagnostics/*` |
| CLI | `cli/linux/src/main.cpp` |
| Package Manager | `cli/linux/src/pkg/*` |
| Project Generator | `rin new` + `rin toolchain-new` |

## CLI الجديد

```bash
rin analyze main.rin
rin bytecode main.rin
rin bytecode main.rin --optimize
rin bytecode main.rin --out build/main.rbc
rin vm main.rin
rin toolchain-new my_app
```

### analyze

ينفذ Semantic Analyzer ثم Type Checker ويجمع الأخطاء والتحذيرات قبل التشغيل.

### bytecode

يبني Bytecode داخلياً ويعرض Disassembly. يمكن حفظه كملف `.rbc`.

### vm

يشغّل الـBytecode مباشرة داخل VM مع حماية من الحلقات غير المنتهية عبر instruction budget.

### toolchain-new

ينشئ:

```text
my_app/
├── rin.toml
├── README.md
├── src/
│   └── main.rin
├── tests/
│   └── main.rin
└── build/
```

## فلسفة الأنواع

Rin تبقى Dynamic-first، لكن النوع الاختياري يصبح مفيداً في المشاريع الكبيرة:

```rin
let name: String = "ABOO";
let age: Int = 19;
let score: Number = 99.5;
let online: Bool = true;
let tags: Array = ["rin", "indsin"];
let data: Map = {};
```

الأنواع الأساسية:

`Any`, `Number`, `Int`, `String`, `Bool`, `Array`, `Map`, `Function`, `Nil`

والـType Checker لا يكسر البرامج القديمة التي لا تستخدم type annotations.

## حدود الـBytecode backend

الـVM الجديد هو **Core Bytecode Backend** آمن وقابل للتوسعة، ويغطي حالياً literals، variables، assignment، arithmetic/comparison، conditionals، loops، print وreturn.

أما ميزات Rin المتقدمة مثل Containers وIndsintime وNoSQL وHTTP وCLC وCandle فتظل مرتبطة بالـcanonical interpreter/native backend إلى أن تُضاف لها Lowering/Runtime ABI مستقلة.

هذا مقصود وليس fallback صامتاً: أي ميزة غير مدعومة في bytecode تظهر كـdiagnostic واضحة.

## التصميم الاحترافي التالي

للوصول إلى VM كامل بلا استثناءات، المرحلة التالية هي إضافة:

1. `Value` tagged representation مشتركة بين VM وRuntime.
2. Call frames + closures.
3. Function bytecode objects.
4. Arrays/Maps كـheap objects.
5. GC أو reference-counted object heap.
6. Module bytecode/import table.
7. Classes/structs/enums lowering.
8. Container/Indsin lowering إلى Runtime ABI.
9. Debug symbols (`.rbc.map`) وbreakpoints.
10. Incremental compilation + cache.
11. LSP diagnostics من نفس Semantic Analyzer.
12. Package lock + dependency graph من RinPM.
13. Cross-platform VM targets: Linux / Windows / macOS / Android.
14. Deterministic bytecode versioning (`RBC1`, ثم `RBC2` عند كسر ABI).

## قاعدة معمارية مهمة

لا تضف Lexer أو Parser ثانياً داخل `rinc` أو الـVM.

المسار الصحيح:

```text
Rin Source
   ↓
Canonical Lexer
   ↓
Canonical Parser
   ↓
Canonical AST
   ↓
┌───────────────┬───────────────┬──────────────┐
│ Interpreter   │ Bytecode      │ Native Code  │
│ Runtime       │ Compiler + VM │ Generator    │
└───────────────┴───────────────┴──────────────┘
```

بهذا تصبح Rin لغة واحدة لها عدة Backends، بدلاً من عدة لغات متشابهة تتباعد مع الوقت.
