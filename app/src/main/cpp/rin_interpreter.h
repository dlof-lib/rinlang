#pragma once
#include "rin_ast.h"
#include "diagnostics/diagnostic.h"
#include "diagnostics/diagnostic_engine.h"
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

// نقطة API حقيقية وفعلية (تختلف تماماً عن ApiRoute أعلاه، والتي تبقى صالحة كما هي لأجل
// المحاكاة/الاختبار بلا شبكة عبر route/call): يُسجِّلها المبرمج بنفسه عبر apiRegister(name, baseUrl)
// ويمكنه إضافة ترويساتها الخاصة (مفتاح API، Authorization...) عبر apiHeader(name, key, value)، ثم
// استدعاؤها فعلياً بطلب شبكة حقيقي عبر apiGet/apiPost/apiPut/apiPatch/apiDelete/apiCall.
struct ApiEndpoint {
    std::string baseUrl;
    std::vector<std::pair<std::string, std::string>> headers; // بترتيب التسجيل؛ نفس المفتاح المُعاد تسجيله يُحدَّث مكانه
};

// ---- relation: علاقة معرَّفة بين مجموعتَي مستندات (شبيهة بمفتاح أجنبي/foreign key بسيط) ----
// يربط قيمة حقل fromField داخل مستند في fromContainer بكل مستند في toContainer تساوي فيه toField
// نفس القيمة (انظر relatedDocs). يُعرَّف عبر defineRelation، ويُستعلَم عنه عبر relatedDocs.
struct RelationDef {
    std::string fromContainer, fromField, toContainer, toField;
};

// ---- migration: خطوة ترحيل مُسمّاة (up/down)، كلاهما دالة Rin بلا وسائط (انظر defineMigration) ----
struct MigrationDef {
    Value up;   // تُستدعى عند runMigration (يجب أن تكون دالة Rin)
    Value down; // تُستدعى عند rollbackMigration؛ قد تبقى nil إن لم تُمرَّر (لا رجوع لهذا الترحيل)
};

// ---- cache: مدخل مخزَّن مؤقّتاً مع مهلة صلاحية اختيارية (TTL) ----
struct CacheEntry {
    Value value;
    long long expiresAt = 0; // 0 أو أقل = بلا انتهاء صلاحية؛ غير ذلك = طابع زمني Unix بالثواني (انظر std::time)
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

    // يجمع كل أسماء المتغيرات المرئية في هذا النطاق وكل الآباء (بلا تكرار)؛ يُستخدم حصراً لبناء
    // اقتراحات "did you mean" لأخطاء E0001 (متغير غير معرَّف) — انظر diagnostics/diagnostic_engine.h.
    void collectVisibleNames(std::unordered_set<std::string>& out) const {
        for (auto& kv : values) out.insert(kv.first);
        if (parent) parent->collectVisibleNames(out);
    }
};

// Internal control-flow signal used to unwind the stack on `return`.
struct ReturnSignal { Value value; };
// Internal control-flow signals used to unwind the stack on `break` / `continue` inside `while`.
struct BreakSignal {};
struct ContinueSignal {};

class Interpreter {
public:
    Interpreter();
    // Runs a full program, returns everything printed (or a formatted error).
    std::string run(const std::vector<StmtPtr>& statements);

    // ---- Structured result of the last run() call (see run()'s catch block) ----
    // Set precisely when run() actually caught a RinError (i.e. a real failure occurred),
    // never inferred from the text of the returned string -- so callers (e.g. the JNI
    // structured-execution bridge) can report SUCCESS/ERROR correctly even when the program's
    // own printed output happens to start with '[' (e.g. `print ["a","b"];`).
    bool hadError() const { return lastDiagnostic_.has_value() || lastErrorMessage_.has_value(); }
    // Full Diagnostic (code/line/column/hints/...) when the failure was raised through the
    // diag:: system; absent for the rare paths that still throw a bare RinError(message,line).
    const std::optional<diag::Diagnostic>& lastDiagnostic() const { return lastDiagnostic_; }
    // Always populated on failure, diagnostic or not: a plain fallback message + line.
    const std::optional<std::string>& lastErrorMessage() const { return lastErrorMessage_; }
    int lastErrorLine() const { return lastErrorLine_; }

    // اسم الملف المستخدَم في كل Diagnostic صادر عن هذا الـ Interpreter (نظام Diagnostics — انظر
    // diagnostics/). يُضبَط عادة إلى نفس الاسم الذي مُرِّر إلى Lexer/Parser لنفس الملف.
    void setSourceFile(const std::string& name) { sourceFile = name; }

    // يحدّد جذر حقيقي على القرص تُبنى فوقه كل مسارات file/save/installation/writeFile/readFile...
    // (مثلاً مجلد التطبيق الخاص على أندرويد عبر context.filesDir). فارغ = المجلد الحالي (CWD).
    void setBasePath(const std::string& path) { basePath = path; }

    // كل معرّفات الربط العامة (container.link.id) المسجَّلة أثناء هذا التشغيل، ومقابل كل واحد اسم
    // الحاوية التي سجّلته. تُستخدم من طرف التطبيق/الأدوات الخارجية (مثلاً tools/rin_link_index.py)
    // لبناء فهرس ربط شامل بين ملفات rin. و.html/.js/.cpp عبر نفس المعرّف (انظر rin_ast.h: LinkIdDeclStmt).
    const std::unordered_map<std::string, std::string>& getLinkIds() const { return linkIdToContainer; }

    // ---- Loomtime bridge (see loom/rin_loom_needle.h) ----
    // Calls a top-level RIN function by name using the *real* interpreter -- full language
    // semantics (loops, recursion, stdlib) included -- so a Warp-bound `onTap` handler can
    // genuinely mutate state instead of being limited to the read-only attribute evaluator in
    // rin_loom_eval.h (which only captures `onTap=increment(count);` as a string, never runs it).
    //
    // `program` supplies every top-level statement so the callee's own `fun` declaration (and any
    // others it calls) hoist exactly like a normal run(). `args` are already-evaluated argument
    // values in call order; `paramAliases[i]` (same length as args, entries may be "") names the
    // Warp cell that argument literally came from in the call expression -- e.g. for
    // `onTap=increment(count);`, args[0] is count's current value and paramAliases[0]=="count".
    // A parameter shadows a same-named global inside the function body, so if the body reassigns
    // that parameter (`fun increment(count){ count = count + 1; }`) the new value only exists in
    // the call frame; after the call returns, this writes it back into `globalsInOut["count"]` via
    // the alias so the caller can push it back into the Warp cell. `globalsInOut` also seeds every
    // current Warp cell as a plain global (so a zero-arg handler like
    // `fun increment(){ count = count + 1; }` works too, by direct global mutation) and is updated
    // in place with the post-call value of every name it originally contained. Returns false
    // (globalsInOut left untouched) if no such function exists in `program`, or if it does but
    // fails at runtime (arity mismatch, a RinError raised inside it, etc.) -- `errorOut` explains why.
    bool callTopLevelFunction(const std::vector<StmtPtr>& program,
                               const std::string& fnName,
                               std::vector<Value>& args,
                               const std::vector<std::string>& paramAliases,
                               std::unordered_map<std::string, Value>& globalsInOut,
                               std::string& errorOut);

private:
    EnvPtr globals;
    std::ostringstream output;
    std::string sourceFile = "<input>"; // نظام Diagnostics — انظر setSourceFile() أعلاه

    // ---- Structured result state for the last run() (see hadError()/lastDiagnostic() above) ----
    std::optional<diag::Diagnostic> lastDiagnostic_;
    std::optional<std::string> lastErrorMessage_;
    int lastErrorLine_ = 0;

    // ---- Diagnostics helpers (src/diagnostics) ----
    // يبني RinError غنياً (Diagnostic كامل: كود + موقع من رقم السطر + رسالة) لأي خطأ تشغيل. العمود
    // يُقارَب بـ 1 لأن AST nodes تحمل .line فقط بلا عمود دقيق (خلافاً لـ Token في مرحلة التحليل).
    RinError err(diag::Code code, int line, std::string message) const;
    // نفس err() لكنها تضيف حقل reason: مباشرة (تُستخدم للأخطاء التي تحتاج شرح "لماذا" منفصلاً عن العنوان).
    RinError errWithReason(diag::Code code, int line, std::string message, std::string reason) const;
    // undefined variable/property مع اقتراح "did you mean" عبر Levenshtein على الأسماء المرئية فعلياً
    // في هذا النطاق (env) + أسماء الدوال المدمجة (natives) عند كان يبحث عن دالة.
    RinError undefinedVariableErr(const std::string& name, int line, const EnvPtr& env) const;
    RinError unknownFunctionErr(const std::string& name, int line) const;

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
    std::unordered_map<std::string, ApiEndpoint> apiEndpoints; // اسم -> نقطة API حقيقية مسجَّلة عبر apiRegister/apiHeader
    int defaultHttpTimeoutMs = 15000; // مهلة افتراضية لكل طلب HTTP حقيقي (قابلة للتعديل عبر httpSetTimeout)
    std::unordered_set<std::string> importedPaths;            // مسارات @import المُنفَّذة فعلاً في هذا التشغيل (لمنع الاستيراد المكرَّر)
    std::unordered_map<std::string, std::string> linkIdToContainer; // معرّف الربط العام (container.link.id) -> اسم الحاوية المسجَّلة به

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

    // ---- schema: مخطط حقول اختياري لكل مجموعة مستندات (container.doc/doc) ----
    // container -> [(اسم الحقل، اسم النوع)] بترتيب التعريف عبر defineSchema. أنواع مدعومة:
    // "string"/"number"/"bool"/"boolean"/"array"/"map"/"object"/"any". حقل مُعرَّف هنا يصبح
    // إلزامياً: أي إدراج/تحديث لاحق عبر insertDoc/updateDoc/document يُرفَض (RinError) إن غاب
    // الحقل أو خالف نوعه (انظر schemaErrors أدناه وvalidateDoc في .cpp).
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> schemaStore;
    // يتحقّق من قيمة (map حقول مستند مقترح) مقابل مخطط الحاوية المُعرَّف؛ مصفوفة فارغة = صالح
    // (أو لا يوجد مخطط أصلاً لهذه الحاوية، فلا قيود). تُستخدم من قِبل الدالة الأصلية validateDoc
    // ومن insertDoc/updateDoc/DocumentStmt قبل أي التزام فعلي بالتغيير.
    std::vector<std::string> schemaErrors(const std::string& container, const Value& fields) const;

    // ---- index: فهرسة قيم حقل معيّن داخل مجموعة مستندات (تسريع البحث عبر findByIndex) ----
    // container -> [(اسم الحقل، جدول: مفتاح القيمة النصي (repr) -> [ids المطابقة بترتيب الإدخال])].
    // تُعاد بناؤها بالكامل (بدل تحديث تفاضلي) بعد أي إدراج/تحديث/حذف مستند أو rollbackTransaction،
    // عبر refreshIndexesForContainer -- أبسط وأقل عرضة للأخطاء من الصيانة التفاضلية، ومقبولة الكلفة
    // لحجم البيانات المتوقَّع في مفسّر Rin.
    using IndexBuckets = std::unordered_map<std::string, std::vector<std::string>>;
    std::unordered_map<std::string, std::vector<std::pair<std::string, IndexBuckets>>> indexStore;
    void refreshIndexesForContainer(const std::string& container);

    // ---- transaction: لقطات (snapshots) عميقة من docStore لدعم commit/rollback حقيقيَّين ----
    // لقطة عميقة إلزامية (لا يكفي نسخ Value الضحلة) لأن updateDoc يُعدِّل MapData داخل MapPtr
    // القائم مكانه أحياناً (دمج جزئي/patch)، فيؤثّر ذلك على أي نسخة ضحلة تشارك نفس المؤشّر.
    std::vector<std::unordered_map<std::string, std::vector<std::pair<std::string, Value>>>> txStack;

    // ---- relation: علاقات معرَّفة بين مجموعتَي مستندات (انظر RelationDef أعلاه) ----
    std::unordered_map<std::string, RelationDef> relationStore;
    std::vector<std::string> relationOrder; // أسماء العلاقات بترتيب أول تعريف (listRelations)

    // ---- migration: خطوات ترحيل مُسمّاة، بترتيب تعريفها وبترتيب تطبيقها الفعلي ----
    std::unordered_map<std::string, MigrationDef> migrationStore;
    std::vector<std::string> migrationOrder;    // أسماء الترحيلات بترتيب التعريف (defineMigration)
    std::vector<std::string> appliedMigrations; // أسماء الترحيلات المُطبَّقة فعلاً، بترتيب التطبيق (runMigration)

    // ---- cache: تخزين مؤقّت للقيم بمهلة صلاحية اختيارية (انظر CacheEntry أعلاه) ----
    std::unordered_map<std::string, CacheEntry> cacheStore;

    // ---- watch/subscribe: دوال Rin مسجَّلة تُستدعى تلقائياً عند حدث ----
    // watch: اسم مجموعة مستندات -> دوال بتوقيع fun(id, doc, event) تُستدعى بعد كل insertDoc/
    // updateDoc/deleteDoc/document عليها (event = "insert"/"update"/"delete").
    std::unordered_map<std::string, std::vector<Value>> docWatchers;
    // subscribe/publish: قناة أحداث عامة مستقلة عن أي مجموعة مستندات؛ دوال بتوقيع fun(payload).
    std::unordered_map<std::string, std::vector<Value>> channelSubs;
    void notifyWatchers(const std::string& container, const std::string& id, const Value& doc,
                         const std::string& event, int line);

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

    // API حقيقية مسجَّلة عبر apiRegister/apiHeader: يبني baseUrl+path مع ترويسات النقطة، يُجري طلب
    // شبكة حقيقياً فعلياً (rin_http.h)، ويُعيد Value(map) بنتيجته (انظر httpResultToValue في .cpp).
    Value performRealApiCall(const std::string& endpointName, const std::string& method,
                              const std::string& path, const Value& bodyValue, int line);

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
