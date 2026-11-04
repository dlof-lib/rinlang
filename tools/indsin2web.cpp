// tools/indsin2web.cpp — المترجم الرسمي لواجهات Indsin إلى الويب (HTML + CSS + JavaScript).
//
// يأخذ ملف Indsin بأي من الصيغتين (الوسوم `<Column#root>` أو `@view.Column=root … .end/view`) ويُخرج
// صفحة HTML واحدة مستقلة: بنية دلالية (header/button/input/hr/…) + أنماط + JS صغير يُعيد حساب الخصائص
// عند تغيّر أي warp، بنفس دلالات المحرّك (warp = حالة تفاعلية، التعابير تُعاد عند كل تغيّر).
//
// خط الأنابيب (نفس واجهة اللغة الرسمية، بلا محلّل ثانٍ):
//   source ──indsin::tags::desugar──▶ لهجة @view ──rin::Lexer/Parser──▶ AST ──(هذا الملف)──▶ HTML
//
// الاستخدام:   indsin2web input.rin [-o out.html] [--check] [--strict] [--title T] [--lang ar] [--dir rtl] [--desugar] (الافتراضي en/ltr كما في المحرّك)
//                         [--fragment] [--report report.json] [--no-runtime-errors]
// رموز الخروج: 0 نجاح · 1 خطأ في المصدر · 2 تحذيرات مع --strict · 64 سوء استعمال.
#include "indsin/rin_indsin_tags.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_ast.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace rin;

namespace {

// ───────────────────────────── تشخيصات ─────────────────────────────
struct Diag { std::string level, message; int line; };
std::vector<Diag> g_diags;
std::set<std::string> g_diagSeen;
void warn(const std::string& m, int line) {
    if (g_diagSeen.insert(m).second) g_diags.push_back({"warning", m, line});
}

// ───────────────────────────── مساعدات نصية ─────────────────────────────
std::string jsStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            case '<': o += "\\u003c"; break; // يمنع إنهاء <script> مبكراً عبر </script>
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
        }
    }
    return o + "\"";
}
std::string htmlEsc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c=='&') o+="&amp;"; else if (c=='<') o+="&lt;"; else if (c=='>') o+="&gt;"; else if (c=='"') o+="&quot;"; else o+=c; }
    return o;
}
std::string numStr(double d) {
    if (d == (long long)d) return std::to_string((long long)d);
    std::ostringstream os; os << d; return os.str();
}
bool isIdent(const std::string& s) {
    if (s.empty() || isdigit((unsigned char)s[0])) return false;
    for (unsigned char c : s) if (!(isalnum(c) || c == '_' || c >= 0x80)) return false;
    return true;
}

// ───────────────────────────── سياق الترجمة ─────────────────────────────
struct Ctx {
    std::set<std::string> warps;                 // أسماء warp (تُقرأ كـ S.name)
    std::set<std::string> globals;               // let/fun على مستوى الملف
    std::vector<std::set<std::string>> locals;   // نطاقات محلية (معاملات/let داخل الدوال)
    std::map<std::string, ExprPtr> warpInit;
    bool isLocal(const std::string& n) const { for (auto& s : locals) if (s.count(n)) return true; return false; }
};
Ctx g;

const std::set<std::string> kActions = {
    "increment","decrement","toggle","set","show","hide","enable","disable","select","unselect","focus","blur",
    "reset","seek","scrollTo","submit","multiply","negate","swap","clamp","cycle","append","backspace","copy","seq"};
// دوال مضمَّنة يوفّرها الـ runtime (R.*)
const std::set<std::string> kNatives = {
    "len","str","num","upper","lower","round","floor","ceil","abs","min","max","sqrt","pow","sin","cos","tan",
    "random","trim","contains","join","split","keys","values","range","push","now","time","int","sum","reverse","print",
    "startsWith","endsWith","replace","isNil","typeof","clamp","substr","indexOf","repeat","capitalize"};
// دوال indsin.* النقية التي ينقلها الـ runtime إلى JS (نماذج/ألوان)
const std::set<std::string> kIndsinFns = {"isEmail","isPhone","isUrl","isNumber","passwordScore","passwordLabel","passwordTone",
    "maskEmail","initials","slug","fieldError","themeColor","onColor","isLight"};

std::string expr(const ExprPtr& e);

std::string opStr(TokenType t, int line) {
    switch (t) {
        case TokenType::PLUS: return "+"; case TokenType::MINUS: return "-"; case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/"; case TokenType::PERCENT: return "%";
        case TokenType::EQUAL_EQUAL: return "==="; case TokenType::BANG_EQUAL: return "!==";
        case TokenType::LESS: return "<"; case TokenType::LESS_EQUAL: return "<=";
        case TokenType::GREATER: return ">"; case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::AND: return "&&"; case TokenType::OR: return "||";
        case TokenType::BANG: return "!";
        case TokenType::NOT_SAME: return "!==";
        default: warn("عامل غير مدعوم في الترجمة إلى الويب", line); return "+";
    }
}

std::string varRef(const std::string& n) {
    if (g.isLocal(n)) return n;
    if (g.warps.count(n)) return "S." + n;
    return n;
}

std::string args(const std::vector<ExprPtr>& a) {
    std::string o;
    for (size_t i = 0; i < a.size(); i++) { if (i) o += ", "; o += expr(a[i]); }
    return o;
}

std::string callExpr(const std::string& callee, const std::vector<ExprPtr>& a, int line) {
    if (callee == "json.parse") return "JSON.parse(" + args(a) + ")";
    if (callee == "json.stringify") return "JSON.stringify(" + args(a) + ")";
    if (!g.isLocal(callee) && !g.globals.count(callee) && kActions.count(callee)) {
        // إجراءات المحرّك: الوسيط الأول اسم خلية warp (لا قيمتها)
        std::string o = "A." + callee + "(";
        for (size_t i = 0; i < a.size(); i++) {
            if (i) o += ", ";
            bool cellArg = i == 0 || (i == 1 && (callee == "copy" || callee == "swap"));
            if (cellArg && a[i]->exprKind == ExprKind::Variable && g.warps.count(static_cast<VariableExpr*>(a[i].get())->name))
                o += jsStr(static_cast<VariableExpr*>(a[i].get())->name);
            else o += expr(a[i]);
        }
        return o + ")";
    }
    if (callee.rfind("indsin.", 0) == 0 && kIndsinFns.count(callee.substr(7)))
        return callee + "(" + args(a) + ")";
    if (!g.isLocal(callee) && !g.globals.count(callee) && !kNatives.count(callee))
        warn("الاستدعاء `" + callee + "(...)` ليس دالة معرَّفة في الملف ولا دالة مضمَّنة في مترجم الويب", line);
    std::string target = g.isLocal(callee) || g.globals.count(callee) ? callee : (kNatives.count(callee) ? "R." + callee : callee);
    return target + "(" + args(a) + ")";
}

std::string expr(const ExprPtr& e) {
    if (!e) return "null";
    switch (e->exprKind) {
        case ExprKind::Literal: {
            auto* l = static_cast<LiteralExpr*>(e.get());
            switch (l->kind) {
                case LiteralExpr::Kind::NUMBER: return numStr(l->number);
                case LiteralExpr::Kind::STRING: return jsStr(l->str);
                case LiteralExpr::Kind::BOOL: return l->boolean ? "true" : "false";
                default: return "null";
            }
        }
        case ExprKind::Variable: return varRef(static_cast<VariableExpr*>(e.get())->name);
        case ExprKind::Assign: {
            auto* a = static_cast<AssignExpr*>(e.get());
            return "(" + varRef(a->name) + " = " + expr(a->value) + ")";
        }
        case ExprKind::Binary: {
            auto* b = static_cast<BinaryExpr*>(e.get());
            if (b->op == TokenType::COPY_OF) return "R.copyOf(" + expr(b->left) + ", " + expr(b->right) + ")";
            if (b->op == TokenType::SAME_SHAPE) { warn("المعامل =::= غير مدعوم في الويب (يُعامل كـ ==)", e->line); return "(" + expr(b->left) + " === " + expr(b->right) + ")"; }
            return "(" + expr(b->left) + " " + opStr(b->op, e->line) + " " + expr(b->right) + ")";
        }
        case ExprKind::Logical: {
            auto* b = static_cast<LogicalExpr*>(e.get());
            return "(" + expr(b->left) + (b->op == TokenType::AND ? " && " : " || ") + expr(b->right) + ")";
        }
        case ExprKind::Conditional: {
            auto* c = static_cast<ConditionalExpr*>(e.get());
            return "(" + expr(c->condition) + " ? " + expr(c->whenTrue) + " : " + expr(c->whenFalse) + ")";
        }
        case ExprKind::Unary: {
            auto* u = static_cast<UnaryExpr*>(e.get());
            return "(" + std::string(u->op == TokenType::MINUS ? "-" : "!") + expr(u->right) + ")";
        }
        case ExprKind::Call: {
            auto* c = static_cast<CallExpr*>(e.get());
            return callExpr(c->callee, c->args, e->line);
        }
        case ExprKind::Array: {
            auto* a = static_cast<ArrayExpr*>(e.get());
            return "[" + args(a->elements) + "]";
        }
        case ExprKind::Map: {
            auto* m = static_cast<MapExpr*>(e.get());
            std::string o = "{";
            for (size_t i = 0; i < m->entries.size(); i++) {
                if (i) o += ", ";
                o += "[" + expr(m->entries[i].key) + "]: " + expr(m->entries[i].value);
            }
            return o + "}";
        }
        case ExprKind::Index: {
            auto* x = static_cast<IndexExpr*>(e.get());
            return expr(x->object) + "[" + expr(x->index) + "]";
        }
        case ExprKind::IndexSet: {
            auto* x = static_cast<IndexSetExpr*>(e.get());
            return "(" + expr(x->object) + "[" + expr(x->index) + "] = " + expr(x->value) + ")";
        }
        case ExprKind::Get: {
            auto* x = static_cast<GetExpr*>(e.get());
            return expr(x->object) + (isIdent(x->name) ? "." + x->name : "[" + jsStr(x->name) + "]");
        }
        case ExprKind::Set: {
            auto* x = static_cast<SetExpr*>(e.get());
            return "(" + expr(x->object) + (isIdent(x->name) ? "." + x->name : "[" + jsStr(x->name) + "]") + " = " + expr(x->value) + ")";
        }
        case ExprKind::MethodCall: {
            auto* x = static_cast<MethodCallExpr*>(e.get());
            return "R.method(" + expr(x->object) + ", " + jsStr(x->method) + (x->args.empty() ? "" : ", " + args(x->args)) + ")";
        }
        case ExprKind::CallValue: {
            auto* x = static_cast<CallValueExpr*>(e.get());
            return expr(x->callee) + "(" + args(x->args) + ")";
        }
        case ExprKind::Lambda: {
            warn("دالة مجهولة (fun) داخل تعبير: تُترجَم كما هي", e->line);
            return "undefined /* lambda غير مدعومة */";
        }
        default:
            warn("نوع تعبير غير مدعوم في الترجمة إلى الويب", e->line);
            return "undefined";
    }
}

// ───────────────────────────── العبارات (جسم الدوال) ─────────────────────────────
std::string stmt(const StmtPtr& s, int ind);
std::string block(const StmtPtr& s, int ind) {
    std::string pad(ind * 2, ' ');
    if (!s) return "{}";
    if (s->stmtKind == StmtKind::BlockStmt) {
        g.locals.emplace_back();
        std::string o = "{\n";
        for (auto& st : static_cast<BlockStmt*>(s.get())->statements) o += stmt(st, ind + 1);
        g.locals.pop_back();
        return o + pad + "}";
    }
    g.locals.emplace_back();
    std::string o = "{\n" + stmt(s, ind + 1) + pad + "}";
    g.locals.pop_back();
    return o;
}
std::string stmt(const StmtPtr& s, int ind) {
    std::string pad(ind * 2, ' ');
    if (!s) return "";
    switch (s->stmtKind) {
        case StmtKind::ExpressionStmt: return pad + expr(static_cast<ExpressionStmt*>(s.get())->expr) + ";\n";
        case StmtKind::PrintStmt: {
            auto* p = static_cast<PrintStmt*>(s.get());
            std::string o = pad + "console.log(" + args(p->exprs) + ");\n";
            return o;
        }
        case StmtKind::LetStmt: {
            auto* l = static_cast<LetStmt*>(s.get());
            std::string init = l->initializer ? expr(l->initializer) : "null";
            if (!g.locals.empty()) g.locals.back().insert(l->name);
            return pad + "let " + l->name + " = " + init + ";\n";
        }
        case StmtKind::BlockStmt: return pad + block(s, ind) + "\n";
        case StmtKind::IfStmt: {
            auto* i = static_cast<IfStmt*>(s.get());
            std::string o = pad + "if (" + expr(i->condition) + ") " + block(i->thenBranch, ind);
            if (i->elseBranch) {
                if (i->elseBranch->stmtKind == StmtKind::IfStmt) o += " else " + stmt(i->elseBranch, ind).substr(pad.size());
                else o += " else " + block(i->elseBranch, ind) + "\n";
            } else o += "\n";
            return o;
        }
        case StmtKind::WhileStmt: {
            auto* w = static_cast<WhileStmt*>(s.get());
            if (w->doneBlock) warn("كتلة #done لحلقة while غير مدعومة في الويب وستُهمَل", s->line);
            return pad + "while (" + expr(w->condition) + ") " + block(w->body, ind) + "\n";
        }
        case StmtKind::ForStmt: {
            auto* f = static_cast<ForStmt*>(s.get());
            if (f->doneBlock) warn("كتلة #done لحلقة for غير مدعومة في الويب وستُهمَل", s->line);
            g.locals.emplace_back();
            std::string init = f->initializer ? stmt(f->initializer, 0) : ";";
            while (!init.empty() && (init.back() == '\n' || init.back() == ';')) init.pop_back();
            std::string o = pad + "for (" + init + "; " + (f->condition ? expr(f->condition) : "") + "; " + (f->increment ? expr(f->increment) : "") + ") " + block(f->body, ind) + "\n";
            g.locals.pop_back();
            return o;
        }
        case StmtKind::ForInStmt: {
            auto* f = static_cast<ForInStmt*>(s.get());
            if (f->pattern) warn("تفكيك let+ داخل for…in غير مدعوم في الويب", s->line);
            g.locals.emplace_back(); g.locals.back().insert(f->varName);
            std::string o = pad + "for (const " + f->varName + " of R.iter(" + expr(f->iterable) + ")) " + block(f->body, ind) + "\n";
            g.locals.pop_back();
            return o;
        }
        case StmtKind::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s.get());
            return pad + "return" + (r->value ? " " + expr(r->value) : "") + ";\n";
        }
        case StmtKind::BreakStmt: return pad + "break;\n";
        case StmtKind::ContinueStmt: return pad + "continue;\n";
        case StmtKind::FunctionStmt: {
            auto* f = static_cast<FunctionStmt*>(s.get());
            if (!g.locals.empty()) g.locals.back().insert(f->name);
            g.locals.emplace_back();
            std::string ps;
            for (size_t i = 0; i < f->params.size(); i++) { if (i) ps += ", "; ps += f->params[i]; g.locals.back().insert(f->params[i]); }
            std::string o = pad + "function " + f->name + "(" + ps + ") " + block(f->body, ind) + "\n";
            g.locals.pop_back();
            return o;
        }
        default:
            warn("عبارة غير مدعومة داخل الدوال عند الترجمة إلى الويب (تُحوَّل إلى تعليق)", s->line);
            return pad + "/* عبارة غير مدعومة (سطر " + std::to_string(s->line) + ") */\n";
    }
}

// ───────────────────────────── شجرة الواجهة ─────────────────────────────
struct Binding { std::string id, key; std::string value; bool dynamic; };
struct Handler { std::string id, event, code; };
struct Out {
    std::string html;
    std::vector<Binding> bindings;
    std::vector<Handler> handlers;
    std::map<std::string, std::string> theme;   // role -> color
    std::string rootBg; int canvasW = 0, canvasH = 0; std::string screen;
    std::set<std::string> usedIds;
    int anon = 0;
} out;

// خصائص يعرفها الـ runtime (يجب أن تطابق جدول P في JS؛ يتحقق منها سكربت الاختبار)
const std::set<std::string> kProps = {
    "text","label","title","value","placeholder","type","src","url","href","alt","fit","checked","open","visible","enabled",
    "width","height","min_width","max_width","min_height","max_height","aspect","weight","flex",
    "padding","padding_left","padding_right","padding_top","padding_bottom","padding_x","padding_y",
    "margin","margin_left","margin_right","margin_top","margin_bottom","margin_x","margin_y",
    "gap","row_gap","column_gap","align","valign","justify","direction","columns","wrap",
    "background","bg","fill","color","textColor","radius","border","border_color","opacity","elevation","shadow","shadowColor","shadowY","z",
    "size","bold","italic","underline","font","family","line_height","letter_spacing","transform","max_lines","variant","min","max","step",
    "ratio","icon","name","id","class","role","autofocus","readonly","multiline","rows","cursor","overflow","position","left","top","right","bottom","rotate","scale","display",
    "ariaLabel","aria_label","description","tooltip","lang","dir",
    "canvas","tone","gradient","message","initials","maxlength","secure","loading","error"};
const std::set<std::string> kEventProps = {"onTap","onClick","onChange","onInput","onSubmit","onFocus","onBlur","onHover","onPress","onLongPress","onKey"};
// خصائص تخص الجذر/الشاشة وتُعالَج خارج جدول P
const std::set<std::string> kRootProps = {"screen","canvas_width","canvas_height","canvas","bg","background","canvas_bg","dark","theme","mask","style","class","extends"};

struct KindInfo { const char* tag; const char* cls; bool voidEl; };
KindInfo kindInfo(const std::string& tag) {
    static const std::map<std::string, KindInfo> m = {
        {"column",{"div","col",false}},{"row",{"div","row",false}},{"stack",{"div","stack",false}},{"box",{"div","box",false}},
        {"container",{"div","box",false}},{"panel",{"div","box",false}},{"frame",{"div","box",false}},{"card",{"div","card",false}},
        {"grid",{"div","grid",false}},{"wrap",{"div","wrap",false}},{"spacer",{"div","spacer",false}},{"scaffold",{"div","scaffold",false}},
        {"splash",{"div","splash",false}},{"header",{"header","header",false}},{"topbar",{"header","topbar",false}},
        {"bottombar",{"footer","bottombar",false}},{"drawer",{"aside","drawer",false}},{"menu",{"nav","menu",false}},
        {"menuitem",{"button","menuitem",false}},{"table",{"div","table",false}},{"tablerow",{"div","tablerow",false}},
        {"text",{"p","text",false}},{"button",{"button","button",false}},{"iconbutton",{"button","button",false}},
        {"image",{"img","image",true}},{"input",{"input","input",true}},{"textfield",{"input","input",true}},
        {"textarea",{"textarea","input",false}},{"checkbox",{"input","checkbox",true}},{"switch",{"input","switch",true}},
        {"divider",{"hr","divider",true}},{"progress",{"progress","progress",false}},{"link",{"a","link",false}},
        {"video",{"video","video",false}},{"audio",{"audio","audio",false}},{"webview",{"iframe","webview",false}},
        {"dialog",{"div","dialog",false}},{"modal",{"div","dialog",false}},{"badge",{"span","badge",false}},{"tag",{"span","tag",false}},
        {"chip",{"span","tag",false}},{"kbd",{"kbd","kbd",false}},{"avatar",{"div","avatar",false}},{"select",{"select","input",false}},
        {"slider",{"input","slider",true}},{"radio",{"input","radio",true}},{"list",{"ul","list",false}},{"listitem",{"li","listitem",false}},
        {"icon",{"span","icon",false}},{"search",{"input","input",true}},{"date",{"input","input",true}},{"time",{"input","input",true}},
        {"banner",{"div","banner",false}},{"tabs",{"div","tabs",false}},{"tabitem",{"button","tabitem",false}},{"tooltip",{"span","tooltip",false}},
        {"skeleton",{"div","skeleton",false}},{"spinner",{"div","spinner",false}},{"loop",{"div","box",false}},{"view",{"div","box",false}}};
    std::string k; for (char c : tag) k += (char)tolower((unsigned char)c);
    auto it = m.find(k);
    if (it != m.end()) return it->second;
    return {"div", "custom", false};
}
bool isKnownKind(const std::string& tag) {
    std::string k; for (char c : tag) k += (char)tolower((unsigned char)c);
    return std::string(kindInfo(tag).cls) != "custom" || k == "custom";
}

bool isConstExpr(const ExprPtr& e) {
    if (!e) return true;
    switch (e->exprKind) {
        case ExprKind::Literal: return true;
        case ExprKind::Unary: return isConstExpr(static_cast<UnaryExpr*>(e.get())->right);
        case ExprKind::Array: for (auto& x : static_cast<ArrayExpr*>(e.get())->elements) if (!isConstExpr(x)) return false; return true;
        default: return false;
    }
}
bool literalString(const ExprPtr& e, std::string& s) {
    if (!e || e->exprKind != ExprKind::Literal) return false;
    auto* l = static_cast<LiteralExpr*>(e.get());
    if (l->kind == LiteralExpr::Kind::STRING) { s = l->str; return true; }
    if (l->kind == LiteralExpr::Kind::NUMBER) { s = numStr(l->number); return true; }
    if (l->kind == LiteralExpr::Kind::BOOL) { s = l->boolean ? "true" : "false"; return true; }
    return false;
}

// ── اشتقاق ثيم من لون بذرة (تقريب مبسّط لـ <Palette seed=…>: المحرّك يستعمل لوحة tonal كاملة مع فحص WCAG) ──
bool parseHex(const std::string& h, int rgb[3]) {
    std::string x = h; if (!x.empty() && x[0] == '#') x = x.substr(1);
    if (x.size() == 3) x = std::string{x[0],x[0],x[1],x[1],x[2],x[2]};
    if (x.size() != 6) return false;
    for (int i = 0; i < 3; i++) { char* e; rgb[i] = (int)strtol(x.substr(i * 2, 2).c_str(), &e, 16); if (*e) return false; }
    return true;
}
std::string mixHex(const int a[3], const int b[3], double t) {
    char buf[8]; int c[3]; for (int i = 0; i < 3; i++) c[i] = (int)(a[i] * (1 - t) + b[i] * t + .5);
    snprintf(buf, sizeof buf, "#%02X%02X%02X", c[0], c[1], c[2]); return buf;
}
double lumOf(const int c[3]) {
    double f[3]; for (int i = 0; i < 3; i++) { double v = c[i] / 255.0; f[i] = v <= .03928 ? v / 12.92 : pow((v + .055) / 1.055, 2.4); }
    return .2126 * f[0] + .7152 * f[1] + .0722 * f[2];
}
void deriveFromSeed(const std::string& seed, bool dark, std::map<std::string, std::string>& t) {
    int s[3], white[3] = {255,255,255}, black[3] = {0,0,0}, ink[3] = {30,30,46};
    if (!parseHex(seed, s)) { warn("seed غير صالح في <Palette>: " + seed, 0); return; }
    auto setIf = [&](const char* k, const std::string& v) { if (!t.count(k)) t[k] = v; };
    setIf("primary", mixHex(s, s, 0));
    if (dark) {
        int bg[3]; parseHex(mixHex(s, black, .88), bg); int sf[3]; parseHex(mixHex(s, black, .8), sf);
        setIf("background", mixHex(s, black, .9)); setIf("surface", mixHex(s, black, .8));
        setIf("text", "#F2F2FA"); setIf("text_muted", mixHex(s, white, .65)); setIf("border", mixHex(s, black, .6));
    } else {
        setIf("background", mixHex(s, white, .94)); setIf("surface", "#FFFFFF");
        setIf("text", mixHex(s, ink, .8)); setIf("text_muted", mixHex(ink, white, .45)); setIf("border", mixHex(s, white, .8));
    }
    setIf("secondary", mixHex(s, white, .35));
}
std::string uniqueId(const std::string& name) {
    std::string base = name.empty() ? "n" + std::to_string(++out.anon) : name;
    std::string id = "i-" + base, cand = id; int k = 2;
    while (!out.usedIds.insert(cand).second) cand = id + "-" + std::to_string(k++);
    return cand;
}

// يحوّل تعبير معالج حدث إلى جسم دالة JS
std::string handlerCode(const ExprPtr& e) {
    if (e && e->exprKind == ExprKind::Variable) {            // onTap=increment;  → اسم دالة بلا استدعاء
        std::string n = static_cast<VariableExpr*>(e.get())->name;
        if (g.globals.count(n)) return n + "(ev)";
        if (kActions.count(n)) { warn("الإجراء `" + n + "` يحتاج وسائط (مثل " + n + "(cell))", e->line); return "";}
        warn("معالج الحدث `" + n + "` غير معرَّف", e->line); return "";
    }
    return expr(e);
}

void emitView(const std::shared_ptr<ViewStmt>& v, int depth, bool isRoot) {
    std::string tagName = v->kindTag.empty() ? (v->role == UiRole::LOOP ? "loop" : "box") : v->kindTag;
    if (v->role == UiRole::LOOP) tagName = "loop";
    KindInfo ki = kindInfo(tagName);
    if (!isKnownKind(tagName))
        warn("المكوّن `" + tagName + "` غير معروف لمترجم الويب: يُترجَم إلى <div> عام مع data-indsin-kind", v->line);
    std::string id = uniqueId(v->name);
    std::string tag = ki.tag;

    // text/label كابن ثابت (للقراءة بدون JS ولمحركات البحث)
    std::string staticText, staticSrc;
    std::string tc = (std::string(ki.cls) == "button" || std::string(ki.cls) == "menuitem" || std::string(ki.cls) == "tabitem") ? "label" : "text";
    bool boolInputLike = false;
    for (auto& a : v->attrs) {
        if (a.key == tc && literalString(a.value, staticText)) {}
        if ((a.key == "src" || a.key == "url") && literalString(a.value, staticSrc)) {}
    }
    (void)boolInputLike;

    // سمات خاصة بالوسم الأصلي
    std::string attrs = " id=\"" + id + "\" class=\"in-" + ki.cls + "\" data-indsin-kind=\"" + htmlEsc(tagName) + "\"";
    if (std::string(ki.cls) == "checkbox") attrs += " type=\"checkbox\"";
    if (std::string(ki.cls) == "switch") attrs += " type=\"checkbox\" role=\"switch\"";
    if (std::string(ki.cls) == "radio") attrs += " type=\"radio\"";
    if (std::string(ki.cls) == "slider") attrs += " type=\"range\"";
    if (std::string(ki.cls) == "button" || std::string(ki.cls) == "menuitem" || std::string(ki.cls) == "tabitem") attrs += " type=\"button\"";
    if (std::string(ki.cls) == "dialog") attrs += " role=\"dialog\" aria-modal=\"true\"";
    if (std::string(ki.cls) == "table") attrs += " role=\"table\"";
    if (std::string(ki.cls) == "tablerow") attrs += " role=\"row\"";
    if (std::string(ki.cls) == "banner") attrs += " role=\"status\"";
    if (std::string(ki.cls) == "image" && !staticSrc.empty()) attrs += " src=\"" + htmlEsc(staticSrc) + "\"";
    if (std::string(ki.cls) == "image") attrs += " alt=\"\"";
    if (std::string(ki.cls) == "text") {
        std::string size; for (auto& a : v->attrs) if (a.key == "size") literalString(a.value, size);
        if (size == "heading") tag = "h1"; else if (size == "title") tag = "h2"; else if (size == "subtitle") tag = "h3";
    }
    if (std::string(ki.cls) == "input" && tag == "input") {
        for (auto& a : v->attrs) if (a.key == "type") { std::string t; if (literalString(a.value, t)) attrs += " type=\"" + htmlEsc(t) + "\""; }
        if (tagName == "Search" || tagName == "search") attrs += " type=\"search\"";
        if (tagName == "Date" || tagName == "date") attrs += " type=\"date\"";
        if (tagName == "Time" || tagName == "time") attrs += " type=\"time\"";
    }

    std::string pad((size_t)depth * 2, ' ');
    out.html += pad + "<" + tag + attrs + ">";

    // الخصائص
    for (auto& a : v->attrs) {
        const std::string& k = a.key;
        if (kEventProps.count(k)) {
            std::string ev = (k == "onTap" || k == "onClick" || k == "onPress") ? "click" : (k == "onChange") ? "change" :
                (k == "onInput") ? "input" : (k == "onSubmit") ? "submit" : (k == "onFocus") ? "focus" : (k == "onBlur") ? "blur" :
                (k == "onHover") ? "mouseenter" : (k == "onLongPress") ? "contextmenu" : "keydown";
            std::string code = handlerCode(a.value);
            if (!code.empty()) out.handlers.push_back({id, ev, code});
            continue;
        }
        if (isRoot && kRootProps.count(k) && !kProps.count(k) && k != "canvas") continue;
        if (isRoot && (k == "screen" || k == "canvas_width" || k == "canvas_height" || k == "canvas" || k == "canvas_bg")) {
            std::string s; literalString(a.value, s);
            if (k == "screen") out.screen = s; else if (k == "canvas_width") out.canvasW = atoi(s.c_str());
            else if (k == "canvas_height") out.canvasH = atoi(s.c_str());
            else { bool dynC = !isConstExpr(a.value); out.bindings.push_back({"indsin-root", "canvas", dynC ? "()=>" + expr(a.value) : expr(a.value), dynC}); }
            continue;
        }
        if (k == "mask" || k == "extends" || k == "style") continue;
        if (!kProps.count(k)) {
            warn("الخاصية `" + k + "` على `" + tagName + "` غير مدعومة في الترجمة إلى الويب (تُحفَظ كـ data-in-" + k + ")", a.line);
        }
        bool dyn = !isConstExpr(a.value);
        std::string code = expr(a.value);
        out.bindings.push_back({id, k, dyn ? "()=>" + code : code, dyn});
        // ربط ثنائي الاتجاه للحقول: value=cell / checked=cell
        bool field = std::string(ki.cls) == "input" || std::string(ki.cls) == "checkbox" || std::string(ki.cls) == "switch" ||
                     std::string(ki.cls) == "slider" || std::string(ki.cls) == "radio";
        if (field && (k == "value" || k == "checked") && a.value && a.value->exprKind == ExprKind::Variable) {
            const std::string& n = static_cast<VariableExpr*>(a.value.get())->name;
            if (g.warps.count(n) && !g.isLocal(n))
                out.handlers.push_back({id, "__bind__", jsStr(n) + "," + jsStr(k)});
        }
    }
    for (auto& b : v->attrs) (void)b;
    if (!staticText.empty() && (tc == "text" || tc == "label")) out.html += htmlEsc(staticText);
    if (ki.voidEl) { out.html += "\n"; return; }
    if (!v->children.empty()) out.html += "\n";
    for (auto& c : v->children) emitView(c, depth + 1, false);
    out.html += (v->children.empty() ? "" : pad) + "</" + tag + ">\n";
}

// يجمع عناصر المستوى الأعلى (داخل الحاويات أيضاً): warp/let/fun/theme/view
std::vector<std::shared_ptr<ViewStmt>> g_roots;
std::string g_userJs;
std::vector<std::string> g_warpInitJs;

void collectDecls(const std::vector<StmtPtr>& body);
void handleStmt(const StmtPtr& s) {
    switch (s->stmtKind) {
        case StmtKind::WarpStmt: {
            auto* w = static_cast<WarpStmt*>(s.get());
            g.warps.insert(w->name); g.warpInit[w->name] = w->initializer;
            break;
        }
        case StmtKind::LetStmt: g.globals.insert(static_cast<LetStmt*>(s.get())->name); break;
        case StmtKind::FunctionStmt: g.globals.insert(static_cast<FunctionStmt*>(s.get())->name); break;
        case StmtKind::ContainerStmt: collectDecls(static_cast<ContainerStmt*>(s.get())->body); break;
        default: break;
    }
}
void collectDecls(const std::vector<StmtPtr>& body) { for (auto& s : body) handleStmt(s); }

void translateTop(const std::vector<StmtPtr>& body) {
    for (auto& s : body) {
        switch (s->stmtKind) {
            case StmtKind::ViewStmt: g_roots.push_back(std::static_pointer_cast<ViewStmt>(s)); break;
            case StmtKind::ContainerStmt: translateTop(static_cast<ContainerStmt*>(s.get())->body); break;
            case StmtKind::WarpStmt: {
                auto* w = static_cast<WarpStmt*>(s.get());
                g_warpInitJs.push_back(jsStr(w->name) + ": " + expr(w->initializer));
                break;
            }
            case StmtKind::LetStmt: {
                auto* l = static_cast<LetStmt*>(s.get());
                g_userJs += "var " + l->name + " = " + (l->initializer ? expr(l->initializer) : "null") + ";\n";
                break;
            }
            case StmtKind::FunctionStmt: g_userJs += stmt(s, 0); break;
            case StmtKind::ThemeStmt: {
                auto* t = static_cast<ThemeStmt*>(s.get());
                bool active = false, dark = false; std::map<std::string, std::string> vals;
                for (auto& a : t->attrs) {
                    std::string v;
                    if (a.key == "active") { if (literalString(a.value, v)) active = v == "true"; }
                    else if (a.key == "dark") { if (literalString(a.value, v)) dark = v == "true"; }
                    else if (a.key == "mode") { if (literalString(a.value, v)) dark = v == "dark"; }
                    else if (literalString(a.value, v)) vals[a.key] = v;
                }
                if (vals.count("seed")) { std::string sd = vals["seed"]; vals.erase("seed"); deriveFromSeed(sd, dark, vals); }
                if (active || out.theme.empty()) for (auto& kv : vals) out.theme[kv.first] = kv.second;
                break;
            }
            case StmtKind::ImportStmt: warn("@import غير مدعوم في الترجمة إلى الويب: مكتبات Rin لا تُضمَّن", s->line); break;
            case StmtKind::ExpressionStmt: case StmtKind::PrintStmt: case StmtKind::IfStmt:
                g_userJs += stmt(s, 0); break;
            default: warn("عبارة على مستوى الملف غير مدعومة في الترجمة إلى الويب وتم تجاهلها", s->line); break;
        }
    }
}

// ───────────────────────────── Runtime + CSS (جزء ثابت) ─────────────────────────────
#include "indsin2web_runtime.inc"

std::string readAll(const std::string& p, bool& ok) {
    std::ifstream f(p, std::ios::binary); ok = (bool)f;
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

} // namespace

int main(int argc, char** argv) {
    std::string in, outPath, title, lang = "en", dir = "", report;
    bool check = false, strict = false, fragment = false, desugarOnly = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char* n) -> std::string { if (i + 1 >= argc) { fprintf(stderr, "الخيار %s يحتاج قيمة\n", n); exit(64); } return argv[++i]; };
        if (a == "-o") outPath = need("-o"); else if (a == "--check") check = true; else if (a == "--strict") strict = true;
        else if (a == "--title") title = need("--title"); else if (a == "--lang") lang = need("--lang"); else if (a == "--dir") dir = need("--dir");
        else if (a == "--fragment") fragment = true; else if (a == "--desugar") desugarOnly = true; else if (a == "--report") report = need("--report");
        else if (a == "-h" || a == "--help") {
            puts("indsin2web — مترجم Indsin إلى HTML/CSS/JS\n  indsin2web input.rin [-o out.html] [--check] [--strict] [--title T] [--lang ar] [--dir rtl|ltr] [--fragment] [--report r.json]");
            return 0;
        } else if (!a.empty() && a[0] == '-') { fprintf(stderr, "خيار غير معروف: %s\n", a.c_str()); return 64; }
        else in = a;
    }
    if (in.empty()) { fprintf(stderr, "usage: indsin2web input.rin [-o out.html]\n"); return 64; }
    bool ok; std::string src = readAll(in, ok);
    if (!ok) { fprintf(stderr, "%s: تعذّرت القراءة\n", in.c_str()); return 1; }
    if (dir.empty()) dir = (lang == "ar" || lang == "he" || lang == "fa" || lang == "ur") ? "rtl" : "ltr";

    std::vector<StmtPtr> program;
    try {
        std::string desugared = indsin::tags::desugar(src);
        if (desugarOnly) { std::cout << desugared; return 0; }
        Lexer lexer(desugared, in);
        Parser parser(lexer.scanTokens(), in);
        program = parser.parse();
    } catch (const indsin::tags::TagError& e) {
        fprintf(stderr, "%s:%d: error: %s\n", in.c_str(), e.line, e.message.c_str()); return 1;
    } catch (const RinError& e) {
        fprintf(stderr, "%s:%d: error: %s\n", in.c_str(), e.line, e.message.c_str()); return 1;
    } catch (const std::exception& e) {
        fprintf(stderr, "%s: error: %s\n", in.c_str(), e.what()); return 1;
    }

    collectDecls(program);
    translateTop(program);
    if (g_roots.empty()) { fprintf(stderr, "%s: error: لا يوجد جذر واجهة (@view/@loop/وسم) في الملف\n", in.c_str()); return 1; }
    for (auto& r : g_roots) emitView(r, 2, true);

    // خصائص الجذر: bg → خلفية الصفحة
    for (auto& b : out.bindings)
        if (b.id == "i-" + g_roots[0]->name && (b.key == "bg" || b.key == "background") && !b.dynamic) { /* يبقى على العنصر نفسه */ }

    for (auto& d : g_diags) fprintf(stderr, "%s:%d: warning: %s\n", in.c_str(), d.line, d.message.c_str());
    if (!report.empty()) {
        std::ofstream r(report);
        r << "{\n  \"file\": " << jsStr(in) << ",\n  \"elements\": " << out.usedIds.size() << ",\n  \"bindings\": " << out.bindings.size()
          << ",\n  \"handlers\": " << out.handlers.size() << ",\n  \"warnings\": [";
        for (size_t i = 0; i < g_diags.size(); i++) r << (i ? "," : "") << "\n    {\"line\": " << g_diags[i].line << ", \"message\": " << jsStr(g_diags[i].message) << "}";
        r << (g_diags.empty() ? "" : "\n  ") << "]\n}\n";
    }
    if (check) { fprintf(stderr, "%s: OK (%zu عنصر، %zu ربط، %zu معالج، %zu تحذير)\n", in.c_str(), out.usedIds.size(), out.bindings.size(), out.handlers.size(), g_diags.size());
                 return (strict && !g_diags.empty()) ? 2 : 0; }

    // ── التجميع ──
    std::string themeCss = ":root{";
    for (auto& kv : out.theme) themeCss += "--in-" + kv.first + ":" + kv.second + ";";
    {   // لون النص فوق primary (تباين WCAG تقريباً)
        auto it = out.theme.find("primary"); int c[3];
        if (it != out.theme.end() && parseHex(it->second, c)) themeCss += "--in-on-primary:" + std::string(lumOf(c) > .179 ? "#000000" : "#FFFFFF") + ";";
    }
    themeCss += "}";
    std::string screenCss;
    {
        int w = out.canvasW, h = out.canvasH;
        static const std::map<std::string, std::pair<int,int>> presets = {{"watch",{184,224}},{"phone_small",{360,640}},{"phone",{390,844}},
            {"phone_large",{430,932}},{"foldable",{673,841}},{"square",{600,600}},{"tablet",{768,1024}}};
        if (!out.screen.empty()) { auto it = presets.find(out.screen); if (it != presets.end()) { w = it->second.first; h = it->second.second; } }
        if (w) screenCss += ".in-screen{max-width:" + std::to_string(w) + "px;margin-inline:auto}";
        if (h) screenCss += ".in-screen{min-height:" + std::to_string(h) + "px}";
    }
    std::string js = "\"use strict\";\n" + std::string(kRuntimeJs) + "\n";
    js += "const S = IN.state({" ;
    for (size_t i = 0; i < g_warpInitJs.size(); i++) js += (i ? ", " : "") + g_warpInitJs[i];
    js += "});\nconst A = IN.actions(S), R = IN.natives, indsin = IN.indsin;\n" + g_userJs;
    js += "IN.boot(S, [\n";
    for (size_t i = 0; i < out.bindings.size(); i++) {
        auto& b = out.bindings[i];
        js += "  [" + jsStr(b.id) + ", " + jsStr(b.key) + ", " + b.value + "]" + (i + 1 < out.bindings.size() ? ",\n" : "\n");
    }
    js += "], [\n";
    for (size_t i = 0; i < out.handlers.size(); i++) {
        auto& h = out.handlers[i];
        if (h.event == "__bind__") js += "  [" + jsStr(h.id) + ", \"__bind__\", " + h.code + "]";
        else js += "  [" + jsStr(h.id) + ", " + jsStr(h.event) + ", (ev)=>{ " + h.code + "; }]";
        js += i + 1 < out.handlers.size() ? ",\n" : "\n";
    }
    js += "]);\n";

    std::string page;
    if (title.empty()) { title = in; auto p = title.find_last_of("/\\"); if (p != std::string::npos) title = title.substr(p + 1); auto d = title.rfind('.'); if (d != std::string::npos) title = title.substr(0, d); }
    if (fragment) {
        page = "<style>" + std::string(kBaseCss) + themeCss + screenCss + "</style>\n<div class=\"in-screen\" id=\"indsin-root\">\n" + out.html + "</div>\n<script>\n" + js + "</script>\n";
    } else {
        page = "<!doctype html>\n<html lang=\"" + htmlEsc(lang) + "\" dir=\"" + htmlEsc(dir) + "\">\n<head>\n<meta charset=\"utf-8\">\n"
               "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<meta name=\"generator\" content=\"indsin2web\">\n<title>" + htmlEsc(title) + "</title>\n<style>\n" +
               kBaseCss + themeCss + screenCss + "\n</style>\n</head>\n<body>\n<div class=\"in-screen\" id=\"indsin-root\">\n" + out.html +
               "</div>\n<noscript>هذه الصفحة تحتاج JavaScript لتطبيق الأنماط والتفاعل.</noscript>\n<script>\n" + js + "</script>\n</body>\n</html>\n";
    }
    if (outPath.empty() || outPath == "-") std::cout << page;
    else { std::ofstream f(outPath, std::ios::binary); f << page; if (!f) { fprintf(stderr, "تعذّرت الكتابة إلى %s\n", outPath.c_str()); return 1; } }
    if (strict && !g_diags.empty()) return 2;
    return 0;
}
