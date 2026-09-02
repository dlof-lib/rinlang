#include "rin_make.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace rin {

static void collect(const StmtPtr& stmt, std::set<std::string>& out) {
    if (!stmt) return;
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
    if (kind == "component") return {"function","condition","loop","loop-control","return","io","container","view","data","table","doc","reckon"};
    if (kind == "page") return {"function","condition","loop","loop-control","return","io","container","view","data","table","doc","api","reckon"};
    if (kind == "library" || kind == "module") return {"function","condition","loop","loop-control","return","io","data","table","doc","import","reckon"};
    if (kind == "service") return {"function","condition","loop","loop-control","return","io","container","data","api","doc","import","reckon"};
    if (kind == "task") return {"function","condition","loop","loop-control","return","io","data","container","reckon"};
    if (kind == "data") return {"condition","loop","loop-control","return","io","data","table","doc","reckon"};
    if (kind == "plugin") return {"function","condition","loop","loop-control","return","io","container","view","data","api","import","chatbot","reckon"};
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
    if (!allowed.empty()) {
        std::vector<std::string> bad;
        for (const auto& cap : used) if (!contains(allowed, cap)) bad.push_back(cap);
        if (!bad.empty()) {
            throw std::runtime_error("Make Unit '" + make.name + "' kind='" + make.makeType + "' uses forbidden capabilities: " + listMissing(bad));
        }
    }

    if (!make.denies.empty()) {
        for (const auto& cap : used) if (contains(make.denies, cap)) {
            throw std::runtime_error("Make Unit '" + make.name + "' denies capability: " + cap);
        }
    }

    if (!make.needs.empty()) {
        std::vector<std::string> missing;
        for (const auto& cap : make.needs) if (used.find(cap) == used.end()) missing.push_back(cap);
        if (!missing.empty()) throw std::runtime_error("Make Unit '" + make.name + "' requires capabilities not used: " + listMissing(missing));
    }

    if (make.strict && !make.uses.empty()) {
        for (const auto& cap : used) if (!contains(make.uses, cap)) {
            throw std::runtime_error("Make Unit '" + make.name + "' is strict: capability '" + cap + "' must be declared with `use " + cap + ";`");
        }
    }
}

} // namespace rin
