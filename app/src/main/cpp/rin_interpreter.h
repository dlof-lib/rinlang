#pragma once
#include "rin_ast.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <variant>
#include <functional>
#include <vector>
#include <utility>

namespace rin {

struct Environment;
using EnvPtr = std::shared_ptr<Environment>;

struct Value; // fwd

struct Callable {
    std::shared_ptr<FunctionStmt> declaration;
    EnvPtr closure;
};

// مصفوفة (array): تُمرَّر بالمرجع (shared_ptr) حتى تعمل push/pop/الفهرسة بالتعديل المباشر.
using ArrayData = std::vector<Value>;
using ArrayPtr = std::shared_ptr<ArrayData>;

// قاموس (map): قائمة أزواج (key, value) تحافظ على ترتيب الإدخال؛ المقارنة بالقيمة عبر valuesEqual.
using MapData = std::vector<std::pair<Value, Value>>;
using MapPtr = std::shared_ptr<MapData>;

struct Value {
    enum class Type { NIL, NUMBER, STRING, BOOL, FUNCTION, ARRAY, MAP } type = Type::NIL;
    double number = 0.0;
    std::string str;
    bool boolean = false;
    std::shared_ptr<Callable> function;
    ArrayPtr array;
    MapPtr map;

    static Value nil() { return Value{}; }
    static Value num(double n) { Value v; v.type = Type::NUMBER; v.number = n; return v; }
    static Value string(std::string s) { Value v; v.type = Type::STRING; v.str = std::move(s); return v; }
    static Value boolean_(bool b) { Value v; v.type = Type::BOOL; v.boolean = b; return v; }
    static Value makeArray(ArrayPtr a) { Value v; v.type = Type::ARRAY; v.array = std::move(a); return v; }
    static Value makeMap(MapPtr m) { Value v; v.type = Type::MAP; v.map = std::move(m); return v; }

    bool isTruthy() const {
        if (type == Type::NIL) return false;
        if (type == Type::BOOL) return boolean;
        if (type == Type::NUMBER) return number != 0.0;
        return true;
    }
    std::string toDisplayString() const;
    std::string typeName() const;
};

// مقارنة تركيبية (structural) بين قيمتين، تُستخدم في == != وفهرسة القواميس بالمفتاح.
bool valuesEqual(const Value& a, const Value& b);

struct Environment : std::enable_shared_from_this<Environment> {
    std::unordered_map<std::string, Value> values;
    EnvPtr parent;
    explicit Environment(EnvPtr parentEnv = nullptr) : parent(std::move(parentEnv)) {}

    void define(const std::string& name, const Value& v) { values[name] = v; }
    bool assign(const std::string& name, const Value& v) {
        auto it = values.find(name);
        if (it != values.end()) { it->second = v; return true; }
        if (parent) return parent->assign(name, v);
        return false;
    }
    bool get(const std::string& name, Value& out) {
        auto it = values.find(name);
        if (it != values.end()) { out = it->second; return true; }
        if (parent) return parent->get(name, out);
        return false;
    }
};

// Internal control-flow signal used to unwind the stack on `return`.
struct ReturnSignal { Value value; };

class Interpreter {
public:
    Interpreter();
    // Runs a full program, returns everything printed (or a formatted error).
    std::string run(const std::vector<StmtPtr>& statements);

private:
    EnvPtr globals;
    std::ostringstream output;

    // حالة لغة الحاويات/البيانات (container / Containers.Group / Volume / link / tying / merge ...)
    std::unordered_map<std::string, EnvPtr> containers;      // اسم الحاوية -> بيئة متغيراتها
    std::unordered_map<std::string, std::string> translations; // lang -> text (آخر ترجمة مسجّلة لكل لغة)
    std::unordered_set<std::string> installedNames;          // ما تم "تثبيته" عبر installation
    std::vector<std::string> containerStack;                 // الحاوية الحالية (لأجل link/tying/merge/save)
    std::string currentFilePath;                              // آخر مسار مُعرَّف عبر file

    // المكتبة القياسية (stdlib): دوال رياضية، معالجة نصوص، مصفوفات وقواميس.
    using NativeFn = std::function<Value(std::vector<Value>&, int)>;
    std::unordered_map<std::string, NativeFn> natives;
    void registerNatives();

    void execute(const StmtPtr& stmt, EnvPtr env);
    void executeBlock(const std::vector<StmtPtr>& statements, EnvPtr env);
    Value evaluate(const ExprPtr& expr, EnvPtr env);
    Value callFunction(const std::shared_ptr<Callable>& fn, std::vector<Value>& args, int line);
};

} // namespace rin
