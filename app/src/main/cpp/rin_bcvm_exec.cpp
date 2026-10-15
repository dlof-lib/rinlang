// ============================================================================
//  rin_bcvm_exec.cpp — آلة RinVM: حلقة التنفيذ (تُضمَّن في نهاية rin_interpreter.cpp)
// ============================================================================
//  أعضاء Interpreter فعلياً، فتصل لكل الدوال المساعدة الخاصة والدوال الحرة الساكنة في
//  rin_interpreter.cpp (prettyPrintValue/requireNumbers/sameReference/diagErr ...) وتستدعيها بنفس
//  الترتيب والرسائل التي يستعملها المفسِّر الشجري، فتتطابق الدلالات حرفياً.
// ============================================================================
#include "rin_bcvm.h"
#include <cstdlib>
#include <set>
#include <functional>

namespace rin {
namespace vm {

struct Frame {
    ProtoPtr proto;
    uint32_t pc = 0;
    uint32_t stackBase = 0;
    uint32_t envBase = 0;
    uint32_t handlerBase = 0;
    uint32_t loopBase = 0;
    std::shared_ptr<Callable> fn;   // null = الإطار القاعدي لهذا التشغيل
    int callLine = 0;
};
struct Handler {
    bool isGoal = false;
    uint32_t target = 0, frameIdx = 0, stack = 0, env = 0, loops = 0;
};
struct LoopMark {
    uint32_t breakPC = 0, contPC = 0, stack = 0, env = 0, handlers = 0;
};
struct RunState {
    std::vector<Value> stack;
    std::vector<EnvPtr> envs;
    std::vector<Frame> frames;
    std::vector<Handler> handlers;
    std::vector<LoopMark> loops;
    void clear() { stack.clear(); envs.clear(); frames.clear(); handlers.clear(); loops.clear(); }
};
struct State {
    std::unordered_map<const void*, ProtoPtr> blocks;   // مفتاح: data() لمتجه العبارات (يُتحقَّق بأول عنصر)
    std::unordered_map<const void*, ProtoPtr> stmts;
    std::unordered_map<const void*, ProtoPtr> exprs;
    std::vector<std::unique_ptr<RunState>> pool;
    size_t depth = 0;
    bool ticks = false;
    Stats stats;
};

} // namespace vm

using vm::Op;
using vm::Instr;

namespace {
struct RunHolder {
    vm::State& S;
    vm::RunState& rs;
    explicit RunHolder(vm::State& s) : S(s), rs(acquire(s)) {}
    ~RunHolder() { rs.clear(); S.depth--; }
    static vm::RunState& acquire(vm::State& s) {
        if (s.depth == s.pool.size()) {
            s.pool.push_back(std::make_unique<vm::RunState>());
            s.pool.back()->stack.reserve(64);
        }
        return *s.pool[s.depth++];
    }
};
} // namespace

std::shared_ptr<vm::State> Interpreter::vmState() {
    if (!vm_) vm_ = std::make_shared<vm::State>();
    return vm_;
}
const vm::Stats* Interpreter::vmStats() const { return vm_ ? &vm_->stats : nullptr; }
void Interpreter::vmFlushCache() {
    if (!vm_) return;
    vm_->blocks.clear(); vm_->stmts.clear(); vm_->exprs.clear();
}

// يتأكد أن إعداد Tick يطابق ميزانية التنفيذ الحالية؛ وإلا يُفرَّغ الكاش.
static inline vm::CompileOptions vmOptions(vm::State& S, long long budget) {
    bool t = budget > 0;
    if (S.ticks != t) {
        S.blocks.clear(); S.stmts.clear(); S.exprs.clear();
        S.ticks = t;
    }
    vm::CompileOptions o; o.ticks = t; return o;
}

vm::ProtoPtr Interpreter::vmBlockProto(const std::vector<StmtPtr>& statements) {
    auto Sp = vmState(); auto& S = *Sp;
    auto opt = vmOptions(S, execBudget_);
    const void* key = statements.data();
    auto it = S.blocks.find(key);
    if (it != S.blocks.end() && it->second->pinned.size() == statements.size() &&
        (statements.empty() || it->second->pinned[0].get() == statements[0].get()) &&
        (statements.size() < 2 || it->second->pinned.back().get() == statements.back().get()))
        return it->second;
    if (S.blocks.size() > 100000) S.blocks.clear();
    auto p = vm::compileBlock(statements, "<block>", opt);
    S.stats.compiled++;
    S.blocks[key] = p;
    return p;
}

vm::ProtoPtr Interpreter::vmFunctionProto(const std::shared_ptr<FunctionStmt>& decl) {
    auto Sp = vmState(); auto& S = *Sp;
    auto opt = vmOptions(S, execBudget_);
    if (decl->vmProto && decl->vmProtoTicks == opt.ticks) return std::static_pointer_cast<vm::Proto>(decl->vmProto);
    std::vector<StmtPtr> empty;
    auto p = vm::compileBlock(decl->body ? decl->body->statements : empty, "fn:" + decl->name, opt);
    p->pinned.push_back(decl->body);
    S.stats.compiled++;
    decl->vmProto = p;
    decl->vmProtoTicks = opt.ticks;
    return p;
}

Interpreter::Flow Interpreter::vmRunBlock(const std::vector<StmtPtr>& statements, EnvPtr env, Value& ret) {
    if (statements.empty()) return Flow::None;
    return vmExecute(vmBlockProto(statements), std::move(env), ret);
}

Interpreter::Flow Interpreter::vmRunStmt(const StmtPtr& stmt, EnvPtr env, Value& ret) {
    auto Sp = vmState(); auto& S = *Sp;
    auto opt = vmOptions(S, execBudget_);
    vm::ProtoPtr p;
    auto it = S.stmts.find(stmt.get());
    if (it != S.stmts.end()) p = it->second;
    else {
        if (S.stmts.size() > 100000) S.stmts.clear();
        p = vm::compileStmt(stmt, opt);
        S.stats.compiled++;
        S.stmts[stmt.get()] = p;
    }
    return vmExecute(p, std::move(env), ret);
}

Value Interpreter::vmRunExpr(const ExprPtr& expr, EnvPtr env) {
    auto Sp = vmState(); auto& S = *Sp;
    auto opt = vmOptions(S, execBudget_);
    vm::ProtoPtr p;
    auto it = S.exprs.find(expr.get());
    if (it != S.exprs.end()) p = it->second;
    else {
        if (S.exprs.size() > 200000) S.exprs.clear();
        p = vm::compileExpr(expr, opt);
        p->pinned.clear();
        // تثبيت الـ AST: نحتفظ بالتعبير نفسه عبر جدول exprs
        p->exprs.push_back(expr);
        S.stats.compiled++;
        S.exprs[expr.get()] = p;
    }
    Value ret;
    vmExecute(p, std::move(env), ret);
    return ret;
}

// ---------------------------------------------------------------------------
//  العمليات الثنائية: نسخة حرفية من evaluate(BinaryExpr) في المفسِّر الشجري
// ---------------------------------------------------------------------------
Value Interpreter::vmBinary(int opi, Value& left, Value& right, int line) {
    Op op = static_cast<Op>(opi);
    TokenType tt = TokenType::PLUS;
    switch (op) {
        case Op::Add: tt = TokenType::PLUS; break;
        case Op::Sub: tt = TokenType::MINUS; break;
        case Op::Mul: tt = TokenType::STAR; break;
        case Op::Gt: tt = TokenType::GREATER; break;
        case Op::Ge: tt = TokenType::GREATER_EQUAL; break;
        case Op::Lt: tt = TokenType::LESS; break;
        case Op::Le: tt = TokenType::LESS_EQUAL; break;
        default: break;
    }
    switch (op) {
    case Op::Add:
        if (left.type == Value::Type::STRING || right.type == Value::Type::STRING)
            return Value::string(left.toDisplayString() + right.toDisplayString());
        if (left.type == Value::Type::NUMBER && right.type == Value::Type::NUMBER)
            return Value::num(left.number + right.number);
        if (left.type == Value::Type::SET || right.type == Value::Type::SET) return setops::binary(tt, left, right, line);
        if (left.type == Value::Type::INSTANCE || right.type == Value::Type::INSTANCE) {
            if (auto r = tryOperatorOverload("__add__", left, right, line)) return *r;
        }
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      "`+` operands must be numbers or strings, found `" + left.typeName() + "` and `" + right.typeName() + "`");
    case Op::Sub:
        if (left.type == Value::Type::SET || right.type == Value::Type::SET) return setops::binary(tt, left, right, line);
        if (left.type == Value::Type::INSTANCE || right.type == Value::Type::INSTANCE) {
            if (auto r = tryOperatorOverload("__sub__", left, right, line)) return *r;
        }
        requireNumbers(left, right, "-", line);
        return Value::num(left.number - right.number);
    case Op::Mul:
        if (left.type == Value::Type::SET || right.type == Value::Type::SET) return setops::binary(tt, left, right, line);
        if (left.type == Value::Type::INSTANCE || right.type == Value::Type::INSTANCE) {
            if (auto r = tryOperatorOverload("__mul__", left, right, line)) return *r;
        }
        requireNumbers(left, right, "*", line);
        return Value::num(left.number * right.number);
    case Op::Div:
        if (left.type == Value::Type::INSTANCE || right.type == Value::Type::INSTANCE) {
            if (auto r = tryOperatorOverload("__div__", left, right, line)) return *r;
        }
        requireNumbers(left, right, "/", line);
        if (right.number == 0) throw errWithReason(diag::Code::E0035_RuntimeError, line,
                                                    "division by zero", "the right-hand side of `/` evaluated to 0");
        return Value::num(left.number / right.number);
    case Op::Mod:
        if (left.type == Value::Type::INSTANCE || right.type == Value::Type::INSTANCE) {
            if (auto r = tryOperatorOverload("__mod__", left, right, line)) return *r;
        }
        requireNumbers(left, right, "%", line);
        if (right.number == 0) throw errWithReason(diag::Code::E0035_RuntimeError, line,
                                                    "division by zero", "the right-hand side of `%` evaluated to 0");
        return Value::num(std::fmod(left.number, right.number));
    case Op::Gt: case Op::Ge: case Op::Lt: case Op::Le: {
        const char* sym = op == Op::Gt ? ">" : op == Op::Ge ? ">=" : op == Op::Lt ? "<" : "<=";
        if (left.type == Value::Type::SET || right.type == Value::Type::SET) return setops::binary(tt, left, right, line);
        if (left.type == Value::Type::INSTANCE || right.type == Value::Type::INSTANCE) {
            if (auto r = tryCompareOverload(tt, left, right, line)) return *r;
        }
        requireNumbers(left, right, sym, line);
        switch (op) {
            case Op::Gt: return Value::boolean_(left.number > right.number);
            case Op::Ge: return Value::boolean_(left.number >= right.number);
            case Op::Lt: return Value::boolean_(left.number < right.number);
            default:     return Value::boolean_(left.number <= right.number);
        }
    }
    case Op::Eq:
        if (left.type == Value::Type::INSTANCE && right.type == Value::Type::INSTANCE) {
            if (auto r = tryEqOverload(left, right, false, line)) return Value::boolean_(*r);
        }
        return Value::boolean_(valuesEqual(left, right));
    case Op::Ne:
        if (left.type == Value::Type::INSTANCE && right.type == Value::Type::INSTANCE) {
            if (auto r = tryEqOverload(left, right, true, line)) return Value::boolean_(*r);
        }
        return Value::boolean_(!valuesEqual(left, right));
    case Op::NotSame: return Value::boolean_(!sameReference(left, right));
    case Op::CopyOf:  return Value::boolean_(valuesEqual(left, right) && !sameReference(left, right));
    default: break;
    }
    return Value::nil();
}

// ---------------------------------------------------------------------------
//  دخول إطار نداء داخل الآلة (نسخة حرفية من مقدمة callFunction)
// ---------------------------------------------------------------------------
void Interpreter::vmEnterCall(vm::RunState& rs, const std::shared_ptr<Callable>& fn, std::vector<Value>& args, int line) {
    if (args.size() != fn->declaration->params.size()) {
        auto d = diagErr(diag::Code::E0007_InvalidArguments, line,
                     "invalid number of arguments for `" + fn->declaration->name + "`");
        d.diagnostic->expected = std::to_string(fn->declaration->params.size()) + " argument(s)";
        d.diagnostic->found = std::to_string(args.size()) + " argument(s)";
        d.diagnostic->withReason("function `" + fn->declaration->name + "` expects " +
                                  std::to_string(fn->declaration->params.size()) + " parameter(s)");
        throw d;
    }
    if (callDepth >= maxCallDepth_) {
        auto d = diagErr(diag::Code::E0035_RuntimeError, line,
                     "maximum function call depth exceeded (" + std::to_string(maxCallDepth_) + ")");
        d.diagnostic->withReason("this is almost always unbounded recursion (a missing base case)")
         .withHint("check that every recursive path in this function eventually returns without calling itself again");
        throw d;
    }
    auto callEnv = std::make_shared<Environment>(fn->closure);
    for (size_t i = 0; i < args.size(); i++) {
        Value bound = copyForBinding(args[i]);
        if (i < fn->declaration->paramTypes.size() && !fn->declaration->paramTypes[i].empty()) {
            checkDeclaredType(bound, fn->declaration->paramTypes[i],
                               "parameter `" + fn->declaration->params[i] + "` of `" + fn->declaration->name + "`", line);
        }
        callEnv->define(fn->declaration->params[i], bound);
    }
    auto proto = vmFunctionProto(fn->declaration);
    callDepth++;
    vm_->stats.calls++;
    vm::Frame fr;
    fr.proto = std::move(proto);
    fr.stackBase = static_cast<uint32_t>(rs.stack.size());
    fr.envBase = static_cast<uint32_t>(rs.envs.size());
    fr.handlerBase = static_cast<uint32_t>(rs.handlers.size());
    fr.loopBase = static_cast<uint32_t>(rs.loops.size());
    fr.fn = fn;
    fr.callLine = line;
    rs.envs.push_back(std::move(callEnv));
    rs.frames.push_back(std::move(fr));
}

// ---------------------------------------------------------------------------
//  أدوات فكّ الإطارات والاستثناءات
// ---------------------------------------------------------------------------
namespace {
inline void popFrameRaw(vm::RunState& rs, int& callDepth) {
    vm::Frame& fr = rs.frames.back();
    bool isCall = fr.fn != nullptr;
    rs.stack.resize(fr.stackBase);
    rs.envs.resize(fr.envBase);
    rs.handlers.resize(fr.handlerBase);
    rs.loops.resize(fr.loopBase);
    rs.frames.pop_back();
    if (isCall) callDepth--;
}
} // namespace

static Value makeThrowMap(const ThrowSignal& ex) {
    auto err = std::make_shared<MapData>();
    err->push_back({Value::string("value"), ex.value});
    err->push_back({Value::string("message"), Value::string(ex.value.toDisplayString())});
    err->push_back({Value::string("line"), Value::num(ex.line)});
    return Value::makeMap(err);
}
static Value makeErrorMap(const RinError& ex) {
    auto err = std::make_shared<MapData>();
    err->push_back({Value::string("message"), Value::string(ex.message)});
    err->push_back({Value::string("line"), Value::num(ex.line)});
    if (ex.diagnostic) err->push_back({Value::string("code"), Value::string(diag::codeString(ex.diagnostic->code))});
    return Value::makeMap(err);
}

// يبحث عن معالج try (أو goal) ويفكّ إليه. true إن وُجد.
static bool unwindToHandler(vm::RunState& rs, bool wantGoal, int& callDepth, Value&& push) {
    for (size_t i = rs.handlers.size(); i-- > 0;) {
        if (rs.handlers[i].isGoal != wantGoal) continue;
        vm::Handler h = rs.handlers[i];
        while (rs.frames.size() > h.frameIdx + 1) {
            if (rs.frames.back().fn) callDepth--;
            rs.frames.pop_back();
        }
        rs.handlers.resize(i);
        rs.loops.resize(h.loops);
        rs.stack.resize(h.stack);
        rs.envs.resize(h.env);
        rs.stack.push_back(std::move(push));
        rs.frames.back().pc = h.target;
        return true;
    }
    return false;
}

// break/continue: يبحث عن علامة حلقة في الإطار الحالي ثم يفكّ إطارات النداء (مثل BreakSignal الشجري).
// يعيد 1 إن وُجدت علامة وضُبط pc، و0 إن لم توجد في هذا التشغيل.
static int applyLoopSignal(vm::RunState& rs, bool isBreak, int& callDepth) {
    for (;;) {
        vm::Frame& fr = rs.frames.back();
        if (rs.loops.size() > fr.loopBase) {
            vm::LoopMark m = rs.loops.back();
            rs.stack.resize(m.stack);
            rs.envs.resize(m.env);
            rs.handlers.resize(m.handlers);
            fr.pc = isBreak ? m.breakPC : m.contPC;
            return 1;
        }
        if (rs.frames.size() == 1) return 0;
        popFrameRaw(rs, callDepth);
    }
}

// ---------------------------------------------------------------------------
//  نقطة الدخول: يُشغّل Proto بحلقة dispatch مع معالجة إشارات المفسِّر (C++ exceptions)
// ---------------------------------------------------------------------------
Interpreter::Flow Interpreter::vmExecute(const vm::ProtoPtr& proto, EnvPtr env, Value& ret) {
    auto Sp = vmState();
    vm::State& S = *Sp;
    S.stats.nestedRuns++;
    RunHolder holder(S);
    vm::RunState& rs = holder.rs;
    struct DepthRestore { int& d; int saved; ~DepthRestore() { d = saved; } } dr{callDepth, callDepth};
    CurrentInterpGuard interpGuard(this);
    rs.envs.push_back(std::move(env));
    {
        vm::Frame f0; f0.proto = proto;
        rs.frames.push_back(std::move(f0));
    }
    for (;;) {
        try {
            return vmLoop(rs, ret);
        } catch (ThrowSignal& e) {
            if (!unwindToHandler(rs, false, callDepth, makeThrowMap(e))) throw;
        } catch (RinError& e) {
            if (!unwindToHandler(rs, false, callDepth, makeErrorMap(e))) throw;
        } catch (AchieveSignal& a) {
            if (!unwindToHandler(rs, true, callDepth, Value(a.value))) throw;
        } catch (BreakSignal&) {
            if (!applyLoopSignal(rs, true, callDepth)) throw;
        } catch (ContinueSignal&) {
            if (!applyLoopSignal(rs, false, callDepth)) throw;
        } catch (ReturnSignal& r) {
            Value v = std::move(r.value);
            if (rs.frames.size() == 1) { ret = std::move(v); return Flow::Return; }
            vm::Frame& fr = rs.frames.back();
            // خروج بإشارة من داخل إطار نداء: يعمل كـ return لذلك الإطار
            std::shared_ptr<Callable> fn = fr.fn;
            int ln = fr.callLine;
            popFrameRaw(rs, callDepth);
            if (fn && !fn->declaration->returnType.empty())
                checkDeclaredType(v, fn->declaration->returnType, "return value of `" + fn->declaration->name + "`", ln);
            rs.stack.push_back(std::move(v));
        }
    }
}

// ---------------------------------------------------------------------------
//  عمليات مساعدة للوصول لخصائص الكائنات (نفس منطق GetExpr/SetExpr/IndexExpr)
// ---------------------------------------------------------------------------
static inline Value* findVarSlot(Environment* e, const std::string& name) {
    for (; e; e = e->parent.get()) {
        auto it = e->values.find(name);
        if (it != e->values.end()) return &it->second;
    }
    return nullptr;
}

#define VM_POP() (std::move(stack.back())); stack.pop_back()

Interpreter::Flow Interpreter::vmLoop(vm::RunState& rs, Value& ret) {
    auto& stack = rs.stack;
    auto& envs = rs.envs;
    auto& frames = rs.frames;
    vm::Stats& stats = vm_->stats;

    vm::Frame* f = &frames.back();
    const vm::Proto* P = f->proto.get();
    const Instr* code = P->code.data();
    uint32_t pc = f->pc;

    auto reload = [&]() {
        f = &frames.back();
        P = f->proto.get();
        code = P->code.data();
        pc = f->pc;
    };
    auto popV = [&]() -> Value { Value v = std::move(stack.back()); stack.pop_back(); return v; };

    // إرجاع قيمة من الإطار الحالي. true = انتهى التشغيل (Flow::Return)
    auto doReturn = [&](Value v) -> bool {
        if (frames.size() == 1) { ret = std::move(v); return true; }
        std::shared_ptr<Callable> fn = f->fn;
        int ln = f->callLine;
        popFrameRaw(rs, callDepth);
        if (fn && !fn->declaration->returnType.empty())
            checkDeclaredType(v, fn->declaration->returnType, "return value of `" + fn->declaration->name + "`", ln);
        stack.push_back(std::move(v));
        reload();
        return false;
    };

    // نداء قيمة دالة: داخل الآلة (إطار جديد)
    auto callFn = [&](const std::shared_ptr<Callable>& fn, std::vector<Value>& args, int line) {
        f->pc = pc;
        vmEnterCall(rs, fn, args, line);
        reload();
    };

    for (;;) {
        const Instr& in = code[pc++];
        stats.instructions++;
        switch (in.op) {
        case Op::Nop: break;
        case Op::Const: stack.push_back(P->consts[static_cast<size_t>(in.a)]); break;
        case Op::Nil: stack.emplace_back(); break;
        case Op::True: stack.push_back(Value::boolean_(true)); break;
        case Op::False: stack.push_back(Value::boolean_(false)); break;
        case Op::Pop: stack.pop_back(); break;
        case Op::PopN: stack.resize(stack.size() - static_cast<size_t>(in.a)); break;
        case Op::Dup: stack.push_back(stack.back()); break;

        case Op::LoadVar: {
            const std::string& name = P->strs[static_cast<size_t>(in.a)];
            const EnvPtr& env = envs.back();
            if (liveActive_) {
                Environment* liveOwner = findLiveOwner(env, name);
                if (liveOwner && !liveOwner->live.empty()) {
                    auto lit = liveOwner->live.find(name);
                    if (lit != liveOwner->live.end()) { stack.push_back(liveRead(liveOwner, lit->second, name, in.b)); break; }
                }
            }
            Value* slot = findVarSlot(env.get(), name);
            if (!slot) throw undefinedVariableErr(name, in.b, env);
            stack.push_back(*slot);
            break;
        }
        case Op::DefineVar: {
            Value v = popV();
            envs.back()->define(P->strs[static_cast<size_t>(in.a)], v);
            break;
        }
        case Op::AssignVar: {
            const std::string& name = P->strs[static_cast<size_t>(in.a)];
            Value v = popV();
            const EnvPtr& env = envs.back();
            Environment* owner = nullptr;
            for (Environment* cur = env.get(); cur; cur = cur->parent.get()) {
                if (cur->values.count(name)) { owner = cur; break; }
            }
            if (!owner) throw undefinedVariableErr(name, in.b, env);
            Value stored = copyForBinding(v);
            if (liveActive_) { stack.push_back(liveAssign(owner, name, stored, in.b)); break; }
            assignStateAware(owner, name, stored, in.b);
            stack.push_back(std::move(stored));
            break;
        }
        case Op::AssignAppend: {
            auto ae = std::static_pointer_cast<AssignExpr>(P->exprs[static_cast<size_t>(in.a)]);
            f->pc = pc;
            if (tryAppendInPlace(ae, envs.back())) pc = static_cast<uint32_t>(in.b);
            break;
        }
        case Op::PushScope: envs.push_back(std::make_shared<Environment>(envs.back())); break;
        case Op::PopScope: envs.resize(envs.size() - static_cast<size_t>(in.a)); break;

        // ---- حساب ----
        case Op::Add: {
            Value& r = stack[stack.size() - 1]; Value& l = stack[stack.size() - 2];
            if (l.type == Value::Type::NUMBER && r.type == Value::Type::NUMBER) { l.number += r.number; stack.pop_back(); break; }
            f->pc = pc;
            Value res = vmBinary(static_cast<int>(in.op), l, r, in.a);
            stack.pop_back(); stack.back() = std::move(res);
            break;
        }
        case Op::Sub: case Op::Mul: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge:
        case Op::Eq: case Op::Ne: {
            Value& r = stack[stack.size() - 1]; Value& l = stack[stack.size() - 2];
            if (l.type == Value::Type::NUMBER && r.type == Value::Type::NUMBER) {
                double a = l.number, b = r.number;
                switch (in.op) {
                    case Op::Sub: l.number = a - b; break;
                    case Op::Mul: l.number = a * b; break;
                    case Op::Lt: l = Value::boolean_(a < b); break;
                    case Op::Le: l = Value::boolean_(a <= b); break;
                    case Op::Gt: l = Value::boolean_(a > b); break;
                    case Op::Ge: l = Value::boolean_(a >= b); break;
                    case Op::Eq: l = Value::boolean_(a == b); break;
                    default: l = Value::boolean_(a != b); break;
                }
                stack.pop_back();
                break;
            }
            f->pc = pc;
            Value res = vmBinary(static_cast<int>(in.op), l, r, in.a);
            stack.pop_back(); stack.back() = std::move(res);
            break;
        }
        case Op::Div: case Op::Mod: case Op::NotSame: case Op::CopyOf: {
            f->pc = pc;
            Value& r = stack[stack.size() - 1]; Value& l = stack[stack.size() - 2];
            Value res = vmBinary(static_cast<int>(in.op), l, r, in.a);
            stack.pop_back(); stack.back() = std::move(res);
            break;
        }
        case Op::Neg: {
            f->pc = pc;
            Value right = popV();
            if (right.type == Value::Type::INSTANCE && right.instance) {
                std::string owner;
                auto method = findMethod(right.instance->className, "__neg__", &owner);
                if (method) {
                    std::vector<Value> noArgs;
                    Value bound = bindMethod(right, method, owner);
                    stack.push_back(callFunction(bound.function, noArgs, in.a));
                    break;
                }
            }
            if (right.type != Value::Type::NUMBER)
                throw diagErr(diag::Code::E0004_InvalidType, in.a, "unary `-` operand must be a number, found `" + right.typeName() + "`");
            stack.push_back(Value::num(-right.number));
            break;
        }
        case Op::Not: { bool t = stack.back().isTruthy(); stack.back() = Value::boolean_(!t); break; }

        // ---- قفز ----
        case Op::Jmp: pc = static_cast<uint32_t>(in.a); break;
        case Op::JmpFalse: { bool t = stack.back().isTruthy(); stack.pop_back(); if (!t) pc = static_cast<uint32_t>(in.a); break; }
        case Op::JmpTrue: { bool t = stack.back().isTruthy(); stack.pop_back(); if (t) pc = static_cast<uint32_t>(in.a); break; }
        case Op::JmpFalseKeep: { if (!stack.back().isTruthy()) pc = static_cast<uint32_t>(in.a); else stack.pop_back(); break; }
        case Op::JmpTrueKeep: { if (stack.back().isTruthy()) pc = static_cast<uint32_t>(in.a); else stack.pop_back(); break; }

        // ---- مجموعات ----
        case Op::MakeArray: {
            size_t n = static_cast<size_t>(in.a);
            auto arr = std::make_shared<ArrayData>();
            arr->reserve(n);
            for (size_t i = stack.size() - n; i < stack.size(); i++) arr->push_back(std::move(stack[i]));
            stack.resize(stack.size() - n);
            stack.push_back(Value::makeArray(arr));
            break;
        }
        case Op::MakeMap: {
            size_t n = static_cast<size_t>(in.a);
            auto m = std::make_shared<MapData>();
            size_t base = stack.size() - 2 * n;
            for (size_t i = 0; i < n; i++) m->put(stack[base + 2 * i], stack[base + 2 * i + 1]);
            stack.resize(base);
            stack.push_back(Value::makeMap(m));
            break;
        }
        case Op::Index: {
            f->pc = pc;
            Value idx = popV(); Value obj = popV();
            int line = in.a;
            if (obj.type == Value::Type::ARRAY) {
                if (idx.type != Value::Type::NUMBER)
                    throw errWithReason(diag::Code::E0021_InvalidIndex, line, "array index must be a number", "found a `" + idx.typeName() + "` index instead");
                long i = static_cast<long>(idx.number);
                if (i < 0 || static_cast<size_t>(i) >= obj.array->size()) {
                    auto d = diagErr(diag::Code::E0021_InvalidIndex, line, "index out of range: " + std::to_string(i));
                    d.diagnostic->withReason("this array has " + std::to_string(obj.array->size()) + " element(s) (valid indices: 0.." +
                                              (obj.array->empty() ? std::string("-") : std::to_string(obj.array->size() - 1)) + ")");
                    throw d;
                }
                stack.push_back((*obj.array)[static_cast<size_t>(i)]);
                break;
            }
            if (obj.type == Value::Type::MAP) {
                if (const Value* hit = obj.map->findValue(idx)) stack.push_back(*hit); else stack.emplace_back();
                break;
            }
            if (obj.type == Value::Type::STRING) {
                if (idx.type != Value::Type::NUMBER)
                    throw errWithReason(diag::Code::E0021_InvalidIndex, line, "string index must be a number", "found a `" + idx.typeName() + "` index instead");
                long i = static_cast<long>(idx.number);
                if (i < 0 || static_cast<size_t>(i) >= obj.str.size()) {
                    auto d = diagErr(diag::Code::E0021_InvalidIndex, line, "index out of range: " + std::to_string(i));
                    d.diagnostic->withReason("this string has " + std::to_string(obj.str.size()) + " character(s)");
                    throw d;
                }
                stack.push_back(Value::string(std::string(1, obj.str[static_cast<size_t>(i)])));
                break;
            }
            if (obj.type == Value::Type::INSTANCE && hasMagic(obj, "__getitem__")) {
                std::vector<Value> a{idx};
                stack.push_back(*callMagic(obj, "__getitem__", a, line));
                break;
            }
            if (obj.type == Value::Type::SET) {
                auto d = diagErr(diag::Code::E0004_InvalidType, line, "cannot index a `set`: it has no positions");
                d.diagnostic->withHint("use `s.has(x)` to test membership, or `s.toArray()[i]` to read by position");
                throw d;
            }
            throw diagErr(diag::Code::E0004_InvalidType, line, "cannot index a value of type `" + obj.typeName() + "`");
        }
        case Op::IndexSet: {
            f->pc = pc;
            Value val = popV(); Value idx = popV(); Value obj = popV();
            int line = in.a;
            if (obj.type == Value::Type::ARRAY) {
                if (idx.type != Value::Type::NUMBER)
                    throw errWithReason(diag::Code::E0021_InvalidIndex, line, "array index must be a number", "found a `" + idx.typeName() + "` index instead");
                long i = static_cast<long>(idx.number);
                if (i == static_cast<long>(obj.array->size())) {
                    obj.array->push_back(val);
                } else if (i < 0 || static_cast<size_t>(i) >= obj.array->size()) {
                    auto d = diagErr(diag::Code::E0021_InvalidIndex, line, "index out of range: " + std::to_string(i));
                    d.diagnostic->withReason("this array has " + std::to_string(obj.array->size()) + " element(s)")
                     .withHint("use `arr[" + std::to_string(obj.array->size()) + "] = value` to append instead");
                    throw d;
                } else {
                    (*obj.array)[static_cast<size_t>(i)] = val;
                }
                stack.push_back(std::move(val));
                break;
            }
            if (obj.type == Value::Type::MAP) { obj.map->put(idx, val); stack.push_back(std::move(val)); break; }
            if (obj.type == Value::Type::INSTANCE && hasMagic(obj, "__setitem__")) {
                std::vector<Value> a{idx, val};
                callMagic(obj, "__setitem__", a, line);
                stack.push_back(std::move(val));
                break;
            }
            throw diagErr(diag::Code::E0004_InvalidType, line, "cannot assign into a value of type `" + obj.typeName() + "` via `[]`");
        }
        case Op::LiveGuard:
            if (liveActive_) { f->pc = pc; liveGuardWrite(envs.back(), P->exprs[static_cast<size_t>(in.a)], in.b); }
            break;

        // ---- خصائص ----
        case Op::GetProp: case Op::GetVarProp: {
            f->pc = pc;
            const std::string& prop = P->strs[static_cast<size_t>(in.a)];
            int line;
            Value obj;
            if (in.op == Op::GetVarProp) {
                const std::string& vname = P->strs[static_cast<size_t>(in.b)];
                line = in.c;
                const EnvPtr& env = envs.back();
                if (!classes.empty() && classes.count(vname)) {
                    Value shadow;
                    if (!env->get(vname, shadow)) { stack.push_back(staticGet(vname, prop, env, line)); break; }
                }
                if (liveActive_) {
                    Value liveProp;
                    if (livePropertyGet(env, vname, prop, line, liveProp)) { stack.push_back(std::move(liveProp)); break; }
                }
                int vline = P->lines[pc - 1];
                bool got = false;
                if (liveActive_) {
                    Environment* liveOwner = findLiveOwner(env, vname);
                    if (liveOwner && !liveOwner->live.empty()) {
                        auto lit = liveOwner->live.find(vname);
                        if (lit != liveOwner->live.end()) { obj = liveRead(liveOwner, lit->second, vname, vline); got = true; }
                    }
                }
                if (!got) {
                    Value* slot = findVarSlot(env.get(), vname);
                    if (!slot) throw undefinedVariableErr(vname, vline, env);
                    obj = *slot;
                }
            } else {
                line = in.b;
                obj = popV();
            }
            if (obj.type == Value::Type::INSTANCE) { stack.push_back(readMember(obj, prop, envs.back(), line)); break; }
            if (obj.type == Value::Type::MAP) {
                bool found = false;
                for (auto& kv : *obj.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == prop) { stack.push_back(kv.second); found = true; break; }
                }
                if (!found) stack.emplace_back();
                break;
            }
            throw diagErr(diag::Code::E0004_InvalidType, line,
                          "cannot access property `." + prop + "` on a value of type `" + obj.typeName() + "`");
        }
        case Op::GetSuper:
            f->pc = pc;
            stack.push_back(evaluateSuperGet(P->strs[static_cast<size_t>(in.a)], envs.back(), in.c));
            break;
        case Op::SetProp: {
            f->pc = pc;
            const std::string& prop = P->strs[static_cast<size_t>(in.a)];
            Value val = popV(); Value obj = popV();
            if (obj.type == Value::Type::INSTANCE) { stack.push_back(writeMember(obj, prop, val, envs.back(), in.b)); break; }
            if (obj.type == Value::Type::MAP) {
                bool done = false;
                for (auto& kv : *obj.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == prop) { kv.second = val; done = true; break; }
                }
                if (!done) obj.map->push_back({Value::string(prop), val});
                stack.push_back(std::move(val));
                break;
            }
            throw diagErr(diag::Code::E0004_InvalidType, in.b,
                          "cannot set property `." + prop + "` on a value of type `" + obj.typeName() + "`");
        }
        case Op::StaticSetTest: {
            const std::string& vname = P->strs[static_cast<size_t>(in.a)];
            if (!classes.empty() && classes.count(vname)) {
                Value shadow;
                if (!envs.back()->get(vname, shadow)) pc = static_cast<uint32_t>(in.b);
            }
            break;
        }
        case Op::StaticSet: {
            f->pc = pc;
            Value val = popV();
            stack.push_back(staticSet(P->strs[static_cast<size_t>(in.a)], P->strs[static_cast<size_t>(in.b)], val, envs.back(), in.c));
            break;
        }

        // ---- نداءات ----
        case Op::CallNamed: {
            size_t argc = static_cast<size_t>(in.b & 0xFFFF);
            int flags = in.b >> 16;
            std::vector<Value> args;
            args.reserve(argc);
            for (size_t i = stack.size() - argc; i < stack.size(); i++) args.push_back(std::move(stack[i]));
            stack.resize(stack.size() - argc);
            const std::string& callee = P->strs[static_cast<size_t>(in.a)];
            if (flags & vm::kCallFlagEligible) {
                if (natives.find(callee) == natives.end() && classes.find(callee) == classes.end()) {
                    Value* slot = findVarSlot(envs.back().get(), callee);
                    if (slot && slot->type == Value::Type::FUNCTION && slot->function) {
                        std::shared_ptr<Callable> fn = slot->function;
                        callFn(fn, args, in.c);
                        break;
                    }
                }
            }
            f->pc = pc;
            Value r = invokeCallee(callee, args, in.c, envs.back());
            stack.push_back(std::move(r));
            break;
        }
        case Op::CallValue: {
            size_t argc = static_cast<size_t>(in.a);
            std::vector<Value> args;
            args.reserve(argc);
            for (size_t i = stack.size() - argc; i < stack.size(); i++) args.push_back(std::move(stack[i]));
            stack.resize(stack.size() - argc);
            Value callee = popV();
            f->pc = pc;
            if (callee.type == Value::Type::INSTANCE && hasMagic(callee, "__call__")) {
                stack.push_back(callValue(callee, args, in.b));
                break;
            }
            if (callee.type != Value::Type::FUNCTION || !callee.function)
                throw diagErr(diag::Code::E0004_InvalidType, in.b, "cannot call a value of type `" + callee.typeName() + "`");
            callFn(callee.function, args, in.b);
            break;
        }
        case Op::CallMethod: {
            size_t argc = static_cast<size_t>(in.b);
            std::vector<Value> args;
            args.reserve(argc);
            for (size_t i = stack.size() - argc; i < stack.size(); i++) args.push_back(std::move(stack[i]));
            stack.resize(stack.size() - argc);
            Value obj = popV();
            const std::string& method = P->strs[static_cast<size_t>(in.a)];
            f->pc = pc;
            if (obj.type == Value::Type::SET) {
                Value out;
                if (tryCallSetMethod(obj, method, args, in.c, out)) { stack.push_back(std::move(out)); break; }
            }
            if (obj.type == Value::Type::INSTANCE) { stack.push_back(callMethodOn(obj, method, args, envs.back(), in.c)); break; }
            if (obj.type == Value::Type::MAP) {
                std::shared_ptr<Callable> target;
                for (auto& kv : *obj.map) {
                    if (kv.first.type == Value::Type::STRING && kv.first.str == method && kv.second.type == Value::Type::FUNCTION) {
                        target = kv.second.function; break;
                    }
                }
                if (target) { callFn(target, args, in.c); break; }
            }
            throw diagErr(diag::Code::E0004_InvalidType, in.c,
                          "cannot call method `." + method + "` on a value of type `" + obj.typeName() + "`");
        }
        case Op::CallSuper: {
            size_t argc = static_cast<size_t>(in.b);
            std::vector<Value> args;
            args.reserve(argc);
            for (size_t i = stack.size() - argc; i < stack.size(); i++) args.push_back(std::move(stack[i]));
            stack.resize(stack.size() - argc);
            const std::string& method = P->strs[static_cast<size_t>(in.a)];
            f->pc = pc;
            Value bound = evaluateSuperGet(method, envs.back(), in.c);
            if (bound.type != Value::Type::FUNCTION)
                throw diagErr(diag::Code::E0004_InvalidType, in.c, "`super." + method + "` is not a method");
            callFn(bound.function, args, in.c);
            break;
        }
        case Op::PipeFlow:
            if (activeFlowSession_) {
                f->pc = pc;
                auto ce = std::static_pointer_cast<CallExpr>(P->exprs[static_cast<size_t>(in.a)]);
                stack.push_back(evaluatePipelineFlow(ce, envs.back()));
                pc = static_cast<uint32_t>(in.b);
            }
            break;
        case Op::MakeClosure: {
            auto e = std::static_pointer_cast<LambdaExpr>(P->exprs[static_cast<size_t>(in.a)]);
            auto callable = std::make_shared<Callable>();
            callable->declaration = e->decl;
            callable->closure = envs.back();
            Value v; v.type = Value::Type::FUNCTION; v.function = callable;
            stack.push_back(std::move(v));
            break;
        }

        // ---- تعريفات ----
        case Op::FuncDef: {
            auto s = std::static_pointer_cast<FunctionStmt>(P->stmts[static_cast<size_t>(in.a)]);
            std::string what;
            if (nameCollides(s->name, what, /*includeExistingFunction=*/false)) {
                throw errWithReason(diag::Code::E0002_DuplicateVariable, s->line,
                                     "function `" + s->name + "` uses the same name as an existing " + what,
                                     "`" + s->name + "` is already defined as a " + what + "; Rin does not "
                                     "allow a function to share a name with a built-in function or a class, "
                                     "since calling `" + s->name + "(...)` would then silently resolve to "
                                     "only one of them instead of this function");
            }
            auto callable = std::make_shared<Callable>();
            callable->declaration = s;
            callable->closure = envs.back();
            Value v; v.type = Value::Type::FUNCTION; v.function = callable;
            envs.back()->define(s->name, v);
            break;
        }
        case Op::ClassDef:
            f->pc = pc;
            registerClassStmt(std::static_pointer_cast<ClassStmt>(P->stmts[static_cast<size_t>(in.a)]));
            break;
        case Op::EnumDef: {
            auto s = std::static_pointer_cast<EnumStmt>(P->stmts[static_cast<size_t>(in.a)]);
            size_t n = s->cases.size();
            size_t base = stack.size() - n;
            auto m = std::make_shared<MapData>();
            for (size_t i = 0; i < n; i++) {
                auto caseMap = std::make_shared<MapData>();
                caseMap->push_back({Value::string("__enum__"), Value::string(s->name)});
                caseMap->push_back({Value::string("name"), Value::string(s->cases[i].name)});
                caseMap->push_back({Value::string("value"), stack[base + i]});
                m->push_back({Value::string(s->cases[i].name), Value::makeMap(caseMap)});
            }
            stack.resize(base);
            envs.back()->define(s->name, Value::makeMap(m));
            break;
        }
        case Op::Let: {
            f->pc = pc;
            auto s = std::static_pointer_cast<LetStmt>(P->stmts[static_cast<size_t>(in.a)]);
            Value v = copyForBinding(popV());
            const EnvPtr env = envs.back();
            if (s->elseBlock && v.type == Value::Type::NIL) letRunElse(s->elseBlock, env, s->line, "`" + s->name + "` was nil");
            if (!s->typeName.empty()) checkDeclaredType(v, s->typeName, "variable `" + s->name + "`", s->line);
            env->define(s->name, v);
            break;
        }
        case Op::LetPattern:
            f->pc = pc;
            letExecPattern(std::static_pointer_cast<LetPatternStmt>(P->stmts[static_cast<size_t>(in.a)]), envs.back());
            break;
        case Op::PatternAssign:
            f->pc = pc;
            stack.push_back(letPatternAssign(std::static_pointer_cast<PatternAssignExpr>(P->exprs[static_cast<size_t>(in.a)]), envs.back()));
            break;

        // ---- print ----
        case Op::CheckStr:
            if (stack.back().type != Value::Type::STRING)
                throw diagErr(diag::Code::E0004_InvalidType, in.b, P->strs[static_cast<size_t>(in.a)] + stack.back().typeName());
            break;
        case Op::CheckNum:
            if (stack.back().type != Value::Type::NUMBER)
                throw diagErr(diag::Code::E0004_InvalidType, in.b, P->strs[static_cast<size_t>(in.a)] + stack.back().typeName());
            break;
        case Op::Truthy: stack.back() = Value::boolean_(stack.back().isTruthy()); break;
        case Op::DisplayP: {
            f->pc = pc;
            bool prettyOn = stack[stack.size() - 1 - static_cast<size_t>(in.a)].boolean;
            Value& v = stack.back();
            std::string s = (prettyOn && (v.type == Value::Type::ARRAY || v.type == Value::Type::MAP))
                                ? prettyPrintValue(v, 0) : v.toDisplayString();
            stack.back() = Value::string(std::move(s));
            break;
        }
        case Op::CheckUL:
            throw diagErr(diag::Code::E0035_RuntimeError, in.a, "'print': لا يمكن استخدام 'upper' و'lower' معاً في نفس الأمر");
        case Op::CheckAlign: {
            const std::string& a = stack.back().str;
            if (a != "left" && a != "right" && a != "center")
                throw diagErr(diag::Code::E0004_InvalidType, in.a, "'print': 'align' يجب أن يكون \"left\" أو \"right\" أو \"center\"، لكن وُجد \"" + a + "\"");
            break;
        }
        case Op::CheckLevel: {
            const std::string& l = stack.back().str;
            if (l != "info" && l != "success" && l != "warn" && l != "warning" && l != "error" && l != "debug")
                throw diagErr(diag::Code::E0035_RuntimeError, in.a, "'print': 'level' غير معروف \"" + l + "\" (المتاح: info, success, warn, error, debug)");
            break;
        }
        case Op::CheckRepeat:
            if (stack.back().number < 0)
                throw diagErr(diag::Code::E0035_RuntimeError, in.a, "'print': 'repeat' يجب ألا يكون سالباً");
            break;
        case Op::Fail:
            throw diagErr(static_cast<diag::Code>(in.c), in.b, P->strs[static_cast<size_t>(in.a)]);
        case Op::PrintEmit: {
            auto s = std::static_pointer_cast<PrintStmt>(P->stmts[static_cast<size_t>(in.a)]);
            std::string fxCode;
            if (s->fx) { if (stack.back().type == Value::Type::STRING) fxCode = stack.back().str; stack.pop_back(); }
            long long times = 1;
            if (s->repeatN) times = static_cast<long long>(stack.back().number), stack.pop_back();
            std::string label;
            if (s->label) { label = stack.back().str; stack.pop_back(); }
            std::string level;
            if (s->level) { level = stack.back().str; stack.pop_back(); }
            std::string alignMode = "left";
            bool hasWidth = false; double widthN = 0;
            if (s->width) {
                if (s->align) { alignMode = stack.back().str; stack.pop_back(); }
                widthN = stack.back().number; stack.pop_back(); hasWidth = true;
            }
            bool lowerOn = false, upperOn = false;
            if (s->lower) { lowerOn = stack.back().boolean; stack.pop_back(); }
            if (s->upper) { upperOn = stack.back().boolean; stack.pop_back(); }
            size_t n = s->exprs.size();
            std::string content;
            {
                size_t base = stack.size() - n;
                const std::string& sep = stack[base - 3].str;
                for (size_t i = 0; i < n; i++) { if (i > 0) content += sep; content += stack[base + i].str; }
            }
            std::string end = stack[stack.size() - n - 2].str;
            stack.resize(stack.size() - n - 3);
            if (upperOn) std::transform(content.begin(), content.end(), content.begin(), [](unsigned char c) { return std::toupper(c); });
            else if (lowerOn) std::transform(content.begin(), content.end(), content.begin(), [](unsigned char c) { return std::tolower(c); });
            if (hasWidth) {
                int w = static_cast<int>(widthN);
                int pad = w - static_cast<int>(content.size());
                if (pad > 0) {
                    if (alignMode == "left") content += std::string(static_cast<size_t>(pad), ' ');
                    else if (alignMode == "right") content = std::string(static_cast<size_t>(pad), ' ') + content;
                    else { int lp = pad / 2, rp = pad - lp; content = std::string(static_cast<size_t>(lp), ' ') + content + std::string(static_cast<size_t>(rp), ' '); }
                }
            }
            std::string prefix;
            if (s->level) {
                if (level == "info") prefix = "ℹ️ ";
                else if (level == "success") prefix = "✅ ";
                else if (level == "warn" || level == "warning") prefix = "⚠️ ";
                else if (level == "error") prefix = "❌ ";
                else if (level == "debug") prefix = "\U0001F41E ";
            }
            if (s->label) prefix += "[" + label + "] ";
            if (!fxCode.empty()) content = "\x1b[" + fxCode + "m" + content + "\x1b[0m";
            for (long long i = 0; i < times; i++) output << prefix << content << end;
            break;
        }

        // ---- تحكم ----
        case Op::Return: {
            Value v = popV();
            f->pc = pc;
            if (doReturn(std::move(v))) return Flow::Return;
            break;
        }
        case Op::End: {
            if (frames.size() == 1) return Flow::None;
            f->pc = pc;
            if (doReturn(Value::nil())) return Flow::Return;
            break;
        }
        case Op::ExprEnd:
            ret = popV();
            return Flow::None;
        case Op::Break: case Op::Continue: {
            bool isBreak = in.op == Op::Break;
            if (applyLoopSignal(rs, isBreak, callDepth)) reload();
            else return isBreak ? Flow::Break : Flow::Continue;
            break;
        }
        case Op::Tick:
            if (execBudget_ > 0 && ++execCount_ > execBudget_)
                throw RinError("Execution limit exceeded\nPossible infinite loop", in.a);
            break;
        case Op::Throw: {
            Value v = popV();
            f->pc = pc;
            ThrowSignal sig{v, in.a};
            // مسار سريع: معالج try داخل هذا التشغيل → بلا استثناء C++
            bool hasTry = false;
            for (auto& h : rs.handlers) if (!h.isGoal) { hasTry = true; break; }
            if (hasTry && unwindToHandler(rs, false, callDepth, makeThrowMap(sig))) { reload(); break; }
            throw sig;
        }
        case Op::TryBegin: {
            vm::Handler h;
            h.isGoal = false; h.target = static_cast<uint32_t>(in.a);
            h.frameIdx = static_cast<uint32_t>(frames.size() - 1);
            h.stack = static_cast<uint32_t>(stack.size()); h.env = static_cast<uint32_t>(envs.size());
            h.loops = static_cast<uint32_t>(rs.loops.size());
            rs.handlers.push_back(h);
            break;
        }
        case Op::GoalBegin: {
            vm::Handler h;
            h.isGoal = true; h.target = static_cast<uint32_t>(in.a);
            h.frameIdx = static_cast<uint32_t>(frames.size() - 1);
            h.stack = static_cast<uint32_t>(stack.size()); h.env = static_cast<uint32_t>(envs.size());
            h.loops = static_cast<uint32_t>(rs.loops.size());
            rs.handlers.push_back(h);
            break;
        }
        case Op::TryEnd: case Op::GoalEnd: rs.handlers.pop_back(); break;
        case Op::CatchBind: {
            Value m = popV();
            if (in.a >= 0) envs.back()->define(P->strs[static_cast<size_t>(in.a)], m);
            break;
        }
        case Op::Achieve: {
            Value v = popV();
            f->pc = pc;
            bool hasGoal = false;
            for (auto& h : rs.handlers) if (h.isGoal) { hasGoal = true; break; }
            if (hasGoal && unwindToHandler(rs, true, callDepth, Value(v))) { reload(); break; }
            throw AchieveSignal{v};
        }
        case Op::LoopEnter: {
            vm::LoopMark m;
            m.breakPC = static_cast<uint32_t>(in.a); m.contPC = static_cast<uint32_t>(in.b);
            m.stack = static_cast<uint32_t>(stack.size()); m.env = static_cast<uint32_t>(envs.size());
            m.handlers = static_cast<uint32_t>(rs.handlers.size());
            rs.loops.push_back(m);
            break;
        }
        case Op::LoopSync:
            rs.loops.back().env = static_cast<uint32_t>(envs.size());
            rs.loops.back().stack = static_cast<uint32_t>(stack.size());
            break;
        case Op::LoopLeave: rs.loops.pop_back(); break;
        case Op::ForIterBegin: {
            Environment* forEnv = envs.back().get();
            auto iterEnv = std::make_shared<Environment>(forEnv->parent);
            iterEnv->values = forEnv->values;
            envs.push_back(std::move(iterEnv));
            break;
        }
        case Op::ForIterEnd: {
            EnvPtr iterEnv = std::move(envs.back());
            envs.pop_back();
            Environment* forEnv = envs.back().get();
            for (auto& kv : iterEnv->values) {
                auto it = forEnv->values.find(kv.first);
                if (it != forEnv->values.end()) it->second = kv.second;
            }
            break;
        }
        case Op::ForInPrep: {
            f->pc = pc;
            Value iterableVal = popV();
            std::vector<Value> items;
            int line = in.a;
            if (iterableVal.type == Value::Type::ARRAY) {
                items = *iterableVal.array;
            } else if (iterableVal.type == Value::Type::MAP) {
                items.reserve(iterableVal.map->size());
                for (auto& kv : *iterableVal.map) items.push_back(kv.first);
            } else if (iterableVal.type == Value::Type::SET) {
                if (iterableVal.set) items = iterableVal.set->items;
            } else if (iterableVal.type == Value::Type::STRING) {
                items.reserve(iterableVal.str.size());
                for (char c : iterableVal.str) items.push_back(Value::string(std::string(1, c)));
            } else if (iterableVal.type == Value::Type::INSTANCE && hasMagic(iterableVal, "__iter__")) {
                std::vector<Value> noArgs;
                Value produced = *callMagic(iterableVal, "__iter__", noArgs, line);
                if (produced.type == Value::Type::ARRAY) items = *produced.array;
                else if (produced.type == Value::Type::MAP) { for (auto& kv : *produced.map) items.push_back(kv.first); }
                else throw diagErr(diag::Code::E0004_InvalidType, line,
                                   "`__iter__` of `" + iterableVal.typeName() + "` must return an array or map, found `" + produced.typeName() + "`");
            } else {
                throw diagErr(diag::Code::E0004_InvalidType, line,
                              "`for...in` requires an array, set, map, or string; found a `" + iterableVal.typeName() + "`");
            }
            stack.push_back(Value::makeArray(std::make_shared<ArrayData>(std::move(items))));
            stack.push_back(Value::num(0));
            break;
        }
        case Op::ForInNext: {
            f->pc = pc;
            Value& idxV = stack.back();
            Value& itemsV = stack[stack.size() - 2];
            size_t idx = static_cast<size_t>(idxV.number);
            if (idx >= itemsV.array->size()) { pc = static_cast<uint32_t>(in.a); break; }
            Value item = (*itemsV.array)[idx];
            idxV.number = static_cast<double>(idx + 1);
            auto s = std::static_pointer_cast<ForInStmt>(P->stmts[static_cast<size_t>(in.b)]);
            const EnvPtr env = envs.back();
            auto iterEnv = std::make_shared<Environment>(env);
            if (s->pattern) {
                LetBindings binds; std::string why;
                if (item.type == Value::Type::NIL || !letBindPattern(*s->pattern, item, env, binds, why))
                    throw diagErr(diag::Code::E0004_InvalidType, s->line,
                                  "cannot destructure a `for...in` item: " + (item.type == Value::Type::NIL ? std::string("the item is nil") : why));
                for (auto& b : binds) iterEnv->define(b.first, b.second);
            } else {
                iterEnv->define(s->varName, item);
            }
            envs.push_back(std::move(iterEnv));
            break;
        }
        case Op::MatchTest: {
            Value v = popV();
            bool eq = valuesEqual(stack.back(), v);
            stack.push_back(Value::boolean_(eq));
            break;
        }
        case Op::ExecStmt: {
            stats.delegated++;
            f->pc = pc;
            const StmtPtr stmt = P->stmts[static_cast<size_t>(in.a)];
            Value r;
            vmDirectStmt_ = stmt.get();
            Flow fl = exec(stmt, envs.back(), r);
            vmDirectStmt_ = nullptr;
            if (fl == Flow::Return) { if (doReturn(std::move(r))) return Flow::Return; }
            else if (fl == Flow::Break || fl == Flow::Continue) {
                if (applyLoopSignal(rs, fl == Flow::Break, callDepth)) reload();
                else return fl;
            }
            break;
        }
        default:
            throw RinError("RinVM: unknown opcode", in.c);
        }
    }
}

// ---------------------------------------------------------------------------
//  مفكّك برنامج كامل (يترجم دون تنفيذ، ويتتبّع الدوال/الأصناف/اللامبدا المرجعية)
// ---------------------------------------------------------------------------
std::string Interpreter::vmDisassembleProgram(const std::vector<StmtPtr>& program) {
    auto Sp = vmState();
    vm::CompileOptions opt;
    std::ostringstream os;
    std::set<const void*> seen;
    std::function<void(const std::shared_ptr<FunctionStmt>&)> dumpFn;
    std::function<void(const vm::ProtoPtr&)> dump = [&](const vm::ProtoPtr& p) {
        os << vm::disassemble(*p) << "\n";
        for (auto& s : p->stmts) {
            if (!s) continue;
            if (s->stmtKind == StmtKind::FunctionStmt) dumpFn(std::static_pointer_cast<FunctionStmt>(s));
            else if (s->stmtKind == StmtKind::ClassStmt)
                for (auto& m : std::static_pointer_cast<ClassStmt>(s)->methods) dumpFn(m);
        }
        for (auto& e : p->exprs) {
            if (e && e->exprKind == ExprKind::Lambda) dumpFn(std::static_pointer_cast<LambdaExpr>(e)->decl);
        }
    };
    dumpFn = [&](const std::shared_ptr<FunctionStmt>& fn) {
        if (!fn || !fn->body || !seen.insert(fn.get()).second) return;
        auto p = vm::compileBlock(fn->body->statements, "fn:" + fn->name + "(" + std::to_string(fn->params.size()) + ")", opt);
        dump(p);
    };
    for (auto& s : program) dump(vm::compileStmt(s, opt));
    return os.str();
}

} // namespace rin
