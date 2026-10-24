// ============================================================================
//  rin_bcvm_compiler.cpp — مترجم AST → بايتكود RinVM + المفكّك (disassembler)
// ============================================================================
//  يُضمَّن في نهاية rin_interpreter.cpp (نفس عُرف rin_set.cpp/rin_oop.cpp)، ويمكن أيضاً بناؤه منفرداً
//  لأدوات لا تحتاج التنفيذ (يعتمد على rin_vm.h وAST فقط).
//
//  قواعد الترجمة (كلها تحفظ دلالات المفسِّر الشجري حرفياً، بما فيها ترتيب التقييم):
//   - التعابير: كل ExprKind له ترجمة مباشرة؛ التعابير المعقّدة تستدعي نفس الدوال المساعدة للمفسّر.
//   - العبارات الأساسية (let/print/if/while/for/for-in/match/try/throw/return/break/continue/
//     fun/class/enum/block/plus.condition/achieve/let+ ...) تُترجَم إلى تعليمات أصلية.
//   - العبارات التصريحية (@container/@view/@save/@route/... والمتغيرات الحيّة...) تُترجَم إلى
//     ExecStmt: تفويض للـ runtime الأصلي لتلك العبارة وحدها، وكل كتلة/تعبير داخلها يعود للـ VM.
//   - الحلقات: علامات حلقة وقت التشغيل (LoopEnter/LoopSync/LoopLeave) تحمل أهداف break/continue
//     وارتفاعات المكدّس/البيئات/المعالجات، فيصحّ break/continue من أي عمق (حتى من تعبير goal).
// ============================================================================
#include "rin_bcvm.h"
#include <unordered_map>
#include <sstream>

namespace rin::vm {

const char* opName(Op op) {
    static const char* names[] = {
#define X(n) #n,
        RIN_VM_OPS(X)
#undef X
    };
    size_t i = static_cast<size_t>(op);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "?";
}

namespace detail {

class Compiler {
public:
    explicit Compiler(const CompileOptions& o) : opt_(o) {}

    ProtoPtr blockProto(const std::vector<StmtPtr>& stmts, const std::string& name) {
        begin(name);
        for (auto& s : stmts) stmt(s);
        emit(Op::End);
        return finish();
    }
    ProtoPtr stmtProto(const StmtPtr& s) {
        begin("<stmt>");
        stmt(s);
        emit(Op::End);
        P_->pinned.push_back(s);
        return finish();
    }
    ProtoPtr exprProto(const ExprPtr& e) {
        begin("<expr>");
        line_ = e ? e->line : 0;
        expr(e);
        emit(Op::ExprEnd);
        return finish();
    }

private:
    CompileOptions opt_;
    ProtoPtr P_;
    int line_ = 0;
    std::unordered_map<std::string, int> strIdx_;

    void begin(const std::string& name) {
        P_ = std::make_shared<Proto>();
        P_->name = name;
        P_->ticks = opt_.ticks;
        strIdx_.clear();
        line_ = 0;
    }
    ProtoPtr finish() { return std::move(P_); }

    // ---- أدوات إصدار ----
    int here() const { return static_cast<int>(P_->code.size()); }
    int emit(Op op, int a = 0, int b = 0, int c = 0) {
        P_->code.push_back(Instr{op, a, b, c});
        P_->lines.push_back(line_);
        return here() - 1;
    }
    int emitJump(Op op) { return emit(op, -1); }
    void patchHere(int at) { P_->code[static_cast<size_t>(at)].a = here(); }
    void patchBHere(int at) { P_->code[static_cast<size_t>(at)].b = here(); }
    int str(const std::string& s) {
        auto it = strIdx_.find(s);
        if (it != strIdx_.end()) return it->second;
        int i = static_cast<int>(P_->strs.size());
        P_->strs.push_back(s);
        strIdx_.emplace(s, i);
        return i;
    }
    int stmtRef(const StmtPtr& s) { P_->stmts.push_back(s); return static_cast<int>(P_->stmts.size()) - 1; }
    // true for `a` or `a.b.c...` (a chain of plain names): the only shapes that can denote a namespace path
    static bool isPathExpr(const ExprPtr& e) {
        for (const Expr* cur = e.get(); cur; ) {
            if (cur->exprKind == ExprKind::Variable) return true;
            if (cur->exprKind != ExprKind::Get) return false;
            cur = static_cast<const GetExpr*>(cur)->object.get();
        }
        return false;
    }

    int exprRef(const ExprPtr& e) { P_->exprs.push_back(e); return static_cast<int>(P_->exprs.size()) - 1; }
    int constNum(double d) {
        P_->consts.push_back(Value::num(d));
        return static_cast<int>(P_->consts.size()) - 1;
    }
    int constStr(const std::string& s) {
        P_->consts.push_back(Value::string(s));
        return static_cast<int>(P_->consts.size()) - 1;
    }
    void tick(const StmtPtr& s) {
        if (opt_.ticks) { line_ = s ? s->line : 0; emit(Op::Tick, s ? s->line : 0); }
    }

    // ====================================================================
    //  التعابير
    // ====================================================================
    static Op binOp(TokenType t, bool& ok) {
        ok = true;
        switch (t) {
            case TokenType::PLUS: return Op::Add;
            case TokenType::MINUS: return Op::Sub;
            case TokenType::STAR: return Op::Mul;
            case TokenType::SLASH: return Op::Div;
            case TokenType::PERCENT: return Op::Mod;
            case TokenType::GREATER: return Op::Gt;
            case TokenType::GREATER_EQUAL: return Op::Ge;
            case TokenType::LESS: return Op::Lt;
            case TokenType::LESS_EQUAL: return Op::Le;
            case TokenType::EQUAL_EQUAL: return Op::Eq;
            case TokenType::BANG_EQUAL: return Op::Ne;
            case TokenType::NOT_SAME: return Op::NotSame;
            case TokenType::COPY_OF: return Op::CopyOf;
            default: ok = false; return Op::Nop;
        }
    }

    void nilExpr() { emit(Op::Nil); }

    void expr(const ExprPtr& ep) {
        if (!ep) { nilExpr(); return; }
        const int savedLine = line_;
        line_ = ep->line;
        switch (ep->exprKind) {
        case ExprKind::Literal: {
            auto e = std::static_pointer_cast<LiteralExpr>(ep);
            switch (e->kind) {
                case LiteralExpr::Kind::NUMBER: emit(Op::Const, constNum(e->number)); break;
                case LiteralExpr::Kind::STRING: emit(Op::Const, constStr(e->str)); break;
                case LiteralExpr::Kind::BOOL: emit(e->boolean ? Op::True : Op::False); break;
                case LiteralExpr::Kind::NIL: emit(Op::Nil); break;
            }
            break;
        }
        case ExprKind::Lambda:
            emit(Op::MakeClosure, exprRef(ep));
            break;
        case ExprKind::Variable: {
            auto e = std::static_pointer_cast<VariableExpr>(ep);
            emit(Op::LoadVar, str(e->name), e->line);
            break;
        }
        case ExprKind::Assign: {
            auto e = std::static_pointer_cast<AssignExpr>(ep);
            expr(e->value);
            emit(Op::AssignVar, str(e->name), e->line);
            break;
        }
        case ExprKind::PatternAssign:
            emit(Op::PatternAssign, exprRef(ep));
            break;
        case ExprKind::Conditional: {
            auto e = std::static_pointer_cast<ConditionalExpr>(ep);
            expr(e->condition);
            int jf = emitJump(Op::JmpFalse);
            expr(e->whenTrue);
            int je = emitJump(Op::Jmp);
            patchHere(jf);
            expr(e->whenFalse);
            patchHere(je);
            break;
        }
        case ExprKind::Goal: {
            auto e = std::static_pointer_cast<GoalExpr>(ep);
            int gb = emit(Op::GoalBegin, -1);
            emit(Op::PushScope);
            for (auto& s : e->body->statements) stmt(s);
            emit(Op::PopScope, 1);
            emit(Op::GoalEnd);
            emit(Op::Nil);
            patchHere(gb);
            break;
        }
        case ExprKind::Logical: {
            auto e = std::static_pointer_cast<LogicalExpr>(ep);
            expr(e->left);
            int j = emitJump(e->op == TokenType::OR ? Op::JmpTrueKeep : Op::JmpFalseKeep);
            expr(e->right);
            patchHere(j);
            break;
        }
        case ExprKind::Unary: {
            auto e = std::static_pointer_cast<UnaryExpr>(ep);
            expr(e->right);
            if (e->op == TokenType::MINUS) emit(Op::Neg, e->line);
            else if (e->op == TokenType::BANG) emit(Op::Not);
            else emit(Op::Neg, e->line); // لا توجد عوامل أحادية أخرى في الـ parser
            break;
        }
        case ExprKind::Binary: {
            auto e = std::static_pointer_cast<BinaryExpr>(ep);
            bool ok = false;
            Op op = binOp(e->op, ok);
            expr(e->left);
            expr(e->right);
            if (ok) emit(op, e->line);
            else { emit(Op::Pop); emit(Op::Pop); emit(Op::Nil); } // لا يحدث: الـ parser لا يُنتج غير ما سبق
            break;
        }
        case ExprKind::Call: {
            auto e = std::static_pointer_cast<CallExpr>(ep);
            int pf = -1;
            if (e->isPipelineRoot) pf = emit(Op::PipeFlow, exprRef(ep), -1);
            for (auto& a : e->args) expr(a);
            static const char* kBuiltinOps[] = {"Addition", "Subtraction", "Multiplication", "Equal"};
            bool eligible = e->callee.find('.') == std::string::npos &&
                            e->callee != "libraryImport" && e->callee != "libraryImportUrl";
            for (auto* b : kBuiltinOps) if (e->callee == b) eligible = false;
            line_ = e->line;
            emit(Op::CallNamed, str(e->callee),
                 static_cast<int>(e->args.size()) | ((eligible ? kCallFlagEligible : 0) << 16), e->line);
            if (pf >= 0) patchBHere(pf);
            break;
        }
        case ExprKind::CallValue: {
            auto e = std::static_pointer_cast<CallValueExpr>(ep);
            expr(e->callee);
            for (auto& a : e->args) expr(a);
            emit(Op::CallValue, static_cast<int>(e->args.size()), e->line);
            break;
        }
        case ExprKind::Array: {
            auto e = std::static_pointer_cast<ArrayExpr>(ep);
            for (auto& el : e->elements) expr(el);
            emit(Op::MakeArray, static_cast<int>(e->elements.size()));
            break;
        }
        case ExprKind::Map: {
            auto e = std::static_pointer_cast<MapExpr>(ep);
            for (auto& en : e->entries) { expr(en.key); expr(en.value); }
            emit(Op::MakeMap, static_cast<int>(e->entries.size()));
            break;
        }
        case ExprKind::Index: {
            auto e = std::static_pointer_cast<IndexExpr>(ep);
            expr(e->object);
            expr(e->index);
            emit(Op::Index, e->line);
            break;
        }
        case ExprKind::IndexSet: {
            auto e = std::static_pointer_cast<IndexSetExpr>(ep);
            emit(Op::LiveGuard, exprRef(e->object), e->line);
            expr(e->object);
            expr(e->index);
            expr(e->value);
            emit(Op::IndexSet, e->line);
            break;
        }
        case ExprKind::Get: {
            auto e = std::static_pointer_cast<GetExpr>(ep);
            // Namespaces (container/group/Volume): a.b / a.b.c may name a namespace member. Resolved at
            // run time (cheap no-op when no namespace exists); otherwise the normal code below runs.
            int nsJump = -1;
            if (isPathExpr(e->object) && !(e->object->exprKind == ExprKind::Variable &&
                                           std::static_pointer_cast<VariableExpr>(e->object)->name == "super")) {
                nsJump = emit(Op::NsGet, exprRef(ep), -1, e->line);
            }
            if (e->object && e->object->exprKind == ExprKind::Variable) {
                auto ve = std::static_pointer_cast<VariableExpr>(e->object);
                if (ve->name == "super") emit(Op::GetSuper, str(e->name), 0, e->line);
                else { line_ = ve->line; emit(Op::GetVarProp, str(e->name), str(ve->name), e->line); line_ = e->line; }
            } else {
                expr(e->object);
                emit(Op::GetProp, str(e->name), e->line);
            }
            if (nsJump >= 0) patchBHere(nsJump);
            break;
        }
        case ExprKind::Set: {
            auto e = std::static_pointer_cast<SetExpr>(ep);
            emit(Op::LiveGuard, exprRef(e->object), e->line);
            int nsEnd = -1;
            if (isPathExpr(e->object)) { // namespace.field = v  (see NsGet above)
                int nt = emit(Op::NsSetTest, exprRef(ep), -1, e->line);
                expr(e->value);
                emit(Op::NsSet, exprRef(ep), 0, e->line);
                nsEnd = emitJump(Op::Jmp);
                P_->code[static_cast<size_t>(nt)].b = here();
            }
            if (e->object && e->object->exprKind == ExprKind::Variable) {
                auto sve = std::static_pointer_cast<VariableExpr>(e->object);
                int test = emit(Op::StaticSetTest, str(sve->name), -1);
                expr(e->object);
                expr(e->value);
                emit(Op::SetProp, str(e->name), e->line);
                int jend = emitJump(Op::Jmp);
                P_->code[static_cast<size_t>(test)].b = here();
                expr(e->value);
                emit(Op::StaticSet, str(sve->name), str(e->name), e->line);
                patchHere(jend);
            } else {
                expr(e->object);
                expr(e->value);
                emit(Op::SetProp, str(e->name), e->line);
            }
            if (nsEnd >= 0) patchHere(nsEnd);
            break;
        }
        case ExprKind::MethodCall: {
            auto e = std::static_pointer_cast<MethodCallExpr>(ep);
            bool isSuper = e->object && e->object->exprKind == ExprKind::Variable &&
                           std::static_pointer_cast<VariableExpr>(e->object)->name == "super";
            if (isSuper) {
                for (auto& a : e->args) expr(a);
                emit(Op::CallSuper, str(e->method), static_cast<int>(e->args.size()), e->line);
            } else {
                expr(e->object);
                for (auto& a : e->args) expr(a);
                emit(Op::CallMethod, str(e->method), static_cast<int>(e->args.size()), e->line);
            }
            break;
        }
        default:
            // نوع تعبير غير معروف: يُقيَّم nil كما يفعل المفسّر في فرعه الاحتياطي.
            emit(Op::Nil);
            break;
        }
        line_ = savedLine;
    }

    // ====================================================================
    //  print: ترتيب التقييم والفحوصات مطابق لـ Interpreter::exec(PrintStmt)
    // ====================================================================
    void printStmt(const std::shared_ptr<PrintStmt>& s) {
        int jIf = -1;
        if (s->ifCond) { expr(s->ifCond); jIf = emitJump(Op::JmpFalse); }
        const int L = s->line;
        // sep
        if (s->sep) { expr(s->sep); emit(Op::CheckStr, str("'print': 'sep' يجب أن يكون نصاً (string)، لكن وُجد نوع "), L); }
        else emit(Op::Const, constStr(" "));
        // end
        if (s->end) { expr(s->end); emit(Op::CheckStr, str("'print': 'end' يجب أن يكون نصاً (string)، لكن وُجد نوع "), L); }
        else emit(Op::Const, constStr("\n"));
        // pretty
        if (s->pretty) { expr(s->pretty); emit(Op::Truthy); }
        else emit(Op::False);
        // exprs
        for (size_t i = 0; i < s->exprs.size(); i++) {
            expr(s->exprs[i]);
            emit(Op::DisplayP, static_cast<int>(i) + 1);
        }
        // upper / lower
        if (s->upper) { expr(s->upper); emit(Op::Truthy); }
        if (s->lower) { expr(s->lower); emit(Op::Truthy); }
        if (s->upper && s->lower) emit(Op::CheckUL, L);
        // width / align
        if (s->width) {
            expr(s->width);
            emit(Op::CheckNum, str("'print': 'width' يجب أن يكون رقماً، لكن وُجد نوع "), L);
            if (s->align) {
                expr(s->align);
                emit(Op::CheckStr, str("'print': 'align' يجب أن يكون نصاً، لكن وُجد نوع "), L);
                emit(Op::CheckAlign, L);
            }
        } else if (s->align) {
            emit(Op::Fail, str("'print': 'align' يتطلّب تحديد 'width' معه"), L,
                 static_cast<int>(diag::Code::E0035_RuntimeError));
        }
        // level / label
        if (s->level) {
            expr(s->level);
            emit(Op::CheckStr, str("'print': 'level' يجب أن يكون نصاً، لكن وُجد نوع "), L);
            emit(Op::CheckLevel, L);
        }
        if (s->label) {
            expr(s->label);
            emit(Op::CheckStr, str("'print': 'label' يجب أن يكون نصاً، لكن وُجد نوع "), L);
        }
        // repeat
        if (s->repeatN) {
            expr(s->repeatN);
            emit(Op::CheckNum, str("'print': 'repeat' يجب أن يكون رقماً، لكن وُجد نوع "), L);
            emit(Op::CheckRepeat, L);
        }
        if (s->fx) expr(s->fx);   // سمات الخط: آخر قيمة على المكدس
        emit(Op::PrintEmit, stmtRef(s));
        if (jIf >= 0) patchHere(jIf);
    }

    // ====================================================================
    //  العبارات
    // ====================================================================
    void delegate(const StmtPtr& s) { emit(Op::ExecStmt, stmtRef(s)); }

    void stmt(const StmtPtr& sp) {
        if (!sp) return;
        line_ = sp->line;
        switch (sp->stmtKind) {
        case StmtKind::ExpressionStmt: {
            auto s = std::static_pointer_cast<ExpressionStmt>(sp);
            tick(sp);
            if (s->expr && s->expr->exprKind == ExprKind::Assign) {
                int sk = emit(Op::AssignAppend, exprRef(s->expr), -1);
                expr(s->expr);
                emit(Op::Pop);
                patchBHere(sk);
            } else {
                expr(s->expr);
                emit(Op::Pop);
            }
            break;
        }
        case StmtKind::PrintStmt:
            tick(sp);
            printStmt(std::static_pointer_cast<PrintStmt>(sp));
            break;
        case StmtKind::LetStmt: {
            auto s = std::static_pointer_cast<LetStmt>(sp);
            tick(sp);
            if (s->initializer) expr(s->initializer); else emit(Op::Nil);
            line_ = s->line;
            emit(Op::Let, stmtRef(sp));
            break;
        }
        case StmtKind::LetPatternStmt:
            tick(sp);
            emit(Op::LetPattern, stmtRef(sp));
            break;
        case StmtKind::LetGroupStmt: {
            tick(sp);
            for (auto& it : std::static_pointer_cast<LetGroupStmt>(sp)->items) stmt(it);
            break;
        }
        case StmtKind::ClassStmt:
            tick(sp);
            emit(Op::ClassDef, stmtRef(sp));
            break;
        case StmtKind::EnumStmt: {
            auto s = std::static_pointer_cast<EnumStmt>(sp);
            tick(sp);
            for (auto& c : s->cases) { if (c.value) expr(c.value); else emit(Op::Nil); }
            line_ = s->line;
            emit(Op::EnumDef, stmtRef(sp));
            break;
        }
        case StmtKind::WarpStmt: {
            auto s = std::static_pointer_cast<WarpStmt>(sp);
            tick(sp);
            if (s->initializer) expr(s->initializer); else emit(Op::Nil);
            emit(Op::DefineVar, str(s->name));
            break;
        }
        case StmtKind::BlockStmt: {
            auto s = std::static_pointer_cast<BlockStmt>(sp);
            tick(sp);
            emit(Op::PushScope);
            for (auto& x : s->statements) stmt(x);
            emit(Op::PopScope, 1);
            break;
        }
        case StmtKind::TryCatchStmt: {
            auto s = std::static_pointer_cast<TryCatchStmt>(sp);
            // Scope Guarantees (finally / defer / catch-when / rethrow / try بلا catch): تُنفَّذ بالمفسِّر مباشرة.
            if (s->needsInterp || s->finallyBranch || s->catchGuard || !s->moreCatches.empty() || !s->catchBranch) {
                delegate(sp);
                break;
            }
            tick(sp);
            int tb = emit(Op::TryBegin, -1);
            stmt(s->tryBranch);
            emit(Op::TryEnd);
            int jend = emitJump(Op::Jmp);
            patchHere(tb);
            emit(Op::PushScope);
            emit(Op::CatchBind, s->catchName.empty() ? -1 : str(s->catchName));
            stmt(s->catchBranch);
            emit(Op::PopScope, 1);
            patchHere(jend);
            break;
        }
        case StmtKind::ThrowStmt: {
            auto s = std::static_pointer_cast<ThrowStmt>(sp);
            if (s->rethrow) { delegate(sp); break; }
            tick(sp);
            if (s->value) expr(s->value); else emit(Op::Nil);
            line_ = s->line;
            emit(Op::Throw, s->line);
            break;
        }
        case StmtKind::IfStmt: {
            auto s = std::static_pointer_cast<IfStmt>(sp);
            tick(sp);
            expr(s->condition);
            int jf = emitJump(Op::JmpFalse);
            stmt(s->thenBranch);
            if (s->elseBranch) {
                int je = emitJump(Op::Jmp);
                patchHere(jf);
                stmt(s->elseBranch);
                patchHere(je);
            } else {
                patchHere(jf);
            }
            break;
        }
        case StmtKind::WhileStmt: {
            auto s = std::static_pointer_cast<WhileStmt>(sp);
            tick(sp);
            int le = emit(Op::LoopEnter, -1, -1);
            int lcond = here();
            expr(s->condition);
            int jf = emitJump(Op::JmpFalse);
            stmt(s->body);
            emit(Op::Jmp, lcond);
            patchHere(jf);                       // Lexit
            emit(Op::LoopLeave);
            if (s->doneBlock) stmt(s->doneBlock);
            int jend = emitJump(Op::Jmp);
            int lbreak = here();
            emit(Op::LoopLeave);
            patchHere(jend);
            P_->code[static_cast<size_t>(le)].a = lbreak;
            P_->code[static_cast<size_t>(le)].b = lcond;
            break;
        }
        case StmtKind::ForStmt: {
            auto s = std::static_pointer_cast<ForStmt>(sp);
            tick(sp);
            emit(Op::PushScope);
            if (s->initializer) stmt(s->initializer);
            int le = emit(Op::LoopEnter, -1, -1);
            int lcond = here();
            int jf = -1;
            if (s->condition) { expr(s->condition); jf = emitJump(Op::JmpFalse); }
            emit(Op::ForIterBegin);
            emit(Op::LoopSync);
            stmt(s->body);
            int lcont = here();
            emit(Op::ForIterEnd);
            if (s->increment) { expr(s->increment); emit(Op::Pop); }
            emit(Op::Jmp, lcond);
            if (jf >= 0) patchHere(jf);          // Lexit
            emit(Op::LoopLeave);
            emit(Op::PopScope, 1);
            if (s->doneBlock) stmt(s->doneBlock);
            int jend = emitJump(Op::Jmp);
            int lbreak = here();
            emit(Op::ForIterEnd);
            emit(Op::LoopLeave);
            emit(Op::PopScope, 1);
            patchHere(jend);
            P_->code[static_cast<size_t>(le)].a = lbreak;
            P_->code[static_cast<size_t>(le)].b = lcont;
            break;
        }
        case StmtKind::ForInStmt: {
            auto s = std::static_pointer_cast<ForInStmt>(sp);
            tick(sp);
            expr(s->iterable);
            emit(Op::ForInPrep, s->line);
            int le = emit(Op::LoopEnter, -1, -1);
            int lnext = here();
            int fn = emit(Op::ForInNext, -1, stmtRef(sp));
            emit(Op::LoopSync);
            stmt(s->body);
            int lcont = here();
            emit(Op::PopScope, 1);
            emit(Op::Jmp, lnext);
            patchHere(fn);                       // Lexit
            emit(Op::LoopLeave);
            emit(Op::PopN, 2);
            if (s->doneBlock) stmt(s->doneBlock);
            int jend = emitJump(Op::Jmp);
            int lbreak = here();
            emit(Op::PopScope, 1);
            emit(Op::LoopLeave);
            emit(Op::PopN, 2);
            patchHere(jend);
            P_->code[static_cast<size_t>(le)].a = lbreak;
            P_->code[static_cast<size_t>(le)].b = lcont;
            break;
        }
        case StmtKind::PlusConditionStmt: {
            auto s = std::static_pointer_cast<PlusConditionStmt>(sp);
            tick(sp);
            expr(s->condition);
            int jf = emitJump(Op::JmpFalse);
            stmt(s->trueBranch);
            int je = emitJump(Op::Jmp);
            patchHere(jf);
            stmt(s->falseBranch);
            patchHere(je);
            break;
        }
        case StmtKind::MatchStmt: {
            auto s = std::static_pointer_cast<MatchStmt>(sp);
            tick(sp);
            expr(s->subject);
            std::vector<int> endJumps;
            for (auto& mc : s->cases) {
                std::vector<int> matched;
                for (auto& v : mc.values) {
                    expr(v);
                    emit(Op::MatchTest);
                    matched.push_back(emitJump(Op::JmpTrue));
                }
                int toNext = emitJump(Op::Jmp);
                for (int m : matched) patchHere(m);
                int guardJump = -1;
                if (mc.guard) { expr(mc.guard); guardJump = emitJump(Op::JmpFalse); }
                emit(Op::Pop);                   // subject
                emit(Op::PushScope);
                if (mc.body) for (auto& x : mc.body->statements) stmt(x);
                emit(Op::PopScope, 1);
                endJumps.push_back(emitJump(Op::Jmp));
                patchHere(toNext);
                if (guardJump >= 0) patchHere(guardJump);
            }
            emit(Op::Pop);                       // subject (لم تُطابق أي حالة)
            if (s->elseBranch) {
                emit(Op::PushScope);
                for (auto& x : s->elseBranch->statements) stmt(x);
                emit(Op::PopScope, 1);
            }
            for (int j : endJumps) patchHere(j);
            break;
        }
        case StmtKind::AchieveStmt: {
            auto s = std::static_pointer_cast<AchieveStmt>(sp);
            tick(sp);
            if (s->value) expr(s->value); else emit(Op::Nil);
            emit(Op::Achieve);
            break;
        }
        case StmtKind::FunctionStmt:
            tick(sp);
            emit(Op::FuncDef, stmtRef(sp));
            break;
        case StmtKind::ReturnStmt: {
            auto s = std::static_pointer_cast<ReturnStmt>(sp);
            tick(sp);
            if (s->value) expr(s->value); else emit(Op::Nil);
            emit(Op::Return);
            break;
        }
        case StmtKind::BreakStmt:
            tick(sp);
            emit(Op::Break);
            break;
        case StmtKind::ContinueStmt:
            tick(sp);
            emit(Op::Continue);
            break;
        default:
            // العبارات التصريحية/الحيّة/الواجهات ...: تفويض للـ runtime (ExecStmt لا يضيف Tick لأن exec() يعدّ بنفسه).
            delegate(sp);
            break;
        }
    }
};

} // namespace detail

ProtoPtr compileBlock(const std::vector<StmtPtr>& stmts, const std::string& name, const CompileOptions& opt) {
    detail::Compiler c(opt);
    auto p = c.blockProto(stmts, name);
    p->pinned.insert(p->pinned.end(), stmts.begin(), stmts.end());
    return p;
}
ProtoPtr compileStmt(const StmtPtr& s, const CompileOptions& opt) {
    detail::Compiler c(opt);
    return c.stmtProto(s);
}
ProtoPtr compileExpr(const ExprPtr& e, const CompileOptions& opt) {
    detail::Compiler c(opt);
    auto p = c.exprProto(e);
    return p;
}

// ---------------------------------------------------------------------------
//  المفكّك
// ---------------------------------------------------------------------------
std::string disassemble(const Proto& p) {
    std::ostringstream os;
    os << "== " << p.name << "  (" << p.code.size() << " instr, " << p.consts.size() << " consts, "
       << p.strs.size() << " names)\n";
    auto strOf = [&](int i) -> std::string {
        return (i >= 0 && static_cast<size_t>(i) < p.strs.size()) ? "'" + p.strs[static_cast<size_t>(i)] + "'" : "<?>";
    };
    for (size_t i = 0; i < p.code.size(); i++) {
        const Instr& in = p.code[i];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%05zu  L%-4d ", i, i < p.lines.size() ? p.lines[i] : 0);
        os << buf << opName(in.op);
        switch (in.op) {
            case Op::Const:
                if (in.a >= 0 && static_cast<size_t>(in.a) < p.consts.size()) {
                    const Value& v = p.consts[static_cast<size_t>(in.a)];
                    os << "  " << (v.type == Value::Type::STRING ? "\"" + v.str + "\"" : v.toDisplayString());
                }
                break;
            case Op::LoadVar: case Op::DefineVar: case Op::AssignVar: os << "  " << strOf(in.a); break;
            case Op::GetProp: case Op::SetProp: os << "  ." << strOf(in.a); break;
            case Op::GetVarProp: os << "  " << strOf(in.b) << "." << strOf(in.a); break;
            case Op::GetSuper: os << "  super." << strOf(in.a); break;
            case Op::StaticSetTest: os << "  " << strOf(in.a) << " -> " << in.b; break;
            case Op::StaticSet: os << "  " << strOf(in.a) << "." << strOf(in.b); break;
            case Op::CallNamed: os << "  " << strOf(in.a) << "  argc=" << (in.b & 0xFFFF) << ((in.b >> 16) ? " [vm-call]" : ""); break;
            case Op::CallMethod: case Op::CallSuper: os << "  " << strOf(in.a) << "  argc=" << in.b; break;
            case Op::CallValue: os << "  argc=" << in.a; break;
            case Op::Jmp: case Op::JmpFalse: case Op::JmpTrue: case Op::JmpFalseKeep: case Op::JmpTrueKeep:
            case Op::TryBegin: case Op::GoalBegin:
                os << "  -> " << in.a; break;
            case Op::AssignAppend: os << "  skip -> " << in.b; break;
            case Op::PipeFlow: os << "  flow-end -> " << in.b; break;
            case Op::LoopEnter: os << "  break -> " << in.a << "  continue -> " << in.b; break;
            case Op::ForInNext: os << "  exit -> " << in.a; break;
            case Op::MakeArray: case Op::MakeMap: case Op::PopN: case Op::PopScope: os << "  " << in.a; break;
            case Op::CatchBind: if (in.a >= 0) os << "  " << strOf(in.a); break;
            case Op::DisplayP: os << "  pretty@-" << in.a; break;
            case Op::CheckStr: case Op::CheckNum: case Op::Fail: os << "  " << strOf(in.a); break;
            case Op::Tick: os << "  L" << in.a; break;
            case Op::Let: case Op::ClassDef: case Op::EnumDef: case Op::FuncDef: case Op::LetPattern: case Op::ExecStmt:
                if (in.a >= 0 && static_cast<size_t>(in.a) < p.stmts.size() && p.stmts[static_cast<size_t>(in.a)]) {
                    os << "  stmt#" << in.a << " (line " << p.stmts[static_cast<size_t>(in.a)]->line << ")";
                }
                break;
            default: break;
        }
        os << "\n";
    }
    return os.str();
}

} // namespace rin::vm
