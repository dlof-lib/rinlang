// ============================================================================
//  rin_vm.h — RinVM: آلة Rin الافتراضية (Bytecode Virtual Machine)
// ============================================================================
//
//  آلة مكدّس (stack machine) حقيقية لكامل لغة Rin:
//
//     AST (rin_parser)  ──►  Compiler (rin_bcvm_compiler.cpp)  ──►  Proto (بايتكود)
//                                                                     │
//                                          Interpreter::vmExecute (rin_bcvm_exec.cpp)
//
//  - كل عبارة وتعبير في اللغة يُترجَم إلى تعليمات بايتكود (Instr) تُنفَّذ بحلقة dispatch
//    واحدة، بمكدّس قيم، ومكدّس إطارات نداء (Frame) خاص بالآلة (نداء دالة Rin من Rin لا يستهلك
//    مكدّس C++)، وجدول معالجات استثناءات (try/catch/goal) وعلامات حلقات (break/continue).
//  - الدلالات مطابقة حرفياً للمفسِّر الشجري لأن كل تعليمة معقّدة تستدعي نفس الدوال المساعدة التي
//    يستدعيها المفسِّر (readMember/writeMember/invokeCallee/liveAssign/tryOperatorOverload ...):
//    الميزات الحية (المتغيرات الحيّة، OOP، الحاويات، الربط، Flow ...) تعمل بلا نسخ منطقها.
//  - العبارات التصريحية الكبيرة (@container/@view/@save/@route ... ) تُنفَّذ عبر تعليمة ExecStmt
//    التي تفوّض للـ runtime الأصلي، بينما كل كتلة/تعبير بداخلها يمرّ على الـ VM (انظر docs/vm.md).
//
//  هذا الملف: تعريف مجموعة التعليمات وبنية Proto ومترجم AST→بايتكود والمفكّك (disassembler).
// ============================================================================
#pragma once
#include "rin_interpreter.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rin::vm {

// ---------------------------------------------------------------------------
// مجموعة التعليمات. الصيغة: X(الاسم). المعاملات (a, b, c) موصوفة بجانب كل تعليمة.
// المكدّس: [... قاع → قمة]. «→» تعني ما يُسحب وما يُدفع.
// ---------------------------------------------------------------------------
#define RIN_VM_OPS(X) \
    /* ثوابت ومكدّس */ \
    X(Nop)          /* — */ \
    X(Const)        /* a=ثابت  → قيمة */ \
    X(Nil) X(True) X(False) \
    X(Pop)          /* قيمة → — */ \
    X(PopN)         /* a=n  n قيم → — */ \
    X(Dup)          /* v → v v */ \
    /* متغيرات */ \
    X(LoadVar)      /* a=اسم b=سطر  → قيمة   (يراعي المتغيرات الحيّة) */ \
    X(DefineVar)    /* a=اسم  v → —           (env->define) */ \
    X(AssignVar)    /* a=اسم b=سطر  v → المخزَّن (state/live aware) */ \
    X(AssignAppend) /* a=expr(AssignExpr) b=قفزة: s = s + rhs في المكان؛ إن نجح قفز إلى b */ \
    X(PushScope)    /* بيئة جديدة ابنة للحالية */ \
    X(PopScope)     /* a=عدد */ \
    /* حساب ومقارنة (a=سطر)  l r → ناتج */ \
    X(Add) X(Sub) X(Mul) X(Div) X(Mod) \
    X(Lt) X(Le) X(Gt) X(Ge) X(Eq) X(Ne) X(NotSame) X(CopyOf) \
    X(Neg)          /* a=سطر */ \
    X(Not) \
    /* قفز */ \
    X(Jmp)          /* a=هدف */ \
    X(JmpFalse)     /* a=هدف  v →   (يسحب دائماً) */ \
    X(JmpTrue) \
    X(JmpFalseKeep) /* إن كانت القمة زائفة: قفز مع إبقائها، وإلا تُسحب */ \
    X(JmpTrueKeep) \
    /* مجموعات وفهرسة */ \
    X(MakeArray)    /* a=n  n قيم → مصفوفة */ \
    X(MakeMap)      /* a=n  2n قيم (مفتاح،قيمة) → قاموس */ \
    X(Index)        /* a=سطر  obj idx → قيمة */ \
    X(IndexSet)     /* a=سطر  obj idx val → val */ \
    X(LiveGuard)    /* a=expr(الكائن) b=سطر — حارس stone/lens قبل أي كتابة */ \
    /* OOP / خصائص */ \
    X(GetProp)      /* a=اسم b=سطر  obj → قيمة */ \
    X(GetVarProp)   /* a=خاصية b=متغير c=سطر → قيمة (static/live/عادي) */ \
    X(GetSuper)     /* a=اسم c=سطر → قيمة */ \
    X(SetProp)      /* a=اسم b=سطر  obj val → val */ \
    X(StaticSetTest)/* a=متغير b=قفزة-إن-صنف: ClassName.f = v ؟ */ \
    X(StaticSet)    /* a=صنف b=اسم c=سطر  val → val */ \
    /* نداءات */ \
    X(CallNamed)    /* a=اسم b=argc|flags<<16 c=سطر  args → قيمة */ \
    X(CallValue)    /* a=argc b=سطر  callee args → قيمة */ \
    X(CallMethod)   /* a=اسم b=argc c=سطر  obj args → قيمة */ \
    X(CallSuper)    /* a=اسم b=argc c=سطر  args → قيمة */ \
    X(PipeFlow)     /* a=expr(CallExpr) b=نهاية: عند جلسة RinFlow نشطة → نتيجة وقفز */ \
    X(MakeClosure)  /* a=expr(LambdaExpr) → دالة */ \
    /* تعريفات */ \
    X(FuncDef)      /* a=stmt(FunctionStmt) */ \
    X(ClassDef)     /* a=stmt(ClassStmt) */ \
    X(EnumDef)      /* a=stmt(EnumStmt)  قيم الحالات → — */ \
    X(Let)          /* a=stmt(LetStmt)  v → — */ \
    X(LetPattern)   /* a=stmt(LetPatternStmt) */ \
    X(PatternAssign)/* a=expr(PatternAssignExpr) → قيمة */ \
    /* print */ \
    X(CheckStr)     /* a=رسالة b=سطر : القمة يجب أن تكون نصاً */ \
    X(CheckNum)     /* a=رسالة b=سطر : القمة يجب أن تكون رقماً */ \
    X(Truthy)       /* v → bool */ \
    X(DisplayP)     /* a=مسافة إلى علم pretty : القمة → نصها */ \
    X(CheckUL)      /* a=سطر : upper و lower معاً خطأ */ \
    X(CheckAlign)   /* a=سطر */ \
    X(CheckLevel)   /* a=سطر */ \
    X(CheckRepeat)  /* a=سطر */ \
    X(Fail)         /* a=رسالة b=سطر c=كود التشخيص */ \
    X(PrintEmit)    /* a=stmt(PrintStmt) */ \
    /* تحكم */ \
    X(Return)       /* v →  (إنهاء الإطار) */ \
    X(End)          /* نهاية Proto: بدون قيمة */ \
    X(ExprEnd)      /* v →  نهاية Proto تعبير */ \
    X(Break) X(Continue) \
    X(Tick)         /* a=سطر — ميزانية التنفيذ (setExecutionBudget) */ \
    X(Throw)        /* a=سطر  v → ✗ */ \
    X(TryBegin)     /* a=هدف catch */ \
    X(TryEnd) \
    X(CatchBind)    /* a=اسم أو -1  خريطة الخطأ → — */ \
    X(GoalBegin)    /* a=نهاية */ \
    X(GoalEnd) \
    X(Achieve)      /* v → ✗ */ \
    X(LoopEnter)    /* a=هدف break b=هدف continue */ \
    X(LoopSync) \
    X(LoopLeave) \
    X(ForIterBegin) /* بيئة تكرار لكل دورة لـ for(;;) */ \
    X(ForIterEnd) \
    X(ForInPrep)    /* a=سطر  iterable → items idx */ \
    X(ForInNext)    /* a=نهاية b=stmt(ForInStmt)  items idx → items idx' (+بيئة) */ \
    X(MatchTest)    /* v → bool   (subject تحتها) */ \
    X(ExecStmt)     /* a=stmt — تفويض لعبارة تصريحية للـ runtime */

enum class Op : uint8_t {
#define X(n) n,
    RIN_VM_OPS(X)
#undef X
    kCount
};

const char* opName(Op op);

struct Instr {
    Op op = Op::Nop;
    int32_t a = 0, b = 0, c = 0;
};

// flags في الجزء العالي من b لـ CallNamed
constexpr int32_t kCallFlagEligible = 1; // الاسم بلا نقطة وليس builtinOp/libraryImport: يصلح لإطار نداء داخل الـ VM

// ---------------------------------------------------------------------------
// Proto: وحدة بايتكود واحدة (متن دالة، كتلة عبارات، أو تعبير منفرد).
// ---------------------------------------------------------------------------
struct Proto {
    std::string name;
    std::vector<Instr> code;
    std::vector<int32_t> lines;                 // سطر المصدر لكل تعليمة (للمفكّك/التشخيص)
    std::vector<Value> consts;                  // ثوابت رقمية/نصية
    std::vector<std::string> strs;              // أسماء ورسائل
    std::vector<StmtPtr> stmts;                 // جدول عقد AST التي تحتاجها التعليمات وقت التنفيذ
    std::vector<ExprPtr> exprs;
    std::vector<StmtPtr> pinned;                // يثبّت الـ AST حيّاً (مفاتيح الكاش مؤشرات)
    bool ticks = false;                         // هل أُدرجت تعليمات Tick (ميزانية التنفيذ)
};
using ProtoPtr = std::shared_ptr<Proto>;

// ---------------------------------------------------------------------------
// المترجم: AST → Proto. لا يعتمد على Interpreter (يمكن ربطه منفرداً مع أدوات).
// ---------------------------------------------------------------------------
struct CompileOptions {
    bool ticks = false;       // أدرج Tick لكل عبارة (يلزم فقط عند تفعيل setExecutionBudget)
};

ProtoPtr compileBlock(const std::vector<StmtPtr>& stmts, const std::string& name, const CompileOptions& opt);
ProtoPtr compileStmt(const StmtPtr& stmt, const CompileOptions& opt);
ProtoPtr compileExpr(const ExprPtr& expr, const CompileOptions& opt);

// مفكّك: نص مقروء للبايتكود (لـ rinvm --disasm).
std::string disassemble(const Proto& p);

// إحصاءات تشغيل (اختيارية، لـ rinvm --stats).
struct Stats {
    uint64_t instructions = 0;
    uint64_t calls = 0;
    uint64_t nestedRuns = 0;
    uint64_t compiled = 0;
    uint64_t delegated = 0;
};

} // namespace rin::vm
