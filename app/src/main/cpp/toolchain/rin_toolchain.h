#pragma once
#include "rin_ast.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace rin::toolchain {

// Production toolchain layer shared by CLI and future IDE/LSP integrations.
// It deliberately reuses Rin's canonical Lexer/Parser/AST instead of creating
// a second grammar, preventing drift between the editor, interpreter and compiler.

struct ToolDiagnostic {
    enum class Severity { Error, Warning, Note };
    Severity severity = Severity::Error;
    std::string code;
    std::string message;
    int line = 1;
    std::string hint;
};

class DiagnosticBag {
public:
    void error(std::string code, std::string msg, int line, std::string hint = {});
    void warning(std::string code, std::string msg, int line, std::string hint = {});
    void note(std::string code, std::string msg, int line, std::string hint = {});
    bool hasErrors() const;
    const std::vector<ToolDiagnostic>& all() const;
    std::string renderText() const;
private:
    std::vector<ToolDiagnostic> items_;
};

struct SemanticResult {
    DiagnosticBag diagnostics;
    std::unordered_map<std::string, std::string> inferredTypes;
};

class SemanticAnalyzer {
public:
    SemanticResult analyze(const std::vector<StmtPtr>& program);
private:
    DiagnosticBag d_;
    std::vector<std::unordered_map<std::string, int>> scopes_;
    std::unordered_set<std::string> functions_;
    std::unordered_map<std::string, size_t> arity_;
    int functionDepth_ = 0;
    int loopDepth_ = 0;
    int goalDepth_ = 0;
    void push();
    void pop();
    bool declare(const std::string&, int);
    bool lookup(const std::string&) const;
    void stmt(const StmtPtr&);
    void expr(const ExprPtr&);
};

class TypeChecker {
public:
    SemanticResult check(const std::vector<StmtPtr>& program);
private:
    DiagnosticBag d_;
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
    std::unordered_map<std::string, std::string> functions_;
    std::string expression(const ExprPtr&);
    void statement(const StmtPtr&);
    std::string normalize(const std::string&) const;
    bool compatible(const std::string&, const std::string&) const;
    std::string lookup(const std::string&) const;
    void push(); void pop();
};

enum class Op : uint8_t {
    Halt, Constant, Nil, True, False,
    Load, Store, Pop,
    Add, Sub, Mul, Div, Mod, Neg, Not,
    Eq, Ne, Lt, Le, Gt, Ge,
    Jump, JumpIfFalse, Loop,
    Print, Return
};

struct Instruction {
    Op op = Op::Halt;
    int operand = 0;
    int line = 0;
};

struct Chunk {
    std::vector<Instruction> code;
    std::vector<std::string> constants;
    void emit(Op op, int operand = 0, int line = 0);
    int constant(const std::string&);
};

struct CompileResult {
    Chunk chunk;
    DiagnosticBag diagnostics;
};

class BytecodeCompiler {
public:
    CompileResult compile(const std::vector<StmtPtr>& program);
    static std::string disassemble(const Chunk&);
private:
    Chunk chunk_;
    DiagnosticBag d_;
    std::unordered_map<std::string, bool> locals_;
    std::unordered_map<std::string, int> loopStart_;
    void stmt(const StmtPtr&);
    bool expr(const ExprPtr&);
    int jump(Op op, int line);
    void patch(int at);
};

class Optimizer {
public:
    size_t optimize(Chunk&);
};

bool writeBytecode(const Chunk&, const std::string& path);
bool readBytecode(Chunk&, const std::string& path);

class BytecodeVM {
public:
    struct Result { bool ok = true; std::string output; DiagnosticBag diagnostics; };
    Result run(const Chunk&);
private:
    std::vector<std::string> stack_;
    std::unordered_map<std::string, std::string> globals_;
    static bool truthy(const std::string&);
    static bool number(const std::string&, double&);
    static std::string value(double);
    std::string pop();
};

struct ProjectOptions {
    std::string name;
    std::string templateName = "console";
};

bool generateProject(const std::string& directory, const ProjectOptions&, DiagnosticBag&);

} // namespace rin::toolchain
