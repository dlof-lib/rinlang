#include "rin_make.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace rin {


// ---------------------------------------------------------------------------------------------
// كشف قدرات الوسائط (docs/MAKE_MEDIA.md): استدعاء make.video* / make.audio* / make.api* /
// make.image.* / make.ocr داخل أي تعبير (حتى المتداخل في let/print/if/return/lambda...) يُسجَّل
// كقدرة: video | audio | api | image | ocr. يخضع لنفس use/need/allow/deny/strict كبقية القدرات.
// make.media.tools() وبقية make.* القديمة (qr/file/hash/uuid/...) لا تُعدّ قدرات.
// ---------------------------------------------------------------------------------------------
static void noteMediaCall(const std::string& callee, std::set<std::string>& out) {
    auto is = [&](const char* base) {
        size_t n = std::char_traits<char>::length(base);
        return callee.compare(0, n, base) == 0 && (callee.size() == n || callee[n] == '.');
    };
    if (is("make.video")) out.insert("video");
    else if (is("make.audio")) out.insert("audio");
    else if (is("make.api")) out.insert("api");
    else if (is("make.ocr") || is("make.image.text")) out.insert("ocr");
    else if (is("make.image")) out.insert("image");
}

static void collect(const StmtPtr& stmt, std::set<std::string>& out);
static void collectExpr(const ExprPtr& e, std::set<std::string>& out);

static void collectBlock(const std::shared_ptr<BlockStmt>& b, std::set<std::string>& out) {
    if (b) for (const auto& s : b->statements) collect(s, out);
}

static void collectExpr(const ExprPtr& e, std::set<std::string>& out) {
    if (!e) return;
    if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
        noteMediaCall(c->callee, out);
        for (const auto& a : c->args) collectExpr(a, out);
    } else if (auto x = std::dynamic_pointer_cast<BinaryExpr>(e)) { collectExpr(x->left, out); collectExpr(x->right, out); }
    else if (auto x = std::dynamic_pointer_cast<LogicalExpr>(e)) { collectExpr(x->left, out); collectExpr(x->right, out); }
    else if (auto x = std::dynamic_pointer_cast<ConditionalExpr>(e)) { collectExpr(x->condition, out); collectExpr(x->whenTrue, out); collectExpr(x->whenFalse, out); }
    else if (auto x = std::dynamic_pointer_cast<UnaryExpr>(e)) collectExpr(x->right, out);
    else if (auto x = std::dynamic_pointer_cast<AssignExpr>(e)) collectExpr(x->value, out);
    else if (auto x = std::dynamic_pointer_cast<ArrayExpr>(e)) { for (const auto& el : x->elements) collectExpr(el, out); }
    else if (auto x = std::dynamic_pointer_cast<MapExpr>(e)) { for (const auto& en : x->entries) { collectExpr(en.key, out); collectExpr(en.value, out); } }
    else if (auto x = std::dynamic_pointer_cast<IndexExpr>(e)) { collectExpr(x->object, out); collectExpr(x->index, out); }
    else if (auto x = std::dynamic_pointer_cast<IndexSetExpr>(e)) { collectExpr(x->object, out); collectExpr(x->index, out); collectExpr(x->value, out); }
    else if (auto x = std::dynamic_pointer_cast<GetExpr>(e)) collectExpr(x->object, out);
    else if (auto x = std::dynamic_pointer_cast<SetExpr>(e)) { collectExpr(x->object, out); collectExpr(x->value, out); }
    else if (auto x = std::dynamic_pointer_cast<MethodCallExpr>(e)) { collectExpr(x->object, out); for (const auto& a : x->args) collectExpr(a, out); }
    else if (auto x = std::dynamic_pointer_cast<LambdaExpr>(e)) { if (x->decl) { out.insert("function"); collectBlock(x->decl->body, out); } }
    else if (auto x = std::dynamic_pointer_cast<CallValueExpr>(e)) { collectExpr(x->callee, out); for (const auto& a : x->args) collectExpr(a, out); }
    else if (auto x = std::dynamic_pointer_cast<PatternAssignExpr>(e)) collectExpr(x->value, out);
    else if (auto x = std::dynamic_pointer_cast<GoalExpr>(e)) collectBlock(x->body, out);
}

// التعبيرات المباشرة داخل العبارات (الجزء الذي كان collect() الأصلي لا يراه إطلاقاً).
static void collectStmtExprs(const StmtPtr& stmt, std::set<std::string>& out) {
    if (auto s = std::dynamic_pointer_cast<ExpressionStmt>(stmt)) collectExpr(s->expr, out);
    else if (auto s = std::dynamic_pointer_cast<LetStmt>(stmt)) { collectExpr(s->initializer, out); collectBlock(s->elseBlock, out); }
    else if (auto s = std::dynamic_pointer_cast<LetPatternStmt>(stmt)) { collectExpr(s->initializer, out); collectBlock(s->elseBlock, out); }
    else if (auto s = std::dynamic_pointer_cast<LetGroupStmt>(stmt)) { for (const auto& it : s->items) collect(it, out); }
    else if (auto s = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
        for (const auto& x : s->exprs) collectExpr(x, out);
        collectExpr(s->ifCond, out);
    }
    else if (auto s = std::dynamic_pointer_cast<ReturnStmt>(stmt)) collectExpr(s->value, out);
    else if (auto s = std::dynamic_pointer_cast<ThrowStmt>(stmt)) collectExpr(s->value, out);
    else if (auto s = std::dynamic_pointer_cast<AchieveStmt>(stmt)) collectExpr(s->value, out);
    else if (auto s = std::dynamic_pointer_cast<IfStmt>(stmt)) collectExpr(s->condition, out);
    else if (auto s = std::dynamic_pointer_cast<WhileStmt>(stmt)) { collectExpr(s->condition, out); collect(s->doneBlock, out); }
    else if (auto s = std::dynamic_pointer_cast<ForStmt>(stmt)) { collectExpr(s->condition, out); collectExpr(s->increment, out); collect(s->doneBlock, out); }
    else if (auto s = std::dynamic_pointer_cast<ForInStmt>(stmt)) { collectExpr(s->iterable, out); collect(s->body, out); collect(s->doneBlock, out); }
    else if (auto s = std::dynamic_pointer_cast<PlusConditionStmt>(stmt)) collectExpr(s->condition, out);
    else if (auto s = std::dynamic_pointer_cast<TryCatchStmt>(stmt)) { collectBlock(s->tryBranch, out); collectBlock(s->catchBranch, out); }
    else if (auto s = std::dynamic_pointer_cast<MatchStmt>(stmt)) {
        collectExpr(s->subject, out);
        for (const auto& c : s->cases) { for (const auto& v : c.values) collectExpr(v, out); collectBlock(c.body, out); }
        collectBlock(s->elseBranch, out);
    }
    else if (auto s = std::dynamic_pointer_cast<ReckonStmt>(stmt)) {
        collectExpr(s->collection, out); collectExpr(s->whereCond, out);
        for (const auto& st : s->stages) collectExpr(st, out);
    }
    else if (auto s = std::dynamic_pointer_cast<ClassStmt>(stmt)) {
        out.insert("function");
        for (const auto& m : s->methods) if (m) collectBlock(m->body, out);
    }
}

static void collect(const StmtPtr& stmt, std::set<std::string>& out) {
    if (!stmt) return;
    collectStmtExprs(stmt, out);
    if (std::dynamic_pointer_cast<FunctionStmt>(stmt)) out.insert("function");
    if (std::dynamic_pointer_cast<PrintStmt>(stmt) || std::dynamic_pointer_cast<LogStmt>(stmt) ||
        std::dynamic_pointer_cast<FileStmt>(stmt) || std::dynamic_pointer_cast<SaveStmt>(stmt) ||
        std::dynamic_pointer_cast<InstallationStmt>(stmt)) out.insert("io");
    if (std::dynamic_pointer_cast<WhileStmt>(stmt) || std::dynamic_pointer_cast<ForStmt>(stmt)) out.insert("loop");
    if (std::dynamic_pointer_cast<IfStmt>(stmt) || std::dynamic_pointer_cast<PlusConditionStmt>(stmt)) out.insert("condition");
    if (std::dynamic_pointer_cast<ReturnStmt>(stmt)) out.insert("return");
    if (std::dynamic_pointer_cast<ReckonStmt>(stmt)) out.insert("reckon");
    if (std::dynamic_pointer_cast<BreakStmt>(stmt) || std::dynamic_pointer_cast<ContinueStmt>(stmt)) out.insert("loop-control");
    if (std::dynamic_pointer_cast<ViewStmt>(stmt) || std::dynamic_pointer_cast<WarpStmt>(stmt) || std::dynamic_pointer_cast<ThemeStmt>(stmt)) out.insert("view");
    if (auto c = std::dynamic_pointer_cast<ContainerStmt>(stmt)) {
        out.insert("container");
        switch (c->kind) {
            case ContainerKind::DATA: out.insert("data"); break;
            case ContainerKind::API: out.insert("api"); break;
            case ContainerKind::IMPORT: out.insert("import"); break;
            case ContainerKind::TABLE: out.insert("table"); break;
            case ContainerKind::DOC: out.insert("doc"); break;
            case ContainerKind::CHATBOT: out.insert("chatbot"); break;
            case ContainerKind::EVERYTHING: out.insert("make"); break;
            default: break;
        }
        for (const auto& child : c->body) collect(child, out);
        return;
    }
    if (auto g = std::dynamic_pointer_cast<ContainerGroupStmt>(stmt)) {
        out.insert("container");
        for (const auto& child : g->body) collect(child, out);
        return;
    }
    if (auto v = std::dynamic_pointer_cast<VolumeStmt>(stmt)) {
        out.insert("container");
        for (const auto& child : v->body) collect(child, out);
        return;
    }
    if (auto b = std::dynamic_pointer_cast<BlockStmt>(stmt)) {
        for (const auto& child : b->statements) collect(child, out);
    }
    if (auto i = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        collect(i->thenBranch, out); collect(i->elseBranch, out);
    }
    if (auto w = std::dynamic_pointer_cast<WhileStmt>(stmt)) collect(w->body, out);
    if (auto f = std::dynamic_pointer_cast<ForStmt>(stmt)) { collect(f->initializer, out); collect(f->body, out); }
    if (auto fn = std::dynamic_pointer_cast<FunctionStmt>(stmt)) collect(fn->body, out);
    if (auto p = std::dynamic_pointer_cast<PlusConditionStmt>(stmt)) { collect(p->trueBranch, out); collect(p->falseBranch, out); }
}

std::set<std::string> makeCapabilities(const std::vector<StmtPtr>& body) {
    std::set<std::string> out;
    for (const auto& s : body) collect(s, out);
    return out;
}

std::vector<std::string> makeDefaultAllows(const std::string& kind) {
    if (kind == "component") return {"function","condition","loop","loop-control","return","io","container","view","data","table","doc","reckon","video","audio","image"};
    if (kind == "page") return {"function","condition","loop","loop-control","return","io","container","view","data","table","doc","api","reckon","video","audio","image","ocr"};
    if (kind == "library" || kind == "module") return {"function","condition","loop","loop-control","return","io","data","table","doc","import","reckon"};
    if (kind == "service") return {"function","condition","loop","loop-control","return","io","container","data","api","doc","import","reckon","image","ocr"};
    if (kind == "task") return {"function","condition","loop","loop-control","return","io","data","container","reckon","api","image","ocr","audio"};
    if (kind == "data") return {"condition","loop","loop-control","return","io","data","table","doc","reckon"};
    if (kind == "plugin") return {"function","condition","loop","loop-control","return","io","container","view","data","api","import","chatbot","reckon","video","audio","image","ocr"};
    if (kind == "app") return {};
    return {"__unknown_make_kind__"};
}

static bool contains(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

static std::string listMissing(const std::vector<std::string>& missing) {
    std::ostringstream o;
    for (size_t i=0;i<missing.size();++i) { if(i) o << ", "; o << missing[i]; }
    return o.str();
}

// جوهر التحقق مشترك بين Make Unit (سياسة كاملة + قوائم افتراضية حسب kind) وأي @container
// عادية استخدمت policy_block (§3.13 Phase 0، بلا أي قوائم افتراضية). label تُستخدم فقط
// لصياغة رسالة الخطأ.
static void enforcePolicy(const std::string& label, const std::set<std::string>& used,
                           const std::vector<std::string>& allowed,
                           const std::vector<std::string>& denies,
                           const std::vector<std::string>& needs,
                           const std::vector<std::string>& uses,
                           bool strict) {
    if (!allowed.empty()) {
        std::vector<std::string> bad;
        for (const auto& cap : used) if (!contains(allowed, cap)) bad.push_back(cap);
        if (!bad.empty()) {
            throw std::runtime_error(label + " uses forbidden capabilities: " + listMissing(bad));
        }
    }

    if (!denies.empty()) {
        for (const auto& cap : used) if (contains(denies, cap)) {
            throw std::runtime_error(label + " denies capability: " + cap);
        }
    }

    if (!needs.empty()) {
        std::vector<std::string> missing;
        for (const auto& cap : needs) if (used.find(cap) == used.end()) missing.push_back(cap);
        if (!missing.empty()) throw std::runtime_error(label + " requires capabilities not used: " + listMissing(missing));
    }

    if (strict && !uses.empty()) {
        for (const auto& cap : used) if (!contains(uses, cap)) {
            throw std::runtime_error(label + " is strict: capability '" + cap + "' must be declared with `use " + cap + ";`");
        }
    }
}

void validateMakeUnit(const MakeStmt& make) {
    auto used = makeCapabilities(make.body);
    auto defaults = makeDefaultAllows(make.makeType);
    if (make.makeType != "app" && make.makeType != "component" && make.makeType != "page" &&
        make.makeType != "library" && make.makeType != "module" && make.makeType != "service" &&
        make.makeType != "task" && make.makeType != "data" && make.makeType != "plugin") {
        throw std::runtime_error("unknown Make kind '" + make.makeType + "'");
    }

    // Explicit allow is the strongest whitelist. Otherwise known Make kinds receive a useful default policy.
    const std::vector<std::string>& allowed = !make.allows.empty() ? make.allows : defaults;
    enforcePolicy("Make Unit '" + make.name + "' kind='" + make.makeType + "'",
                  used, allowed, make.denies, make.needs, make.uses, make.strict);
}

void validateContainerPolicy(const ContainerStmt& c) {
    auto used = makeCapabilities(c.body);
    // بلا أي قائمة افتراضية هنا (خلافاً لـ Make Unit): حاوية عادية لا تملك مفهوم kind/makeType،
    // فـ allow الصريحة فقط -إن وُجدت- هي التي تعمل كقائمة بيضاء.
    enforcePolicy("Container '" + c.name + "'", used, c.allows, c.denies, c.needs, c.uses, c.strict);
}

void validateContainerGroupPolicy(const ContainerGroupStmt& g) {
    auto used = makeCapabilities(g.body);
    // نفس مبدأ validateContainerPolicy بالضبط (بلا قوائم افتراضية): سياسة المجموعة تُطبَّق على
    // مجموع القدرات المستخدَمة في كامل شجرتها (حاوياتها المباشرة + أي مجموعات فرعية متداخلة
    // وحاوياتها بدورها)، لا على المجموعة كغلاف تنظيمي مجرَّد.
    enforcePolicy("Containers.Group '" + g.name + "'", used, g.allows, g.denies, g.needs, g.uses, g.strict);
}

} // namespace rin
