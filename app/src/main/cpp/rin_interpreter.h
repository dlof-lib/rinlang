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

// نقطة API واحدة مُسجَّلة داخل @container.api عبر عبارة route؛ يُستخدَم لمطابقة استدعاءات call()/callApi()
// بأسلوب "API حقيقي قابل للاختبار" (mock/stub) دون الحاجة لاتصال شبكة فعلي.
struct ApiRoute {
    std::string method;
    std::string path;
    double status = 200;
    Value body;
};

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

    // يحدّد جذر حقيقي على القرص تُبنى فوقه كل مسارات file/save/installation/writeFile/readFile...
    // (مثلاً مجلد التطبيق الخاص على أندرويد عبر context.filesDir). فارغ = المجلد الحالي (CWD).
    void setBasePath(const std::string& path) { basePath = path; }

private:
    EnvPtr globals;
    std::ostringstream output;

    // حالة لغة الحاويات/البيانات (container / Containers.Group / Volume / link / tying / merge ...)
    std::unordered_map<std::string, EnvPtr> containers;      // اسم الحاوية -> بيئة متغيراتها
    std::unordered_map<std::string, ContainerKind> containerKinds; // اسم الحاوية -> نوعها (لإعادة بنائها بشكل صحيح عند الحفظ)
    std::unordered_map<std::string, EnvPtr> groupEnvs;       // اسم المجموعة -> بيئتها الخاصة (متغيرات مُعلَنة مباشرة داخلها)
    std::unordered_map<std::string, std::vector<std::string>> groupMembers; // اسم المجموعة -> أسماء الحاويات/المجموعات الفرعية المباشرة بداخلها (بالترتيب)
    // ---- Section: حالة تُحفَظ بعد الإغلاق (قبل هذا كانت Section زخرفية بحتة: تطبع 🔹/◽ فقط ثم
    // تُفقَد متغيراتها فوراً مع نهاية الكتلة، بلا أي إمكانية للاستعلام عنها لاحقاً) ----
    std::unordered_map<std::string, EnvPtr> sectionEnvs; // اسم القسم -> بيئته (آخر تنفيذ له إن تكرّر، مثلاً داخل حلقة)
    std::vector<std::string> sectionOrder;               // أسماء الأقسام المُسمّاة بترتيب أول ظهور (بلا تكرار)
    std::vector<std::string> groupStack;                      // المجموعة الحالية المفتوحة (لتسجيل الأعضاء أثناء التنفيذ)
    std::unordered_map<std::string, std::string> translations; // lang -> text (آخر ترجمة مسجّلة لكل لغة)
    std::unordered_set<std::string> installedNames;          // ما تم "تثبيته" عبر installation (بما فيها ما حُمِّل من فهرس سابق فعلي على القرص)
    std::vector<std::string> containerStack;                 // مفتاح الحاوية الحالية (لأجل link/tying/merge/save/route/call)
    std::string currentFilePath;                              // آخر مسار مُعرَّف عبر file
    std::unordered_map<std::string, std::vector<ApiRoute>> apiRoutes; // مفتاح container.api -> نقاطها المسجَّلة عبر route
    std::unordered_set<std::string> importedPaths;            // مسارات @import المُنفَّذة فعلاً في هذا التشغيل (لمنع الاستيراد المكرَّر)

    // ---- مفهوم الجدول (container.table / table المستقلة) ----
    std::unordered_map<std::string, std::vector<Value>> tableRows;  // مفتاح الحاوية -> صفوفها (كل صف Value::ARRAY)
    // مفتاح الحاوية -> آخر "style value=" مسجَّل (مثال: "style://dark"). كانت خاصة بالجداول فقط،
    // وعُمِّمت الآن لتُستخدم أيضاً داخل container.object/Object، container.portal/portal،
    // container.block/block (مفاهيم التنسيق والستايل)، وليس container.table/table حصراً.
    std::unordered_map<std::string, std::string> containerStyles;
    std::string buildTablePng(const std::string& key) const;        // يرسم الجدول كصورة PNG حقيقية (شبكة خلايا ملوّنة)

    // ---- قاعدة بيانات لاعلاقية / NoSQL (container.doc / doc المستقلة) ----
    // container = "مجموعة مستندات" (collection)، وتُخزَّن مستنداتها هنا بترتيب الإدخال؛ id عربون
    // فريد داخل نفس المجموعة (إدراج بنفس id موجود = تحديث/upsert). Containers.Group التي تضم عدّة
    // container.doc تصبح فعلياً "قاعدة بيانات" (database) كاملة من عدّة مجموعات مستندات مرتّبة.
    std::unordered_map<std::string, std::vector<std::pair<std::string, Value>>> docStore;

    // ---- تخزين حقيقي على القرص (save/file/installation) ----
    std::string basePath;                                     // جذر حقيقي اختياري لكل عمليات الملفات
    bool installedIndexLoaded = false;
    // يدمج basePath مع مسار نسبي، ويطبّع المسار (يحلّ "." و".." ويرفض أي محاولة هروب خارج basePath
    // نفسه، سواء عبر "../" متكررة أو مسار مطلق صريح) لمنع أي كود Rin (خصوصاً مكتبة مستوردة من مصدر
    // غير موثوق) من الوصول لملفات خارج مجلد المشروع المعزول. line اختياري لرسالة خطأ أوضح إن توفّر.
    std::string resolvePath(const std::string& rawPath, int line = -1) const;
    void ensureParentDir(const std::string& fullPath) const;            // ينشئ مجلدات الأب إن لزم (mkdir -p يدوياً)
    std::string buildSaveDocument(const std::string& key, const EnvPtr& containerEnv,
                                   ContainerKind kind, bool simplified) const; // يبني نص .rin قابل لإعادة القراءة من متغيرات حاوية
    void writeRealFile(const std::string& relPath, const std::string& content, int line, const std::string& who) const; // كتابة فعلية + رسائل خطأ واضحة
    void loadInstalledIndex();                                          // يحمّل فهرس rin_installed/index.rininstall عند بداية run()
    void appendInstalledIndex(const std::string& name, const std::string& relPath, bool simplified) const;

    Value performApiCall(const std::string& containerKey, const std::string& method, const std::string& path, int line);

    // المكتبة القياسية (stdlib): دوال رياضية، معالجة نصوص، مصفوفات وقواميس.
    using NativeFn = std::function<Value(std::vector<Value>&, int)>;
    std::unordered_map<std::string, NativeFn> natives;
    void registerNatives();

    void execute(const StmtPtr& stmt, EnvPtr env);
    void executeBlock(const std::vector<StmtPtr>& statements, EnvPtr env);
    Value evaluate(const ExprPtr& expr, EnvPtr env);
    Value callFunction(const std::shared_ptr<Callable>& fn, std::vector<Value>& args, int line);

    // ---- حارس عمق الاستدعاء (call depth guard) ----
    // بلا هذا الحارس، دالة Rin تتكرّر ذاتياً بلا حالة توقّف (خطأ شائع من المستخدم، وليس فقط هجوماً
    // متعمَّداً) تُسبِّب Stack Overflow حقيقياً في مكدّس C++ الأصلي — وهذا segmentation fault لا يمكن
    // لأي try/catch اعتراضه أبداً (على خلاف RinError)، فيُسقِط التطبيق بالكامل (أو عملية الاختبار
    // خارج أندرويد) بدل رسالة خطأ Rin واضحة وقابلة للاستمرار. الحد أدناه محافظ (يعمل بأمان حتى مع
    // مكدّسات صغيرة نسبياً كما في بعض خيوط أندرويد)، ويكفي لأي تكرار عملي (fibonacci، إلخ).
    static constexpr int kMaxCallDepth = 300;
    int callDepth = 0;

    // ينسخ متغيرات هدف tying/merge (حاوية مفردة أو Containers.Group كاملة) داخل بيئة الحاوية الحالية.
    // يُرجع true إن كان الهدف مجموعة (Containers.Group)، أو false إن كان حاوية مفردة.
    bool copyTargetIntoCurrentContainer(const std::string& target, int line);
};

} // namespace rin
