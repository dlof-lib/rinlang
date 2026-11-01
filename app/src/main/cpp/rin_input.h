// ============================================================================
//  rin_input.h — Rin 1.0: محرّك نماذج الإدخال (مفسِّر + مترجم + منفِّذ) — ملف مخصّص لـ input
// ----------------------------------------------------------------------------
//  لا دوال ولا مفاهيم جديدة في اللغة: الدوال العامة تبقى input / inputNumber / confirm / choose.
//  ما يضيفه هذا المحرّك هو *ميزات* لوضع النموذج (input(prompt, target[, schema])) الذي يربط
//  الإدخال بالحاويات وكائنات OOP والقواميس:
//
//    1) نموذج بمعاملة (transaction): كل ما كُتب في الهدف يُسجَّل في دفتر (journal)، وإن أُلغي النموذج
//       أو فشل مُدقِّق أو رمى أي خطأ، يُعاد الهدف كما كان (حاوية/كائن/قاموس) — لا نصف-نموذج أبداً.
//    2) نماذج متداخلة: مواصفة الحقل في المخطّط يمكن أن تكون *كائناً* (OOP: تركيب) فيُملأ في مكانه،
//       أو *اسم حاوية* فتُملأ ويُسلَّم لقطتها (container.snapshot) إلى الحقل الأب.
//    3) مُدقِّق للنموذج كله (cross-field): input(prompt, validator, target[, schema]) — يستلم الناتج
//       النهائي (لقطة الحاوية / الكائن / القاموس)؛ true يقبل، نص = سبب الرفض فيُعاد السؤال (حتى 5 جولات).
//    4) فحص مسبق (preflight): حاوية مقفلة (container.isLocked)، كائن مجمَّد (oop.isFrozen)، دورة في
//       الهدف المتداخل، عمق زائد — كلها تُرفض *قبل* السؤال الأول لا بعد أن يكتب المستخدم إجاباته.
//
//  المعمارية (ثلاث مراحل، كل مرحلة صنف مستقل قابل للاختبار بلا واجهة):
//
//     وسائط Rin ──► FormInterpreter ──► Request (خطة شجرية FormPlan)
//                   (يحلّل ويفحص مسبقاً)
//     Request   ──► FormCompiler    ──► Program (تعليمات مسطّحة + جداول ثوابت؛ disassemble() للتشخيص)
//     Program   ──► FormExecutor    ──► نتيجة   (يشغّل التعليمات + دفتر المعاملة + جولات المُدقِّق)
//
//  كل سؤال فعلي يُفوَّض إلى نفس الدوال الأصلية (input/inputNumber/confirm/choose)، وكل كتابة تمرّ عبر
//  setState/oop.set؛ لذلك تُحترم الصلاحيات و`set` والتجميد والمراقبون وروابط OOP/Indsin بلا أي كود خاص.
//
//  يُضمَّن rin_input.cpp في نهاية rin_interpreter.cpp (بنفس أسلوب rin_oop.cpp) فلا تغيير في CMake/Gradle.
// ============================================================================
#pragma once
#include "rin_interpreter.h"
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rin {
namespace inputx {

// ---------------------------------------------------------------------------
// Host: الجسر الوحيد بين المحرّك والـ Interpreter — كل ما يلزم من اللغة يُستدعى من هنا،
// فتبقى الأصناف الثلاثة أدناه بلا وصول إلى أعضاء Interpreter الخاصة (وقابلة للاختبار بمضيف وهمي).
// ---------------------------------------------------------------------------
struct Host {
    std::function<Value(const char*, std::vector<Value>, int)> call;          // دالة مبنية بالاسم (كـ name(args) في Rin)
    std::function<bool(const std::string&)> hasContainer;                       // هل هذه حاوية معروفة؟
    std::function<bool(const Value&)> isCallable;                               // fun أو كائن يعرّف __call__
    std::function<Value(const Value&, std::vector<Value>, int)> invoke;         // callValue
    std::function<RinError(int, const std::string&, const std::string&)> fail;  // خطأ تشغيل (سطر، رسالة، سبب)
    std::function<bool()> atEof;                                                // هل انتهى الإدخال في آخر سؤال؟
};

// ---------------------------------------------------------------------------
// 1) الخطة (ناتج المفسِّر)
// ---------------------------------------------------------------------------
enum class TargetKind { Container, Object, Map };
enum class FieldKind { Text, Number, Bool, Choice, NestedObject, NestedContainer };

struct FormPlan;
struct FieldPlan {
    std::string name;
    FieldKind kind = FieldKind::Text;
    Value spec;                          // Choice: الخيارات · NestedObject: الكائن · NestedContainer: اسم الحاوية
    bool hasValidator = false;           // مُدقِّق الحقل (من المخطّط)
    Value validator;
    std::shared_ptr<FormPlan> nested;    // الخطة الفرعية للأنواع المتداخلة
    bool commitNested = false;           // هل يُسلَّم ناتج الفرعي إلى الحقل الأب (حاوية دائماً، كائن إن اختلف عن الحالي)
};

struct FormPlan {
    TargetKind kind = TargetKind::Map;
    Value target;                        // نص (اسم حاوية) | كائن | قاموس
    std::string prompt;                  // بادئة الأسئلة (تتسلسل في المتداخل: "P> home.")
    std::vector<FieldPlan> fields;
};

struct Request {
    std::shared_ptr<FormPlan> root;
    bool hasCheck = false;               // مُدقِّق النموذج كله
    Value check;
    int line = 0;
};

// ---------------------------------------------------------------------------
// 2) البرنامج (ناتج المترجم)
// ---------------------------------------------------------------------------
enum class Op : uint8_t { Enter, Leave, AskText, AskNumber, AskBool, AskChoice, Commit };

struct Instr {
    Op op = Op::Enter;
    int scope = -1;    // فهرس النطاق (الهدف) في Program::scopes
    int name = -1;     // اسم الحقل في Program::strings
    int label = -1;    // نص السؤال في Program::strings
    int valid = -1;    // مُدقِّق الحقل في Program::consts
    int spec = -1;     // الخيارات في Program::consts
    int src = -1;      // Commit: النطاق الفرعي الذي يُؤخذ ناتجه
};

struct Scope {
    TargetKind kind = TargetKind::Map;
    Value target;
    std::string path;  // "" للجذر
    int parent = -1;
};

struct Program {
    std::vector<Instr> code;
    std::vector<Scope> scopes;
    std::vector<std::string> strings;
    std::vector<Value> consts;
    bool hasCheck = false;
    int checkConst = -1;
    int line = 0;

    // نص مقروء للتعليمات (للاختبار والتشخيص من C++ فقط — ليس دالة في اللغة).
    std::string disassemble() const;
    size_t askCount() const;
};

// ---------------------------------------------------------------------------
// 3) الأصناف الثلاثة
// ---------------------------------------------------------------------------
class FormInterpreter {
public:
    explicit FormInterpreter(const Host& h) : h_(h) {}
    // هل هذا النداء لـ input() وضع نموذج؟ (نفس شروط parseArgs القديمة: هدف بلا key، مع مُدقِّق اختياري ومخطّط اختياري)
    bool recognize(const std::vector<Value>& a) const;
    // يحلّل الوسائط إلى خطة شجرية، ويرفض كل ما يمكن رفضه قبل السؤال الأول.
    Request analyze(const std::vector<Value>& a, int line) const;

private:
    static constexpr int kMaxDepth = 6;
    struct Visited { std::set<const void*> objects; std::set<std::string> containers; };
    bool isTarget(const Value& v) const;
    std::shared_ptr<FormPlan> planScope(const Value& target, const std::string& prompt, const Value* schema,
                                        const std::string& path, int depth, Visited& vis, int line) const;
    const Host& h_;
};

class FormCompiler {
public:
    Program compile(const Request& rq) const;
private:
    int emitScope(const FormPlan& plan, int parent, const std::string& path, Program& p) const;
    static int str(Program& p, const std::string& s);
    static int cst(Program& p, const Value& v);
};

class FormExecutor {
public:
    explicit FormExecutor(const Host& h) : h_(h) {}
    Value run(const Program& p);
    static constexpr int kMaxRounds = 5;

private:
    struct Entry { int scope; std::string key; Value before; bool existed; };
    Value read(const Scope& s, const std::string& key, bool& existed);
    void write(const Scope& s, const std::string& key, const Value& v);
    void journal(const Program& p, int scope, const std::string& key);
    void rollback(const Program& p);
    void pass(const Program& p);
    Value subject(const Program& p);
    const Host& h_;
    std::vector<Entry> journal_;
    std::set<std::pair<int, std::string>> seen_;
    std::string note_;
    int line_ = 0;
};

// الواجهة الوحيدة التي يراها Interpreter.
class FormEngine {
public:
    explicit FormEngine(const Host& h) : host_(h), interp_(host_), exec_(host_) {}
    bool recognize(const std::vector<Value>& a) const { return interp_.recognize(a); }
    Value run(const std::vector<Value>& a, int line);
    const Program& lastProgram() const { return last_; }
private:
    Host host_;
    FormInterpreter interp_;
    FormCompiler comp_;
    FormExecutor exec_;
    Program last_;
};

} // namespace inputx
} // namespace rin
