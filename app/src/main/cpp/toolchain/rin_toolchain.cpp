#include "toolchain/rin_toolchain.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace rin::toolchain {
namespace fs = std::filesystem;

static std::string sev(ToolDiagnostic::Severity s) {
    return s == ToolDiagnostic::Severity::Error ? "error" :
           s == ToolDiagnostic::Severity::Warning ? "warning" : "note";
}
void DiagnosticBag::error(std::string c,std::string m,int l,std::string h){items_.push_back({ToolDiagnostic::Severity::Error,std::move(c),std::move(m),l,std::move(h)});}
void DiagnosticBag::warning(std::string c,std::string m,int l,std::string h){items_.push_back({ToolDiagnostic::Severity::Warning,std::move(c),std::move(m),l,std::move(h)});}
void DiagnosticBag::note(std::string c,std::string m,int l,std::string h){items_.push_back({ToolDiagnostic::Severity::Note,std::move(c),std::move(m),l,std::move(h)});}
bool DiagnosticBag::hasErrors() const { for(auto& x:items_) if(x.severity==ToolDiagnostic::Severity::Error) return true; return false; }
const std::vector<ToolDiagnostic>& DiagnosticBag::all() const { return items_; }
std::string DiagnosticBag::renderText() const {
    std::ostringstream o;
    for(auto& x:items_) {
        o << x.line << ": " << sev(x.severity) << "[" << x.code << "]: " << x.message;
        if(!x.hint.empty()) o << "\n    help: " << x.hint;
        o << "\n";
    }
    return o.str();
}

void SemanticAnalyzer::push(){scopes_.push_back({});}
void SemanticAnalyzer::pop(){if(!scopes_.empty())scopes_.pop_back();}
bool SemanticAnalyzer::declare(const std::string& n,int line){
    if(scopes_.empty()) push();
    auto& s=scopes_.back();
    if(s.count(n)){d_.error("SEM001","duplicate declaration of '"+n+"'",line,"rename the declaration or remove the duplicate");return false;}
    s[n]=line; return true;
}
bool SemanticAnalyzer::lookup(const std::string& n) const {
    for(auto i=scopes_.rbegin();i!=scopes_.rend();++i) if(i->count(n)) return true;
    return functions_.count(n)!=0;
}
void SemanticAnalyzer::expr(const ExprPtr& e){
    if(!e)return;
    if(auto x=std::dynamic_pointer_cast<VariableExpr>(e)){
        if(!lookup(x->name)) d_.error("SEM002","use of undeclared name '"+x->name+"'",e->line,"declare it before use");
    } else if(auto x=std::dynamic_pointer_cast<AssignExpr>(e)){
        if(!lookup(x->name)) d_.error("SEM002","assignment to undeclared name '"+x->name+"'",e->line);
        expr(x->value);
    } else if(auto x=std::dynamic_pointer_cast<BinaryExpr>(e)){expr(x->left);expr(x->right);}
    else if(auto x=std::dynamic_pointer_cast<LogicalExpr>(e)){expr(x->left);expr(x->right);}
    else if(auto x=std::dynamic_pointer_cast<ConditionalExpr>(e)){expr(x->condition);expr(x->whenTrue);expr(x->whenFalse);}
    else if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e))expr(x->right);
    else if(auto x=std::dynamic_pointer_cast<CallExpr>(e)){
        if(!functions_.count(x->callee)) d_.warning("SEM003","call to '"+x->callee+"' is not known statically",e->line,"it may be a native/runtime function");
        else if(arity_.count(x->callee) && arity_[x->callee]!=x->args.size())
            d_.error("SEM004","function '"+x->callee+"' expects "+std::to_string(arity_[x->callee])+" argument(s), got "+std::to_string(x->args.size()),e->line);
        for(auto&a:x->args)expr(a);
    } else if(auto x=std::dynamic_pointer_cast<MethodCallExpr>(e)){expr(x->object);for(auto&a:x->args)expr(a);}
    else if(auto x=std::dynamic_pointer_cast<GetExpr>(e))expr(x->object);
    else if(auto x=std::dynamic_pointer_cast<SetExpr>(e)){expr(x->object);expr(x->value);}
    else if(auto x=std::dynamic_pointer_cast<IndexExpr>(e)){expr(x->object);expr(x->index);}
    else if(auto x=std::dynamic_pointer_cast<IndexSetExpr>(e)){expr(x->object);expr(x->index);expr(x->value);}
    else if(auto x=std::dynamic_pointer_cast<ArrayExpr>(e)){for(auto&a:x->elements)expr(a);}
    else if(auto x=std::dynamic_pointer_cast<MapExpr>(e)){for(auto&a:x->entries){expr(a.key);expr(a.value);}}
    else if(auto x=std::dynamic_pointer_cast<GoalExpr>(e)){++goalDepth_;stmt(x->body);--goalDepth_;}
}
void SemanticAnalyzer::stmt(const StmtPtr& s){
    if(!s)return;
    if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){declare(x->name,s->line);expr(x->initializer);}
    else if(auto x=std::dynamic_pointer_cast<TextStmt>(s)){declare(x->name,s->line);expr(x->initializer);}
    else if(auto x=std::dynamic_pointer_cast<ExpressionStmt>(s))expr(x->expr);
    else if(auto x=std::dynamic_pointer_cast<PrintStmt>(s)){for(auto&e:x->exprs)expr(e);expr(x->sep);expr(x->end);expr(x->ifCond);}
    else if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){expr(x->condition);push();stmt(x->thenBranch);pop();if(x->elseBranch){push();stmt(x->elseBranch);pop();}}
    else if(auto x=std::dynamic_pointer_cast<WhileStmt>(s)){expr(x->condition);++loopDepth_;push();stmt(x->body);pop();--loopDepth_;}
    else if(auto x=std::dynamic_pointer_cast<ForStmt>(s)){push();stmt(x->initializer);expr(x->condition);expr(x->increment);++loopDepth_;stmt(x->body);--loopDepth_;pop();}
    else if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){push();for(auto&a:x->statements)stmt(a);pop();}
    else if(auto x=std::dynamic_pointer_cast<FunctionStmt>(s)){functions_.insert(x->name);arity_[x->name]=x->params.size();declare(x->name,s->line);push();++functionDepth_;for(auto&p:x->params)declare(p,s->line);stmt(x->body);--functionDepth_;pop();}
    else if(auto x=std::dynamic_pointer_cast<ReturnStmt>(s)){if(!functionDepth_)d_.error("SEM005","return used outside a function",s->line);expr(x->value);}
    else if(std::dynamic_pointer_cast<BreakStmt>(s)){if(!loopDepth_)d_.error("SEM006","break used outside a loop",s->line);}
    else if(std::dynamic_pointer_cast<ContinueStmt>(s)){if(!loopDepth_)d_.error("SEM007","continue used outside a loop",s->line);}
    else if(auto x=std::dynamic_pointer_cast<AchieveStmt>(s)){if(!goalDepth_)d_.error("SEM008","achieve used outside a goal block",s->line);expr(x->value);}
    else if(auto x=std::dynamic_pointer_cast<PlusConditionStmt>(s)){expr(x->condition);stmt(x->trueBranch);stmt(x->falseBranch);}
    else if(auto x=std::dynamic_pointer_cast<MatchStmt>(s)){expr(x->subject);for(auto&c:x->cases){for(auto&e:c.values)expr(e);stmt(c.body);}stmt(x->elseBranch);}
    else if(auto x=std::dynamic_pointer_cast<ThrowStmt>(s))expr(x->value);
    else if(auto x=std::dynamic_pointer_cast<TryCatchStmt>(s)){stmt(x->tryBranch);push();declare(x->catchName,s->line);stmt(x->catchBranch);pop();}
    else if(auto x=std::dynamic_pointer_cast<ClassStmt>(s)){declare(x->name,s->line);for(auto&m:x->methods){push();for(auto&p:m->params)declare(p,m->line);stmt(m->body);pop();}}
    else if(auto x=std::dynamic_pointer_cast<EnumStmt>(s)){declare(x->name,s->line);for(auto&c:x->cases)expr(c.value);}
}
SemanticResult SemanticAnalyzer::analyze(const std::vector<StmtPtr>& p){d_=DiagnosticBag{};scopes_.clear();functions_.clear();arity_.clear();functionDepth_=loopDepth_=goalDepth_=0;push();for(auto&s:p)stmt(s);SemanticResult r;r.diagnostics=d_;return r;}

void TypeChecker::push(){scopes_.push_back({});}
void TypeChecker::pop(){if(!scopes_.empty())scopes_.pop_back();}
std::string TypeChecker::normalize(const std::string& t) const {
    std::string x=t; for(char&c:x)c=(char)std::tolower((unsigned char)c);
    if(x=="any")return"Any"; if(x=="number"||x=="float"||x=="double")return"Number";
    if(x=="int"||x=="integer")return"Int"; if(x=="string"||x=="str")return"String";
    if(x=="bool"||x=="boolean")return"Bool"; if(x=="array"||x=="list")return"Array";
    if(x=="map"||x=="object"||x=="dictionary")return"Map"; if(x=="function"||x=="fn")return"Function";
    if(x=="nil"||x=="null")return"Nil"; return t;
}
std::string TypeChecker::lookup(const std::string& n) const {for(auto i=scopes_.rbegin();i!=scopes_.rend();++i){auto it=i->find(n);if(it!=i->end())return it->second;}return"Any";}
bool TypeChecker::compatible(const std::string&a,const std::string&b) const{return a=="Any"||b=="Any"||a==b||(a=="Int"&&b=="Number");}
std::string TypeChecker::expression(const ExprPtr&e){
    if(!e)return"Nil";
    if(auto x=std::dynamic_pointer_cast<LiteralExpr>(e)){switch(x->kind){case LiteralExpr::Kind::NUMBER:return"Number";case LiteralExpr::Kind::STRING:return"String";case LiteralExpr::Kind::BOOL:return"Bool";default:return"Nil";}}
    if(auto x=std::dynamic_pointer_cast<VariableExpr>(e))return lookup(x->name);
    if(auto x=std::dynamic_pointer_cast<AssignExpr>(e)){auto t=expression(x->value),old=lookup(x->name);if(old!="Any"&&!compatible(old,t))d_.error("TYPE002","cannot assign "+t+" to "+old,e->line);return t;}
    if(auto x=std::dynamic_pointer_cast<ArrayExpr>(e)){for(auto&a:x->elements)expression(a);return"Array";}
    if(auto x=std::dynamic_pointer_cast<MapExpr>(e)){for(auto&a:x->entries){expression(a.key);expression(a.value);}return"Map";}
    if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e)){auto t=expression(x->right);if(x->op==TokenType::MINUS&&t!="Number"&&t!="Int"&&t!="Any")d_.error("TYPE003","unary '-' requires a numeric value",e->line);return t;}
    if(auto x=std::dynamic_pointer_cast<LogicalExpr>(e)){expression(x->left);expression(x->right);return"Bool";}
    if(auto x=std::dynamic_pointer_cast<BinaryExpr>(e)){auto a=expression(x->left),b=expression(x->right);switch(x->op){case TokenType::PLUS:if(a==b&&(a=="String"||a=="Number"||a=="Int"))return a;if(a=="String"||b=="String")return"String";if(a=="Any"||b=="Any")return"Any";d_.error("TYPE004","operator '+' has incompatible operands "+a+" and "+b,e->line);return"Any";case TokenType::MINUS:case TokenType::STAR:case TokenType::SLASH:case TokenType::PERCENT:if((a=="Number"||a=="Int"||a=="Any")&&(b=="Number"||b=="Int"||b=="Any"))return"Number";d_.error("TYPE005","arithmetic operator requires numeric operands",e->line);return"Any";case TokenType::EQUAL_EQUAL:case TokenType::BANG_EQUAL:case TokenType::LESS:case TokenType::LESS_EQUAL:case TokenType::GREATER:case TokenType::GREATER_EQUAL:return"Bool";default:return"Any";}}
    if(auto x=std::dynamic_pointer_cast<ConditionalExpr>(e)){expression(x->condition);auto a=expression(x->whenTrue),b=expression(x->whenFalse);return a==b?a:"Any";}
    if(auto x=std::dynamic_pointer_cast<CallExpr>(e)){for(auto&a:x->args)expression(a);return functions_.count(x->callee)?functions_[x->callee]:"Any";}
    if(auto x=std::dynamic_pointer_cast<MethodCallExpr>(e)){expression(x->object);for(auto&a:x->args)expression(a);return"Any";}
    if(auto x=std::dynamic_pointer_cast<GetExpr>(e)){expression(x->object);return"Any";}
    if(auto x=std::dynamic_pointer_cast<SetExpr>(e)){expression(x->object);return expression(x->value);}
    if(auto x=std::dynamic_pointer_cast<IndexExpr>(e)){expression(x->object);expression(x->index);return"Any";}
    if(auto x=std::dynamic_pointer_cast<IndexSetExpr>(e)){expression(x->object);expression(x->index);return expression(x->value);}
    if(auto x=std::dynamic_pointer_cast<GoalExpr>(e)){statement(x->body);return"Any";}
    return"Any";
}
void TypeChecker::statement(const StmtPtr&s){
    if(!s)return;
    if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){auto t=expression(x->initializer);auto declared=normalize(x->typeName);if(!declared.empty()&&!compatible(declared,t))d_.error("TYPE001","initializer of '"+x->name+"' has type "+t+" but '"+declared+"' was declared",s->line);if(scopes_.empty())push();scopes_.back()[x->name]=declared.empty()?t:declared;}
    else if(auto x=std::dynamic_pointer_cast<TextStmt>(s)){auto t=expression(x->initializer);if(t!="String"&&t!="Any")d_.error("TYPE006","text declaration requires String, got "+t,s->line);scopes_.back()[x->name]="String";}
    else if(auto x=std::dynamic_pointer_cast<ExpressionStmt>(s))expression(x->expr);
    else if(auto x=std::dynamic_pointer_cast<PrintStmt>(s)){for(auto&e:x->exprs)expression(e);expression(x->sep);expression(x->end);expression(x->ifCond);}
    else if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){push();for(auto&a:x->statements)statement(a);pop();}
    else if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){expression(x->condition);statement(x->thenBranch);statement(x->elseBranch);}
    else if(auto x=std::dynamic_pointer_cast<WhileStmt>(s)){expression(x->condition);statement(x->body);}
    else if(auto x=std::dynamic_pointer_cast<ForStmt>(s)){statement(x->initializer);expression(x->condition);expression(x->increment);statement(x->body);}
    else if(auto x=std::dynamic_pointer_cast<FunctionStmt>(s)){std::string ret=normalize(x->returnType);functions_[x->name]=ret.empty()?"Any":ret;push();for(size_t i=0;i<x->params.size();++i)scopes_.back()[x->params[i]]=i<x->paramTypes.size()&&!x->paramTypes[i].empty()?normalize(x->paramTypes[i]):"Any";statement(x->body);pop();}
    else if(auto x=std::dynamic_pointer_cast<ReturnStmt>(s))expression(x->value);
    else if(auto x=std::dynamic_pointer_cast<PlusConditionStmt>(s)){expression(x->condition);statement(x->trueBranch);statement(x->falseBranch);}
    else if(auto x=std::dynamic_pointer_cast<MatchStmt>(s)){expression(x->subject);for(auto&c:x->cases){for(auto&e:c.values)expression(e);statement(c.body);}statement(x->elseBranch);}
}
SemanticResult TypeChecker::check(const std::vector<StmtPtr>&p){d_=DiagnosticBag{};scopes_.clear();functions_.clear();push();for(auto&s:p)statement(s);SemanticResult r;r.diagnostics=d_;return r;}

void Chunk::emit(Op op,int operand,int line){code.push_back({op,operand,line});}
int Chunk::constant(const std::string&s){constants.push_back(s);return (int)constants.size()-1;}
int BytecodeCompiler::jump(Op op,int line){chunk_.emit(op,-1,line);return (int)chunk_.code.size()-1;}
void BytecodeCompiler::patch(int at){chunk_.code[at].operand=(int)chunk_.code.size();}
bool BytecodeCompiler::expr(const ExprPtr&e){
    if(!e){chunk_.emit(Op::Nil,0);return true;}
    if(auto x=std::dynamic_pointer_cast<LiteralExpr>(e)){
        if(x->kind==LiteralExpr::Kind::NIL){chunk_.emit(Op::Nil,0,e->line);return true;}
        std::string v=x->kind==LiteralExpr::Kind::NUMBER?std::to_string(x->number):x->kind==LiteralExpr::Kind::BOOL?(x->boolean?"true":"false"):x->str;
        chunk_.emit(Op::Constant,chunk_.constant((x->kind==LiteralExpr::Kind::STRING?"s:":x->kind==LiteralExpr::Kind::NUMBER?"n:":"b:")+v),e->line);return true;
    }
    if(auto x=std::dynamic_pointer_cast<VariableExpr>(e)){chunk_.emit(Op::Load,chunk_.constant("name:"+x->name),e->line);return true;}
    if(auto x=std::dynamic_pointer_cast<AssignExpr>(e)){if(!expr(x->value))return false;chunk_.emit(Op::Store,chunk_.constant("name:"+x->name),e->line);return true;}
    if(auto x=std::dynamic_pointer_cast<UnaryExpr>(e)){if(!expr(x->right))return false;chunk_.emit(x->op==TokenType::MINUS?Op::Neg:Op::Not,0,e->line);return true;}
    if(auto x=std::dynamic_pointer_cast<BinaryExpr>(e)){
        if(!expr(x->left)||!expr(x->right))return false;Op op=Op::Add;
        switch(x->op){case TokenType::PLUS:op=Op::Add;break;case TokenType::MINUS:op=Op::Sub;break;case TokenType::STAR:op=Op::Mul;break;case TokenType::SLASH:op=Op::Div;break;case TokenType::PERCENT:op=Op::Mod;break;case TokenType::EQUAL_EQUAL:op=Op::Eq;break;case TokenType::BANG_EQUAL:op=Op::Ne;break;case TokenType::LESS:op=Op::Lt;break;case TokenType::LESS_EQUAL:op=Op::Le;break;case TokenType::GREATER:op=Op::Gt;break;case TokenType::GREATER_EQUAL:op=Op::Ge;break;default:d_.error("BC001","operator is not supported by the core bytecode backend",e->line);return false;}
        chunk_.emit(op,0,e->line);return true;
    }
    if(auto x=std::dynamic_pointer_cast<ConditionalExpr>(e)){
        if(!expr(x->condition))return false;int jf=jump(Op::JumpIfFalse,e->line);chunk_.emit(Op::Pop,0,e->line);if(!expr(x->whenTrue))return false;int je=jump(Op::Jump,e->line);patch(jf);chunk_.emit(Op::Pop,0,e->line);if(!expr(x->whenFalse))return false;patch(je);return true;
    }
    d_.error("BC002","expression is not lowered by the core bytecode backend",e->line,"use `rin build` for the full canonical backend");return false;
}
void BytecodeCompiler::stmt(const StmtPtr&s){
    if(!s)return;
    if(auto x=std::dynamic_pointer_cast<LetStmt>(s)){if(expr(x->initializer)){chunk_.emit(Op::Store,chunk_.constant("name:"+x->name),s->line);locals_[x->name]=true;}return;}
    if(auto x=std::dynamic_pointer_cast<ExpressionStmt>(s)){if(expr(x->expr))chunk_.emit(Op::Pop,0,s->line);return;}
    if(auto x=std::dynamic_pointer_cast<PrintStmt>(s)){for(auto&e:x->exprs)if(expr(e))chunk_.emit(Op::Print,0,s->line);return;}
    if(auto x=std::dynamic_pointer_cast<BlockStmt>(s)){for(auto&a:x->statements)stmt(a);return;}
    if(auto x=std::dynamic_pointer_cast<IfStmt>(s)){expr(x->condition);int jf=jump(Op::JumpIfFalse,s->line);chunk_.emit(Op::Pop,0,s->line);stmt(x->thenBranch);int je=jump(Op::Jump,s->line);patch(jf);chunk_.emit(Op::Pop,0,s->line);stmt(x->elseBranch);patch(je);return;}
    if(auto x=std::dynamic_pointer_cast<WhileStmt>(s)){int start=(int)chunk_.code.size();expr(x->condition);int jf=jump(Op::JumpIfFalse,s->line);chunk_.emit(Op::Pop,0,s->line);stmt(x->body);chunk_.emit(Op::Loop,start,s->line);patch(jf);chunk_.emit(Op::Pop,0,s->line);return;}
    if(auto x=std::dynamic_pointer_cast<ForStmt>(s)){stmt(x->initializer);int start=(int)chunk_.code.size();if(x->condition)expr(x->condition);else chunk_.emit(Op::True,0,s->line);int jf=jump(Op::JumpIfFalse,s->line);chunk_.emit(Op::Pop,0,s->line);stmt(x->body);if(x->increment){expr(x->increment);chunk_.emit(Op::Pop,0,s->line);}chunk_.emit(Op::Loop,start,s->line);patch(jf);chunk_.emit(Op::Pop,0,s->line);return;}
    if(auto x=std::dynamic_pointer_cast<ReturnStmt>(s)){expr(x->value);chunk_.emit(Op::Return,0,s->line);return;}
    d_.warning("BC003","statement is not lowered by the core bytecode backend",s->line,"use `rin build` for the canonical interpreter/native compiler");
}
CompileResult BytecodeCompiler::compile(const std::vector<StmtPtr>&p){chunk_=Chunk{};d_=DiagnosticBag{};locals_.clear();for(auto&s:p)stmt(s);chunk_.emit(Op::Halt);CompileResult r;r.chunk=chunk_;r.diagnostics=d_;return r;}
std::string BytecodeCompiler::disassemble(const Chunk&c){
    std::ostringstream o;
    for(size_t i=0;i<c.code.size();++i){auto in=c.code[i];o<<std::setw(4)<<i<<"  OP="<<(int)in.op<<"  arg="<<in.operand<<"  line="<<in.line;if(in.op==Op::Constant&&in.operand>=0&&in.operand<(int)c.constants.size())o<<"  "<<c.constants[in.operand];o<<"\n";}return o.str();
}
bool BytecodeVM::truthy(const std::string&v){return v!="nil"&&v!="false"&&v!="0"&&v!="0.0"&&v!="";}
bool BytecodeVM::number(const std::string&v,double&n){try{size_t p=0;n=std::stod(v,&p);return p==v.size();}catch(...){return false;}}
std::string BytecodeVM::value(double n){std::ostringstream o;o<<std::setprecision(15)<<n;return o.str();}
std::string BytecodeVM::pop(){if(stack_.empty())return"nil";auto v=stack_.back();stack_.pop_back();return v;}

size_t Optimizer::optimize(Chunk& c) {
    size_t changes=0;
    // Conservative peephole optimization. Never changes observable evaluation order.
    for(size_t i=0;i+2<c.code.size();++i) {
        auto a=c.code[i], b=c.code[i+1], op=c.code[i+2];
        if(a.op==Op::Constant && b.op==Op::Constant && op.operand==0) {
            bool fold = op.op==Op::Add||op.op==Op::Sub||op.op==Op::Mul||op.op==Op::Div||op.op==Op::Mod;
            if(fold && a.operand>=0 && b.operand>=0 && a.operand<(int)c.constants.size() && b.operand<(int)c.constants.size()) {
                double x,y;
                try { x=std::stod(c.constants[a.operand].substr(2)); y=std::stod(c.constants[b.operand].substr(2)); }
                catch(...) { continue; }
                if(op.op==Op::Div && y==0) continue;
                double z=op.op==Op::Add?x+y:op.op==Op::Sub?x-y:op.op==Op::Mul?x*y:op.op==Op::Div?x/y:std::fmod(x,y);
                c.code[i]={Op::Constant,c.constant("n:"+std::to_string(z)),a.line};
                c.code.erase(c.code.begin()+i+1,c.code.begin()+i+3);
                ++changes; if(i>0)--i;
            }
        }
    }
    return changes;
}
static bool writeU32(std::ofstream& f,uint32_t x){f.write(reinterpret_cast<const char*>(&x),4);return !!f;}
static bool readU32(std::ifstream& f,uint32_t& x){f.read(reinterpret_cast<char*>(&x),4);return !!f;}
bool writeBytecode(const Chunk& c,const std::string& path){
    std::ofstream f(path,std::ios::binary); if(!f)return false;
    f.write("RBC1",4); if(!writeU32(f,(uint32_t)c.constants.size())||!writeU32(f,(uint32_t)c.code.size()))return false;
    for(auto&s:c.constants){if(!writeU32(f,(uint32_t)s.size()))return false;f.write(s.data(),(std::streamsize)s.size());}
    for(auto&i:c.code){if(!writeU32(f,(uint32_t)i.op)||!writeU32(f,(uint32_t)i.operand)||!writeU32(f,(uint32_t)i.line))return false;}
    return !!f;
}
bool readBytecode(Chunk& c,const std::string& path){
    std::ifstream f(path,std::ios::binary); if(!f)return false;char magic[4];f.read(magic,4);if(std::string(magic,4)!="RBC1")return false;
    uint32_t nc=0,ni=0;if(!readU32(f,nc)||!readU32(f,ni))return false;c=Chunk{};
    for(uint32_t i=0;i<nc;++i){uint32_t n=0;if(!readU32(f,n))return false;std::string s(n,'\0');f.read(s.data(),n);if(!f)return false;c.constants.push_back(std::move(s));}
    for(uint32_t i=0;i<ni;++i){uint32_t op=0,arg=0,line=0;if(!readU32(f,op)||!readU32(f,arg)||!readU32(f,line))return false;c.code.push_back({(Op)op,(int)arg,(int)line});}
    return true;
}

BytecodeVM::Result BytecodeVM::run(const Chunk&c){
    Result r;stack_.clear();globals_.clear();size_t ip=0,steps=0;const size_t limit=50000000;
    auto binary=[&](Op op,const std::string&a,const std::string&b)->std::string{
        double x,y;
        if(op==Op::Add && (!number(a,x)||!number(b,y))) return a+b;
        if(!number(a,x)||!number(b,y)){if(op==Op::Eq)return a==b?"true":"false";if(op==Op::Ne)return a!=b?"true":"false";return"false";}
        switch(op){case Op::Add:return value(x+y);case Op::Sub:return value(x-y);case Op::Mul:return value(x*y);case Op::Div:if(y==0){r.ok=false;r.diagnostics.error("VM001","division by zero",0);return"nil";}return value(x/y);case Op::Mod:return value(std::fmod(x,y));case Op::Eq:return x==y?"true":"false";case Op::Ne:return x!=y?"true":"false";case Op::Lt:return x<y?"true":"false";case Op::Le:return x<=y?"true":"false";case Op::Gt:return x>y?"true":"false";case Op::Ge:return x>=y?"true":"false";default:return"nil";}
    };
    while(ip<c.code.size()&&r.ok){
        if(++steps>limit){r.ok=false;r.diagnostics.error("VM002","bytecode execution limit exceeded",0,"check for an infinite loop");break;}
        auto in=c.code[ip++];
        switch(in.op){
            case Op::Halt:return r;
            case Op::Constant:{auto v=c.constants[in.operand];stack_.push_back(v.size()>2&&v[1]==':'?v.substr(2):v);break;}
            case Op::Nil:stack_.push_back("nil");break;case Op::True:stack_.push_back("true");break;case Op::False:stack_.push_back("false");break;
            case Op::Load:{auto k=c.constants[in.operand].substr(5);stack_.push_back(globals_.count(k)?globals_[k]:"nil");break;}
            case Op::Store:{auto v=pop();auto k=c.constants[in.operand].substr(5);globals_[k]=v;stack_.push_back(v);break;}
            case Op::Pop:pop();break;
            case Op::Neg:{double x;if(!number(pop(),x)){r.ok=false;r.diagnostics.error("VM003","numeric negation of non-number",in.line);break;}stack_.push_back(value(-x));break;}
            case Op::Not:stack_.push_back(truthy(pop())?"false":"true");break;
            case Op::Add:case Op::Sub:case Op::Mul:case Op::Div:case Op::Mod:case Op::Eq:case Op::Ne:case Op::Lt:case Op::Le:case Op::Gt:case Op::Ge:{auto b=pop(),a=pop();stack_.push_back(binary(in.op,a,b));break;}
            case Op::Jump:ip=(size_t)in.operand;break;
            case Op::JumpIfFalse:if(!truthy(stack_.empty()?"nil":stack_.back()))ip=(size_t)in.operand;break;
            case Op::Loop:ip=(size_t)in.operand;break;
            case Op::Print:{r.output+=pop()+"\n";break;}
            case Op::Return:pop();return r;
        }
    } return r;
}

static bool writeText(const fs::path&p,const std::string&s){std::ofstream f(p);if(!f)return false;f<<s;return true;}
bool generateProject(const std::string&dir,const ProjectOptions&o,DiagnosticBag&d){
    if(o.name.empty()){d.error("NEW001","project name cannot be empty",1);return false;}
    fs::path root=fs::path(dir);std::error_code ec;
    fs::create_directories(root/"src",ec);fs::create_directories(root/"tests",ec);fs::create_directories(root/"build",ec);
    if(ec){d.error("NEW002","cannot create project directories: "+ec.message(),1);return false;}
    const std::string toml="[package]\nname = \""+o.name+"\"\nversion = \"0.1.0\"\nlanguage = \"rin\"\n\n[build]\nentry = \"src/main.rin\"\noutput = \"build/"+o.name+"\"\n\n[toolchain]\nsemantic = true\ntypecheck = true\nbytecode = true\n";
    const std::string main="fun main() {\n    let message: String = \"Hello from "+o.name+"\";\n    print message;\n}\n\nmain();\n";
    const std::string test="print \"Rin test: "+o.name+"\";\n";
    const std::string readme="# "+o.name+"\n\nGenerated by Rin Toolchain.\n\n## Commands\n\n- `rin check src/main.rin`\n- `rin analyze src/main.rin`\n- `rin bytecode src/main.rin`\n- `rin vm src/main.rin`\n- `rin build src/main.rin -o build/"+o.name+"`\n";
    if(!writeText(root/"rin.toml",toml)||!writeText(root/"src/main.rin",main)||!writeText(root/"tests/main.rin",test)||!writeText(root/"README.md",readme)){d.error("NEW003","failed to write project files",1);return false;}
    return true;
}
} // namespace rin::toolchain
