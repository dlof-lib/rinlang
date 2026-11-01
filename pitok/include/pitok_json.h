#pragma once
// pitok_json.h — تفريغ AST كـ JSON. أداة تحقّق/تصحيح فقط (ليست جزءًا من نحو اللغة)،
// تُستخدم لإثبات أن Parser يعمل فعليًا على ملفات .rin حقيقية دون تشغيل محرك Rin.

#include "pitok_ast.h"
#include <sstream>
#include <iomanip>

namespace pitok {

inline std::string jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\t': o << "\\t"; break;
            default: o << c;
        }
    }
    return o.str();
}

inline void dumpExpr(std::ostream& os, const ExprPtr& e) {
    if (!e) { os << "null"; return; }
    switch (e->kind) {
        case ExprKind::Number:
            os << "{\"type\":\"Number\",\"value\":" << e->numValue << "}";
            break;
        case ExprKind::String:
            os << "{\"type\":\"String\",\"value\":\"" << jsonEscape(e->strValue) << "\"}";
            break;
        case ExprKind::Identifier:
            os << "{\"type\":\"Identifier\",\"name\":\"" << jsonEscape(e->name) << "\"}";
            break;
        case ExprKind::Binary:
            os << "{\"type\":\"Binary\",\"op\":\"" << e->op << "\",\"left\":";
            dumpExpr(os, e->left);
            os << ",\"right\":";
            dumpExpr(os, e->right);
            os << "}";
            break;
        case ExprKind::Call:
            os << "{\"type\":\"Call\",\"name\":\"" << jsonEscape(e->name) << "\",\"args\":[";
            for (size_t i = 0; i < e->args.size(); ++i) {
                if (i) os << ",";
                dumpExpr(os, e->args[i]);
            }
            os << "]}";
            break;
    }
}

inline void dumpNode(std::ostream& os, const ViewNodePtr& n, int indent = 0) {
    std::string pad(indent, ' ');
    os << pad << "{\n";
    os << pad << "  \"kind\": \"" << jsonEscape(n->kind) << "\",\n";
    os << pad << "  \"name\": \"" << jsonEscape(n->name) << "\",\n";
    os << pad << "  \"line\": " << n->line << ",\n";
    os << pad << "  \"attributes\": [";
    for (size_t i = 0; i < n->attributes.size(); ++i) {
        if (i) os << ",";
        os << "\n" << pad << "    {\"key\": \"" << jsonEscape(n->attributes[i].key) << "\", \"value\": ";
        dumpExpr(os, n->attributes[i].value);
        os << "}";
    }
    if (!n->attributes.empty()) os << "\n" << pad << "  ";
    os << "],\n";
    os << pad << "  \"bindings\": [";
    for (size_t i = 0; i < n->bindings.size(); ++i) {
        if (i) os << ",";
        os << "\n" << pad << "    {\"target\": \"" << jsonEscape(n->bindings[i].target)
           << "\", \"event\": \"" << jsonEscape(n->bindings[i].event) << "\", \"action\": ";
        dumpExpr(os, n->bindings[i].action);
        os << "}";
    }
    if (!n->bindings.empty()) os << "\n" << pad << "  ";
    os << "],\n";
    os << pad << "  \"children\": [";
    for (size_t i = 0; i < n->children.size(); ++i) {
        if (i) os << ",";
        os << "\n";
        dumpNode(os, n->children[i], indent + 4);
    }
    if (!n->children.empty()) os << "\n" << pad << "  ";
    os << "]\n";
    os << pad << "}";
}

inline std::string dumpProgram(const Program& prog) {
    std::ostringstream os;
    os << "{\n  \"warps\": [";
    for (size_t i = 0; i < prog.warps.size(); ++i) {
        if (i) os << ",";
        os << "\n    {\"name\": \"" << jsonEscape(prog.warps[i].name) << "\", \"init\": ";
        dumpExpr(os, prog.warps[i].initValue);
        os << "}";
    }
    if (!prog.warps.empty()) os << "\n  ";
    os << "],\n  \"roots\": [\n";
    for (size_t i = 0; i < prog.roots.size(); ++i) {
        if (i) os << ",\n";
        dumpNode(os, prog.roots[i], 4);
    }
    os << "\n  ]\n}";
    return os.str();
}

} // namespace pitok
