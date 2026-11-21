// ============================================================================
//  rin_print_layout.cpp — مسافات · تنسيق · تعليم · تخطيط للطباعة (docs/print-layout.md)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp ويُسجَّل عبر registerNativesPrintLayout().
//  المحلل يفكّك سمات print الجديدة إلى نداء واحد  __pfx(sep, pretty, <21 سمة>, قيم...)
//  فيعمل على المفسّر الشجري وRinVM بلا أي تعديل فيهما. نفس المحرّك متاح كدوال:
//    layoutText(value, {opts})   fmtNumber(n, spec)   showSpaces(s)
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace rin {
RinError diagErr(diag::Code code, int line, std::string message);

namespace printlayout {

static const char* kAttrs[40] = {"indent", "gap", "pad", "before", "after", "visible", "squeeze", "trim",
                                 "fmt", "case", "wrap", "cut", "mark", "hl", "hlwrap", "box", "title",
                                 "cols", "colgap", "page", "place",
                                 "leader", "justify", "inalign", "rule", "numbers", "beside", "table", "shadow", "cutat",
                                 "tabs", "dedent", "maxlines", "ends", "ruler", "hlcase", "focus", "tree", "bars", "diff"};

// ------------------------------------------------------------ utf-8 / width
static unsigned decodeCp(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + n > s.size()) n = 1;
    unsigned cp = n == 1 ? c : (c & (0xFF >> (n + 1)));
    for (size_t k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += n;
    return cp;
}
static std::vector<std::string> splitCp(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) { size_t st = i; decodeCp(s, i); out.push_back(s.substr(st, i - st)); }
    return out;
}
static int cpWidth(unsigned cp) {
    if ((cp >= 0x300 && cp <= 0x36F) || (cp >= 0x64B && cp <= 0x65F) || cp == 0x670 ||
        (cp >= 0x6D6 && cp <= 0x6DC) || (cp >= 0x6DF && cp <= 0x6E4) || cp == 0x6E7 || cp == 0x6E8 ||
        (cp >= 0x6EA && cp <= 0x6ED) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
        (cp >= 0xFE00 && cp <= 0xFE0F)) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD)) return 2;
    return 1;
}
static int dispWidth(const std::string& s) {   // عرض الخلايا (يتجاهل أكواد ANSI والحركات والأحرف الصفرية)
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2; while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) i++; i++; continue;
        }
        w += cpWidth(decodeCp(s, i));
    }
    return w;
}
static std::string sp(int n) { return n > 0 ? std::string(static_cast<size_t>(n), ' ') : std::string(); }
static std::string rep(const std::string& s, int n) { std::string o; for (int i = 0; i < n; i++) o += s; return o; }
static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out; size_t st = 0;
    for (;;) { size_t p = s.find('\n', st); if (p == std::string::npos) { out.push_back(s.substr(st)); break; } out.push_back(s.substr(st, p - st)); st = p + 1; }
    return out;
}

// ------------------------------------------------------------ number format
static std::string groupDigits(const std::string& digits) {
    std::string out; int n = static_cast<int>(digits.size());
    for (int i = 0; i < n; i++) { out += digits[static_cast<size_t>(i)]; int left = n - 1 - i; if (left > 0 && left % 3 == 0) out += ','; }
    return out;
}
static std::string fmtNum(double x, std::string spec, int line, const std::string& who) {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    if (spec == "money") spec = ",.2"; else if (spec == "percent" || spec == "pct") spec = ".0%";
    else if (spec == "int") spec = ".0"; else if (spec == "compact") spec = "k";
    auto bad = [&]() { return diagErr(diag::Code::E0007_InvalidArguments, line, who + ": صيغة fmt غير صالحة \"" + spec + "\" (مثال: \",.2\" أو \"05\" أو \".1%\" أو \"x\" أو \"k\" أو \"money\")"); };
    size_t i = 0; bool plus = false, zero = false, comma = false; int width = 0, prec = -1; char type = 0;
    if (i < spec.size() && spec[i] == '+') { plus = true; i++; }
    if (i < spec.size() && spec[i] == '0') { zero = true; i++; }
    while (i < spec.size() && isdigit(static_cast<unsigned char>(spec[i]))) { width = width * 10 + (spec[i] - '0'); i++; if (width > 1000) throw bad(); }
    if (i < spec.size() && spec[i] == ',') { comma = true; i++; }
    if (i < spec.size() && spec[i] == '.') { i++; prec = 0; bool any = false; while (i < spec.size() && isdigit(static_cast<unsigned char>(spec[i]))) { prec = prec * 10 + (spec[i] - '0'); i++; any = true; if (prec > 30) throw bad(); } if (!any) throw bad(); }
    if (i < spec.size()) { type = spec[i]; i++; }
    if (i != spec.size()) throw bad();
    if (type && !strchr("f%eExXbok", type)) throw bad();
    bool neg = x < 0; double ax = std::fabs(x);
    std::string body, suffix;
    char buf[512];
    if (type == 'x' || type == 'X' || type == 'b' || type == 'o') {
        if (ax != std::floor(ax) || ax > 9e15) throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": fmt \"" + std::string(1, type) + "\" يتطلّب عدداً صحيحاً");
        unsigned long long v = static_cast<unsigned long long>(ax);
        if (type == 'b') { if (!v) body = "0"; while (v) { body = char('0' + (v & 1)) + body; v >>= 1; } }
        else { snprintf(buf, sizeof buf, type == 'x' ? "%llx" : type == 'X' ? "%llX" : "%llo", v); body = buf; }
    } else if (type == 'e' || type == 'E') {
        snprintf(buf, sizeof buf, type == 'e' ? "%.*e" : "%.*E", prec < 0 ? 2 : prec, ax); body = buf;
    } else if (type == 'k') {
        const char* units[] = {"", "K", "M", "B", "T"}; int u = 0; double v = ax;
        while (v >= 1000 && u < 4) { v /= 1000; u++; }
        if (prec >= 0) snprintf(buf, sizeof buf, "%.*f", prec, v);
        else { snprintf(buf, sizeof buf, "%.1f", v); std::string t = buf; if (t.size() > 2 && t.substr(t.size() - 2) == ".0") t.resize(t.size() - 2); snprintf(buf, sizeof buf, "%s", t.c_str()); }
        body = buf; suffix = units[u];
    } else {
        if (type == '%') { ax *= 100; suffix = "%"; }
        if (prec >= 0) snprintf(buf, sizeof buf, "%.*f", prec, ax);
        else { std::string d = Value::num(ax).toDisplayString(); snprintf(buf, sizeof buf, "%s", d.c_str()); }
        body = buf;
        if (comma || true) {
            size_t dot = body.find_first_of(".eE");
            std::string ip = dot == std::string::npos ? body : body.substr(0, dot), rest = dot == std::string::npos ? "" : body.substr(dot);
            bool allDig = !ip.empty() && std::all_of(ip.begin(), ip.end(), [](char c) { return isdigit(static_cast<unsigned char>(c)); });
            if (comma && allDig) body = groupDigits(ip) + rest;
        }
    }
    body += suffix;
    bool isZero = body.find_first_of("123456789") == std::string::npos;
    std::string sign = (neg && !isZero) ? "-" : (plus ? "+" : "");
    int len = static_cast<int>(sign.size() + body.size());
    if (width > len) { if (zero) return sign + std::string(static_cast<size_t>(width - len), '0') + body; return sp(width - len) + sign + body; }
    return sign + body;
}

// ------------------------------------------------------------ text helpers
static std::string caseLine(const std::string& s, const std::string& mode) {
    std::string o = s;
    auto up = [](char c) { return (c >= 'a' && c <= 'z') ? char(c - 32) : c; };
    auto lo = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
    if (mode == "upper") { for (auto& c : o) c = up(c); return o; }
    if (mode == "lower") { for (auto& c : o) c = lo(c); return o; }
    if (mode == "swap") { for (auto& c : o) c = (c >= 'a' && c <= 'z') ? up(c) : lo(c); return o; }
    if (mode == "title") { bool st = true; for (auto& c : o) { if (c == ' ' || c == '\t') st = true; else { c = st ? up(c) : lo(c); st = false; } } return o; }
    if (mode == "sentence") { bool first = true; for (auto& c : o) { if (first && c != ' ' && c != '\t') { c = up(c); first = false; } else c = lo(c); } return o; }
    // snake / kebab / camel: كلمات من أحرف/أرقام (والبايتات غير ASCII تُعدّ حروفاً) مع فصل camelCase
    std::vector<std::string> words; std::string cur; bool prevLower = false;
    for (unsigned char c : s) {
        bool wc = isalnum(c) || c >= 0x80;
        if (!wc) { if (!cur.empty()) words.push_back(cur); cur.clear(); prevLower = false; continue; }
        if (c >= 'A' && c <= 'Z' && prevLower) { words.push_back(cur); cur.clear(); }
        cur += static_cast<char>(lo(static_cast<char>(c)));
        prevLower = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }
    if (!cur.empty()) words.push_back(cur);
    std::string r;
    for (size_t i = 0; i < words.size(); i++) {
        std::string w = words[i];
        if (mode == "camel" && i > 0 && !w.empty()) w[0] = up(w[0]);
        if (i > 0 && mode != "camel") r += mode == "snake" ? "_" : "-";
        r += w;
    }
    return r;
}
static std::string trimLine(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}
static std::string squeezeLine(const std::string& s) {
    size_t lead = 0; while (lead < s.size() && (s[lead] == ' ' || s[lead] == '\t')) lead++;
    std::string o = s.substr(0, lead); bool prev = false;
    for (size_t i = lead; i < s.size(); i++) {
        bool w = s[i] == ' ' || s[i] == '\t';
        if (w) { if (!prev) o += ' '; prev = true; } else { o += s[i]; prev = false; }
    }
    return o;
}
static std::string cutLine(const std::string& s, int n, const std::string& mode = "end") {
    if (dispWidth(s) <= n) return s;
    const std::string el = "\xE2\x80\xA6";
    auto cps = splitCp(s);
    auto wOf = [](const std::string& cp) { size_t k = 0; return cpWidth(decodeCp(cp, k)); };
    if (mode == "start") {   // …النهاية
        std::string o; int w = 0;
        for (size_t i = cps.size(); i-- > 0;) { int cw = wOf(cps[i]); if (w + cw > n - 1) break; o = cps[i] + o; w += cw; }
        return el + o;
    }
    if (mode == "middle") {  // بداية…نهاية
        int head = (n - 1 + 1) / 2, tail = n - 1 - head;
        std::string a, b; int w = 0;
        for (auto& cp : cps) { int cw = wOf(cp); if (w + cw > head) break; a += cp; w += cw; }
        w = 0;
        for (size_t i = cps.size(); i-- > 0;) { int cw = wOf(cps[i]); if (w + cw > tail) break; b = cps[i] + b; w += cw; }
        return a + el + b;
    }
    std::string o; int w = 0;
    for (auto& cp : cps) { int cw = wOf(cp); if (w + cw > n - 1) break; o += cp; w += cw; }
    return o + el;
}
static std::string justifyLine(const std::string& s, int width) {   // توزيع المسافات حتى العرض
    std::vector<std::string> ws; size_t i = 0;
    while (i <= s.size()) { size_t p = s.find(' ', i); if (p == std::string::npos) p = s.size(); if (p > i) ws.push_back(s.substr(i, p - i)); i = p + 1; }
    if (ws.size() < 2) return s;
    int tot = 0; for (auto& w : ws) tot += dispWidth(w);
    int gaps = static_cast<int>(ws.size()) - 1, space = width - tot;
    if (space < gaps) return s;
    std::string o;
    for (size_t k = 0; k < ws.size(); k++) { o += ws[k]; if (static_cast<int>(k) < gaps) o += sp(space / gaps + (static_cast<int>(k) < space % gaps ? 1 : 0)); }
    return o;
}
static std::vector<std::string> wrapLine(const std::string& line, int width) {
    std::vector<std::string> out;
    if (width <= 0 || dispWidth(line) <= width) { out.push_back(line); return out; }
    std::string cur; int curW = 0;
    auto flush = [&]() { out.push_back(cur); cur.clear(); curW = 0; };
    size_t i = 0;
    while (i <= line.size()) {
        size_t p = line.find(' ', i); if (p == std::string::npos) p = line.size();
        std::string w = line.substr(i, p - i); i = p + 1;
        if (w.empty()) continue;
        int ww = dispWidth(w);
        if (ww > width) {
            if (curW) flush();
            for (auto& cp : splitCp(w)) { size_t k = 0; int cw = cpWidth(decodeCp(cp, k)); if (curW + cw > width) flush(); cur += cp; curW += cw; }
        } else if (!curW) { cur = w; curW = ww; }
        else if (curW + 1 + ww <= width) { cur += " " + w; curW += 1 + ww; }
        else { flush(); cur = w; curW = ww; }
    }
    if (curW || out.empty()) out.push_back(cur);
    return out;
}
static std::string hlLine(const std::string& s, const std::vector<std::string>& pats, const std::string& o, const std::string& c, bool cs = true) {
    std::string out;
    auto lc = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? char(ch + 32) : ch; };
    auto matchAt = [&](size_t i, const std::string& p) {
        if (i + p.size() > s.size()) return false;
        for (size_t k = 0; k < p.size(); k++) if (cs ? s[i + k] != p[k] : lc(s[i + k]) != lc(p[k])) return false;
        return true;
    };
    for (size_t i = 0; i < s.size();) {
        size_t best = 0;
        for (auto& p : pats) if (!p.empty() && p.size() > best && matchAt(i, p)) best = p.size();
        if (best) { out += o + s.substr(i, best) + c; i += best; }
        else { size_t st = i; decodeCp(s, i); out += s.substr(st, i - st); }
    }
    return out;
}
static std::string showWs(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == ' ') o += "\xC2\xB7"; else if (c == '\t') o += "\xE2\x86\x92"; else if (c == '\r') o += "\xE2\x90\x8D"; else o += c; }
    return o;
}
static std::string markText(const std::string& kind, int idx, int maxIdx) {
    static const std::pair<const char*, const char*> k[] = {
        {"bullet", "\xE2\x80\xA2"}, {"dash", "-"}, {"arrow", "\xE2\x86\x92"}, {"check", "\xE2\x9C\x93"}, {"cross", "\xE2\x9C\x97"},
        {"star", "\xE2\x98\x85"}, {"dot", "\xC2\xB7"}, {"plus", "+"}, {"circle", "\xE2\x97\x8B"}, {"square", "\xE2\x96\xA0"}, {"warn", "\xE2\x9A\xA0"}};
    for (auto& e : k) if (kind == e.first) return e.second;
    if (kind == "num") { std::string n = std::to_string(idx + 1); int w = static_cast<int>(std::to_string(maxIdx).size()); return sp(w - static_cast<int>(n.size())) + n + "."; }
    if (kind == "alpha") { std::string r; int n = idx; do { r = char('a' + n % 26) + r; n = n / 26 - 1; } while (n >= 0); return r + "."; }
    if (kind == "none") return "";
    return kind;
}

// شجرة لقاموس/مصفوفة متداخلة
static void treeRec(const Value& v, const std::string& pre, bool ascii, const std::function<std::string(const Value&)>& one, std::vector<std::string>& out, int depth) {
    std::vector<std::pair<std::string, const Value*>> items;
    if (v.type == Value::Type::MAP) for (auto& kv : *v.map) items.push_back({kv.first.toDisplayString(), &kv.second});
    else if (v.type == Value::Type::ARRAY) { int i = 0; for (auto& e : *v.array) items.push_back({"[" + std::to_string(i++) + "]", &e}); }
    const char* mid = ascii ? "+-- " : "├─ "; const char* last = ascii ? "`-- " : "└─ ";
    const char* bar = ascii ? "|   " : "│  "; const char* gap = "   ";
    for (size_t i = 0; i < items.size(); i++) {
        bool isLast = i + 1 == items.size(); const Value& c = *items[i].second;
        bool cont = (c.type == Value::Type::MAP && !c.map->empty()) || (c.type == Value::Type::ARRAY && !c.array->empty());
        if (cont && depth < 64) { out.push_back(pre + (isLast ? last : mid) + items[i].first); treeRec(c, pre + (isLast ? gap : bar), ascii, one, out, depth + 1); }
        else out.push_back(pre + (isLast ? last : mid) + items[i].first + ": " + one(c));
    }
}
// فرق الأسطر بين نصين (LCS): "- " محذوف · "+ " مضاف · "  " ثابت
static std::vector<std::string> diffLines(const std::string& a, const std::string& b, int line, const std::string& who) {
    auto A = splitLines(a), B = splitLines(b);
    if (A.size() > 2000 || B.size() > 2000) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'diff' يدعم حتى 2000 سطر لكل نص");
    size_t n = A.size(), m = B.size();
    std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = n; i-- > 0;) for (size_t j = m; j-- > 0;) L[i][j] = A[i] == B[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    std::vector<std::string> out; size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (A[i] == B[j]) { out.push_back("  " + A[i]); i++; j++; }
        else if (L[i + 1][j] >= L[i][j + 1]) out.push_back("- " + A[i++]);
        else out.push_back("+ " + B[j++]);
    }
    while (i < n) out.push_back("- " + A[i++]);
    while (j < m) out.push_back("+ " + B[j++]);
    return out;
}

// ------------------------------------------------------------ config
struct Cfg {
    bool sepSet = false; std::string sep = " "; bool pretty = false;
    std::string indent; int gap = -1, pad = -1, before = 0, after = 0;
    bool visible = false, squeeze = false, trim = false;
    std::string fmt, caseMode, mark; int wrap = 0, cut = 0;
    std::vector<std::string> hl; std::string hlOpen = "\xC2\xAB", hlClose = "\xC2\xBB";
    bool box = false; std::string boxStyle = "single", title; bool hasTitle = false;
    int cols = 0, colgap = 2, page = 0; std::string place;
    std::string leader; bool justify = false; std::string inalign, rule; int numbers = -1;
    bool beside = false, table = false, shadow = false; std::string cutat = "end";
    int tabs = 0, maxlines = 0, bars = 0; bool dedent = false, ends = false, ruler = false, hlCS = true, diff = false;
    std::string focus, tree;
};

static int intArg(const Value& v, const std::string& who, const char* name, int line, int lo, int hi) {
    if (v.type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, who + ": '" + name + "' يجب أن يكون رقماً، لكن وُجد نوع " + v.typeName());
    double d = std::floor(v.number);
    if (!(d >= lo && d <= hi)) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": '" + std::string(name) + "' خارج المدى المسموح (" + std::to_string(lo) + ".." + std::to_string(hi) + ")");
    return static_cast<int>(d);
}
static std::string strArg(const Value& v, const std::string& who, const char* name, int line) {
    if (v.type != Value::Type::STRING) throw diagErr(diag::Code::E0004_InvalidType, line, who + ": '" + name + "' يجب أن يكون نصاً، لكن وُجد نوع " + v.typeName());
    return v.str;
}

static Cfg buildCfg(const Value& sep, const Value& pretty, const Value* a /*40*/, const std::string& who, int line) {
    Cfg c;
    if (sep.type != Value::Type::NIL) { c.sep = strArg(sep, who, "sep", line); c.sepSet = true; }
    c.pretty = pretty.isTruthy();
    auto has = [&](int i) { return a[i].type != Value::Type::NIL; };
    if (has(0)) c.indent = a[0].type == Value::Type::STRING ? a[0].str : sp(intArg(a[0], who, "indent", line, 0, 1000));
    if (has(1)) c.gap = intArg(a[1], who, "gap", line, 0, 1000);
    if (has(2)) c.pad = intArg(a[2], who, "pad", line, 0, 1000);
    if (has(3)) c.before = intArg(a[3], who, "before", line, 0, 1000);
    if (has(4)) c.after = intArg(a[4], who, "after", line, 0, 1000);
    c.visible = a[5].isTruthy(); c.squeeze = a[6].isTruthy(); c.trim = a[7].isTruthy();
    if (has(8)) c.fmt = strArg(a[8], who, "fmt", line);
    if (has(9)) {
        c.caseMode = strArg(a[9], who, "case", line);
        static const char* ok[] = {"upper", "lower", "title", "sentence", "swap", "snake", "kebab", "camel"};
        bool f = false; for (auto o : ok) if (c.caseMode == o) f = true;
        if (!f) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'case' غير معروف \"" + c.caseMode + "\" (المتاح: upper, lower, title, sentence, swap, snake, kebab, camel)");
    }
    if (has(10)) c.wrap = intArg(a[10], who, "wrap", line, 1, 10000);
    if (has(11)) c.cut = intArg(a[11], who, "cut", line, 1, 10000);
    if (has(12)) c.mark = (a[12].type == Value::Type::BOOL) ? (a[12].boolean ? "bullet" : "") : strArg(a[12], who, "mark", line);
    if (has(13)) {
        if (a[13].type == Value::Type::STRING) c.hl.push_back(a[13].str);
        else if (a[13].type == Value::Type::ARRAY) { for (auto& e : *a[13].array) c.hl.push_back(strArg(e, who, "hl", line)); }
        else throw diagErr(diag::Code::E0004_InvalidType, line, who + ": 'hl' يجب أن يكون نصاً أو مصفوفة نصوص، لكن وُجد نوع " + a[13].typeName());
    }
    if (has(14)) {
        if (a[14].type == Value::Type::ARRAY && a[14].array->size() == 2) { c.hlOpen = strArg((*a[14].array)[0], who, "hlwrap", line); c.hlClose = strArg((*a[14].array)[1], who, "hlwrap", line); }
        else {
            std::string w = strArg(a[14], who, "hlwrap", line); auto cps = splitCp(w);
            if (cps.size() == 2 && std::string("([{<\xC2\xAB\xE2\x80\xB9").find(cps[0]) != std::string::npos) { c.hlOpen = cps[0]; c.hlClose = cps[1]; }
            else { c.hlOpen = w; c.hlClose = w; }
        }
    }
    if (has(15)) {
        if (a[15].type == Value::Type::BOOL) c.box = a[15].boolean;
        else {
            c.boxStyle = strArg(a[15], who, "box", line); c.box = c.boxStyle != "none";
            static const char* ok[] = {"single", "double", "round", "bold", "ascii", "none"};
            bool f = false; for (auto o : ok) if (c.boxStyle == o) f = true;
            if (!f) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'box' غير معروف \"" + c.boxStyle + "\" (المتاح: single, double, round, bold, ascii)");
        }
    }
    if (has(16)) { c.title = strArg(a[16], who, "title", line); c.hasTitle = true; if (!c.box) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'title' يتطلّب 'box' معه"); }
    if (has(17)) c.cols = intArg(a[17], who, "cols", line, 1, 1000);
    if (has(18)) c.colgap = intArg(a[18], who, "colgap", line, 0, 100);
    if (has(19)) c.page = intArg(a[19], who, "page", line, 1, 10000);
    if (has(20)) {
        c.place = strArg(a[20], who, "place", line);
        if (c.place != "left" && c.place != "center" && c.place != "right") throw diagErr(diag::Code::E0004_InvalidType, line, who + ": 'place' يجب أن يكون \"left\" أو \"center\" أو \"right\"، لكن وُجد \"" + c.place + "\"");
        if (!c.page) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'place' يتطلّب تحديد 'page' معه");
    }
    if (has(21)) { c.leader = strArg(a[21], who, "leader", line); if (!c.page) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'leader' يتطلّب تحديد 'page' معه"); }
    c.justify = a[22].isTruthy();
    if (c.justify && !c.wrap) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'justify' يتطلّب 'wrap' معه");
    if (has(23)) {
        c.inalign = strArg(a[23], who, "inalign", line);
        if (c.inalign != "left" && c.inalign != "center" && c.inalign != "right") throw diagErr(diag::Code::E0004_InvalidType, line, who + ": 'inalign' يجب أن يكون \"left\" أو \"center\" أو \"right\"، لكن وُجد \"" + c.inalign + "\"");
    }
    if (has(24)) c.rule = a[24].type == Value::Type::BOOL ? (a[24].boolean ? "\xE2\x94\x80" : "") : strArg(a[24], who, "rule", line);
    if (has(25)) c.numbers = a[25].type == Value::Type::BOOL ? (a[25].boolean ? 1 : -1) : intArg(a[25], who, "numbers", line, 0, 1000000);
    c.beside = a[26].isTruthy();
    if (has(27)) {
        if (a[27].type == Value::Type::BOOL) c.table = a[27].boolean;
        else { c.boxStyle = strArg(a[27], who, "table", line); c.table = true; }
        if (c.boxStyle != "single" && c.boxStyle != "double" && c.boxStyle != "round" && c.boxStyle != "bold" && c.boxStyle != "ascii") throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'table' غير معروف \"" + c.boxStyle + "\" (المتاح: single, double, round, bold, ascii)");
    }
    c.shadow = a[28].isTruthy();
    if (has(29)) {
        c.cutat = strArg(a[29], who, "cutat", line);
        if (c.cutat != "end" && c.cutat != "start" && c.cutat != "middle") throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'cutat' غير معروف \"" + c.cutat + "\" (المتاح: end, start, middle)");
        if (!c.cut) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'cutat' يتطلّب 'cut' معه");
    }
    if (has(30)) c.tabs = intArg(a[30], who, "tabs", line, 1, 64);
    c.dedent = a[31].isTruthy();
    if (has(32)) c.maxlines = intArg(a[32], who, "maxlines", line, 1, 100000);
    c.ends = a[33].isTruthy(); c.ruler = a[34].isTruthy();
    if (has(35)) c.hlCS = a[35].isTruthy();
    if (has(36)) c.focus = strArg(a[36], who, "focus", line);
    if (has(37)) {
        if (a[37].type == Value::Type::BOOL) c.tree = a[37].boolean ? "unicode" : "";
        else { c.tree = strArg(a[37], who, "tree", line); if (c.tree != "unicode" && c.tree != "ascii") throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": 'tree' غير معروف \"" + c.tree + "\" (المتاح: true, \"unicode\", \"ascii\")"); }
    }
    if (has(38)) c.bars = a[38].type == Value::Type::BOOL ? (a[38].boolean ? 20 : 0) : intArg(a[38], who, "bars", line, 1, 200);
    c.diff = a[39].isTruthy();
    if (c.gap >= 0 && c.sepSet) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": لا يمكن استخدام 'gap' و'sep' معاً");
    return c;
}

// ------------------------------------------------------------ engine
static std::string render(const std::vector<Value>& vals, const Cfg& c, int line) {
    const std::string who = "'print'";
    auto one = [&](const Value& v, bool allowPretty) -> std::string {
        if (v.type == Value::Type::NUMBER && !c.fmt.empty()) return fmtNum(v.number, c.fmt, line, who);
        if (allowPretty && c.pretty && (v.type == Value::Type::ARRAY || v.type == Value::Type::MAP)) return prettyPrintValue(v, 0);
        return v.toDisplayString();
    };
    std::string text;
    std::vector<std::string> tbl; bool isTable = false;
    if (c.table) {   // جدول: مصفوفة صفوف (الأول عنوان) أو مصفوفة قواميس (المفاتيح عنوان)
        isTable = true;
        std::vector<Value> rowsSrc = (vals.size() == 1 && vals[0].type == Value::Type::ARRAY) ? *vals[0].array : vals;
        std::vector<std::vector<std::string>> rows; std::vector<std::vector<bool>> isn;
        if (!rowsSrc.empty() && rowsSrc[0].type == Value::Type::MAP) {
            std::vector<std::string> keys;
            for (auto& r : rowsSrc) if (r.type == Value::Type::MAP) for (auto& kv : *r.map) { std::string k = kv.first.toDisplayString(); if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k); }
            rows.push_back(keys); isn.push_back(std::vector<bool>(keys.size(), false));
            for (auto& r : rowsSrc) {
                std::vector<std::string> row; std::vector<bool> nm;
                for (auto& k : keys) { const Value* f = r.type == Value::Type::MAP ? r.map->findValue(Value::string(k)) : nullptr; row.push_back(f ? one(*f, false) : ""); nm.push_back(f && f->type == Value::Type::NUMBER); }
                rows.push_back(row); isn.push_back(nm);
            }
        } else {
            for (auto& r : rowsSrc) {
                std::vector<std::string> row; std::vector<bool> nm;
                if (r.type == Value::Type::ARRAY) for (auto& e : *r.array) { row.push_back(one(e, false)); nm.push_back(e.type == Value::Type::NUMBER); }
                else { row.push_back(one(r, false)); nm.push_back(r.type == Value::Type::NUMBER); }
                rows.push_back(row); isn.push_back(nm);
            }
        }
        size_t nc = 0; for (auto& r : rows) nc = std::max(nc, r.size());
        for (auto& r : rows) for (auto& x : r) std::replace(x.begin(), x.end(), '\n', ' ');
        std::vector<int> cw(nc, 0); std::vector<bool> numCol(nc, true);
        for (size_t ri = 0; ri < rows.size(); ri++) for (size_t k = 0; k < nc; k++) {
            std::string cell = k < rows[ri].size() ? rows[ri][k] : "";
            cw[k] = std::max(cw[k], dispWidth(cell));
            if (ri > 0 && !(k < isn[ri].size() && isn[ri][k])) numCol[k] = false;
        }
        static const char* G[5][11] = {
            {"┌", "─", "┐", "│", "└", "┘", "┬", "┴", "├", "┤", "┼"},
            {"╔", "═", "╗", "║", "╚", "╝", "╦", "╩", "╠", "╣", "╬"},
            {"╭", "─", "╮", "│", "╰", "╯", "┬", "┴", "├", "┤", "┼"},
            {"┏", "━", "┓", "┃", "┗", "┛", "┳", "┻", "┣", "┫", "╋"},
            {"+", "-", "+", "|", "+", "+", "+", "+", "+", "+", "+"}};
        int gi = c.boxStyle == "double" ? 1 : c.boxStyle == "round" ? 2 : c.boxStyle == "bold" ? 3 : c.boxStyle == "ascii" ? 4 : 0;
        auto hline = [&](int l, int m, int r) { std::string o = G[gi][l]; for (size_t k = 0; k < nc; k++) { o += rep(G[gi][1], cw[k] + 2); o += (k + 1 < nc) ? G[gi][m] : G[gi][r]; } return o; };
        if (!rows.empty() && nc) {
            tbl.push_back(hline(0, 6, 2));
            for (size_t ri = 0; ri < rows.size(); ri++) {
                std::string o = G[gi][3];
                for (size_t k = 0; k < nc; k++) {
                    std::string cell = k < rows[ri].size() ? rows[ri][k] : ""; int pn = cw[k] - dispWidth(cell);
                    bool right = numCol[k] && ri > 0;
                    o += " " + (right ? sp(pn) + cell : cell + sp(pn)) + " " + G[gi][3];
                }
                tbl.push_back(o);
                if (ri == 0 && rows.size() > 1) tbl.push_back(hline(8, 10, 9));
            }
            tbl.push_back(hline(4, 7, 5));
        }
    } else if (!c.tree.empty()) {   // شجرة لقاموس/مصفوفة متداخلة
        std::function<std::string(const Value&)> f1 = [&](const Value& v) { return one(v, false); };
        std::vector<std::string> out;
        for (auto& v : vals) {
            if (v.type == Value::Type::MAP || v.type == Value::Type::ARRAY) treeRec(v, "", c.tree == "ascii", f1, out, 0);
            else out.push_back(one(v, false));
        }
        for (size_t i = 0; i < out.size(); i++) text += (i ? "\n" : "") + out[i];
    } else if (c.bars > 0) {   // مخطط أعمدة أفقي: قاموس (اسم→رقم) أو مصفوفة أرقام أو أزواج [اسم، رقم]
        std::vector<std::pair<std::string, double>> it; std::vector<std::string> shown;
        auto addPair = [&](const std::string& lbl, const Value& v) {
            if (v.type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, who + ": 'bars' يتطلّب أرقاماً، لكن وُجد نوع " + v.typeName());
            it.push_back({lbl, v.number}); shown.push_back(one(v, false));
        };
        for (auto& v : vals) {
            if (v.type == Value::Type::MAP) for (auto& kv : *v.map) addPair(kv.first.toDisplayString(), kv.second);
            else if (v.type == Value::Type::ARRAY) { int i = 0; for (auto& e : *v.array) { if (e.type == Value::Type::ARRAY && e.array->size() == 2) addPair((*e.array)[0].toDisplayString(), (*e.array)[1]); else addPair(std::to_string(++i), e); } }
            else addPair(std::to_string(it.size() + 1), v);
        }
        double mx = 0; for (auto& p : it) mx = std::max(mx, p.second);
        int lw = 0, vw = 0; for (size_t i = 0; i < it.size(); i++) { lw = std::max(lw, dispWidth(it[i].first)); vw = std::max(vw, dispWidth(shown[i])); }
        for (size_t i = 0; i < it.size(); i++) {
            int n = mx > 0 && it[i].second > 0 ? std::max(1, static_cast<int>(std::lround(it[i].second / mx * c.bars))) : 0;
            text += (i ? "\n" : "") + it[i].first + sp(lw - dispWidth(it[i].first)) + " " + rep("█", n) + rep("░", c.bars - n) + " " + sp(vw - dispWidth(shown[i])) + shown[i];
        }
    } else if (c.diff) {
        if (vals.size() != 2) throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": 'diff' يتطلّب قيمتين بالضبط (قبل، بعد)");
        auto d = diffLines(one(vals[0], false), one(vals[1], false), line, who);
        for (size_t i = 0; i < d.size(); i++) text += (i ? "\n" : "") + d[i];
    } else if (!c.leader.empty()) {   // نقاط قيادة: (اسم، قيمة) أزواجاً حتى عرض page
        std::vector<std::string> items; for (auto& v : vals) items.push_back(one(v, false));
        auto lc = splitCp(c.leader);
        for (size_t i = 0; i < items.size(); i += 2) {
            std::string l = items[i], r = i + 1 < items.size() ? items[i + 1] : "";
            int room = c.page - dispWidth(l) - dispWidth(r); std::string ln;
            if (r.empty()) ln = l;
            else if (room >= 4) { std::string f; int fillN = room - 2; for (int k = 0; k < fillN; k++) f += lc.empty() ? " " : lc[static_cast<size_t>(k) % lc.size()]; ln = l + " " + f + " " + r; }
            else ln = l + " " + r;
            text += (i ? "\n" : "") + ln;
        }
    } else if (c.beside) {   // كتل جنباً إلى جنب
        std::vector<std::vector<std::string>> blks; std::vector<int> bw; size_t H = 0;
        for (auto& v : vals) { auto ls = splitLines(one(v, true)); int w = 0; for (auto& l : ls) w = std::max(w, dispWidth(l)); blks.push_back(ls); bw.push_back(w); H = std::max(H, ls.size()); }
        int g = c.gap >= 0 ? c.gap : c.colgap;
        for (size_t r = 0; r < H; r++) {
            std::string ln;
            for (size_t k = 0; k < blks.size(); k++) { std::string cell = r < blks[k].size() ? blks[k][r] : ""; ln += (k ? sp(g) : "") + cell + sp(bw[k] - dispWidth(cell)); }
            size_t e = ln.find_last_not_of(' '); ln = e == std::string::npos ? "" : ln.substr(0, e + 1);
            text += (r ? "\n" : "") + ln;
        }
    } else if (c.cols > 0) {
        std::vector<std::pair<std::string, bool>> cells;
        for (auto& v : vals) {
            if (v.type == Value::Type::ARRAY) for (auto& e : *v.array) cells.push_back({one(e, false), e.type == Value::Type::NUMBER});
            else cells.push_back({one(v, false), v.type == Value::Type::NUMBER});
        }
        for (auto& cl : cells) std::replace(cl.first.begin(), cl.first.end(), '\n', ' ');
        size_t n = static_cast<size_t>(c.cols), rows = (cells.size() + n - 1) / n;
        std::vector<int> cw(n, 0); std::vector<bool> numCol(n, true);
        for (size_t i = 0; i < cells.size(); i++) { cw[i % n] = std::max(cw[i % n], dispWidth(cells[i].first)); if (!cells[i].second) numCol[i % n] = false; }
        for (size_t r = 0; r < rows; r++) {
            std::string ln;
            for (size_t k = 0; k < n && r * n + k < cells.size(); k++) {
                auto& cl = cells[r * n + k]; int padn = cw[k] - dispWidth(cl.first);
                std::string cell = numCol[k] ? sp(padn) + cl.first : cl.first + sp(padn);
                ln += (k ? sp(c.colgap) : "") + cell;
            }
            size_t e = ln.find_last_not_of(' '); ln = e == std::string::npos ? "" : ln.substr(0, e + 1);
            text += (r ? "\n" : "") + ln;
        }
    } else {
        std::string sep = c.gap >= 0 ? sp(c.gap) : c.sep;
        for (size_t i = 0; i < vals.size(); i++) text += (i ? sep : "") + one(vals[i], true);
    }
    std::vector<std::string> block;
    if (isTable) block = tbl;
    else {
    // أسطر منطقية
    auto lines = splitLines(text);
    if (c.tabs > 0) for (auto& l : lines) {
        std::string o; int col = 0;
        for (auto& cp : splitCp(l)) { if (cp == "\t") { int n = c.tabs - col % c.tabs; o += sp(n); col += n; } else { o += cp; size_t k = 0; col += cpWidth(decodeCp(cp, k)); } }
        l = o;
    }
    if (c.dedent) {
        size_t common = std::string::npos;
        for (auto& l : lines) { if (l.find_first_not_of(' ') == std::string::npos) continue; common = std::min(common, l.find_first_not_of(' ')); }
        if (common != std::string::npos && common > 0) for (auto& l : lines) l = l.size() >= common && l.find_first_not_of(' ') >= common ? l.substr(common) : (l.find_first_not_of(' ') == std::string::npos ? "" : l);
    }
    for (auto& l : lines) {
        if (c.trim) l = trimLine(l);
        if (c.squeeze) l = squeezeLine(l);
        if (!c.caseMode.empty()) l = caseLine(l, c.caseMode);
    }
    // تعليم (بادئات بنفس العرض + إزاحة معلّقة للأسطر الملتفّة)
    std::vector<std::string> prefixes(lines.size());
    int P = 0;
    if ((!c.mark.empty() && c.mark != "none") || !c.focus.empty()) {
        int total = 0; for (auto& l : lines) if (!l.empty()) total++;
        int idx = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            if (lines[i].empty()) continue;
            std::string m = (!c.mark.empty() && c.mark != "none") ? markText(c.mark, idx, total) : "";
            idx++;
            if (!m.empty() && m.back() != ' ') m += " ";
            if (!c.focus.empty()) m = (lines[i].find(c.focus) != std::string::npos ? "▶ " : "  ") + m;
            prefixes[i] = m; P = std::max(P, dispWidth(m));
        }
        for (auto& p : prefixes) if (!p.empty()) p += sp(P - dispWidth(p));
    }
    for (size_t i = 0; i < lines.size(); i++) {
        auto subs = wrapLine(lines[i], c.wrap);
        for (size_t k = 0; k < subs.size(); k++) {
            std::string s = subs[k];
            if (c.justify && k + 1 < subs.size()) s = justifyLine(s, c.wrap);
            if (c.cut > 0) s = cutLine(s, c.cut, c.cutat);
            if (!c.hl.empty()) s = hlLine(s, c.hl, c.hlOpen, c.hlClose, c.hlCS);
            if (c.visible) s = showWs(s);
            if (c.ends) s += "¶";
            std::string pre = k == 0 ? (prefixes[i].empty() && !lines[i].empty() ? sp(P) : prefixes[i]) : (lines[i].empty() ? "" : sp(P));
            block.push_back(pre + s);
        }
    }
    if (c.maxlines > 0 && static_cast<int>(block.size()) > c.maxlines) {
        int extra = static_cast<int>(block.size()) - c.maxlines;
        block.resize(static_cast<size_t>(c.maxlines)); block.push_back("… (+" + std::to_string(extra) + " more)");
    }
    if (!c.inalign.empty() && c.inalign != "left") {
        int mw = 0; for (auto& l : block) mw = std::max(mw, dispWidth(l));
        for (auto& l : block) { int d = mw - dispWidth(l); l = sp(c.inalign == "center" ? d / 2 : d) + l; }
    }
    if (c.numbers >= 0) {   // عمود أرقام الأسطر
        int nw = static_cast<int>(std::to_string(c.numbers + static_cast<int>(block.size())).size());
        for (size_t i = 0; i < block.size(); i++) { std::string n = std::to_string(c.numbers + static_cast<int>(i)); block[i] = sp(nw - static_cast<int>(n.size())) + n + " │ " + block[i]; }
    }
    }
    int pad = c.pad >= 0 ? c.pad : (c.box ? 1 : 0);
    if (pad > 0) for (auto& l : block) l = sp(pad) + l + sp(pad);
    if (c.box) {
        static const char* S[6][6] = {
            {"\xE2\x94\x8C", "\xE2\x94\x80", "\xE2\x94\x90", "\xE2\x94\x82", "\xE2\x94\x94", "\xE2\x94\x98"},
            {"\xE2\x95\x94", "\xE2\x95\x90", "\xE2\x95\x97", "\xE2\x95\x91", "\xE2\x95\x9A", "\xE2\x95\x9D"},
            {"\xE2\x95\xAD", "\xE2\x94\x80", "\xE2\x95\xAE", "\xE2\x94\x82", "\xE2\x95\xB0", "\xE2\x95\xAF"},
            {"\xE2\x94\x8F", "\xE2\x94\x81", "\xE2\x94\x93", "\xE2\x94\x83", "\xE2\x94\x97", "\xE2\x94\x9B"},
            {"+", "-", "+", "|", "+", "+"}, {"+", "-", "+", "|", "+", "+"}};
        int si = c.boxStyle == "double" ? 1 : c.boxStyle == "round" ? 2 : c.boxStyle == "bold" ? 3 : c.boxStyle == "ascii" ? 4 : 0;
        int inner = 0; for (auto& l : block) inner = std::max(inner, dispWidth(l));
        int tw = c.hasTitle ? dispWidth(c.title) : 0;
        if (c.hasTitle) inner = std::max(inner, tw + 3);
        std::string h = S[si][1];
        std::vector<std::string> out;
        out.push_back(std::string(S[si][0]) + (c.hasTitle ? h + " " + c.title + " " + rep(h, inner - tw - 3) : rep(h, inner)) + S[si][2]);
        for (auto& l : block) out.push_back(std::string(S[si][3]) + l + sp(inner - dispWidth(l)) + S[si][3]);
        out.push_back(std::string(S[si][4]) + rep(h, inner) + S[si][5]);
        block = out;
    }
    if (c.shadow && !block.empty()) {
        int bw = 0; for (auto& l : block) bw = std::max(bw, dispWidth(l));
        for (size_t i = 1; i < block.size(); i++) block[i] += sp(bw - dispWidth(block[i])) + "░";
        block.push_back(" " + rep("░", bw));
    }
    if (!c.rule.empty() && !block.empty()) {
        int bw = 0; for (auto& l : block) bw = std::max(bw, dispWidth(l));
        auto rc = splitCp(c.rule); std::string r; for (int i = 0; i < bw; i++) r += rc[static_cast<size_t>(i) % rc.size()];
        block.insert(block.begin(), r); block.push_back(r);
    }
    if (c.ruler && !block.empty()) {
        int bw = 0; for (auto& l : block) bw = std::max(bw, dispWidth(l));
        std::string ones, tens;
        for (int i = 1; i <= bw; i++) { ones += char('0' + i % 10); tens += (i % 10 == 0) ? char('0' + (i / 10) % 10) : ' '; }
        if (bw >= 10) block.insert(block.begin(), tens);
        block.insert(block.begin() + (bw >= 10 ? 1 : 0), ones);
    }
    if (c.page > 0 && !c.place.empty()) {
        int bw = 0; for (auto& l : block) bw = std::max(bw, dispWidth(l));
        int off = c.place == "center" ? (c.page - bw) / 2 : c.place == "right" ? c.page - bw : 0;
        if (off > 0) for (auto& l : block) if (!l.empty()) l = sp(off) + l;
    }
    std::string res = rep("\n", c.before);
    for (size_t i = 0; i < block.size(); i++) res += (i ? "\n" : "") + ((c.indent.empty() || block[i].empty()) ? "" : c.indent) + block[i];
    return res + rep("\n", c.after);
}

} // namespace printlayout

void Interpreter::registerNativesPrintLayout() {
    using namespace printlayout;
    // مُولَّد من المحلل: __pfx(sep, pretty, <40 سمة بترتيب kAttrs>, قيم...)
    natives["__pfx"] = [](std::vector<Value>& a, int line) -> Value {
        if (a.size() < 42) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'__pfx' استدعاء داخلي غير صالح");
        Cfg c = buildCfg(a[0], a[1], a.data() + 2, "'print'", line);
        std::vector<Value> vals(a.begin() + 42, a.end());
        return Value::string(render(vals, c, line));
    };
    // layoutText(value, {indent, gap, pad, before, after, visible, squeeze, trim, fmt, case, wrap, cut, mark, hl, hlwrap, box, title, cols, colgap, page, place, sep, pretty})
    natives["layoutText"] = [](std::vector<Value>& a, int line) -> Value {
        if (a.empty() || a.size() > 2) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'layoutText' تتوقع (قيمة، خيارات؟)");
        Value slots[40]; Value sep, pretty;
        if (a.size() == 2) {
            if (a[1].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'layoutText': الخيارات يجب أن تكون قاموساً، لكن وُجد نوع " + a[1].typeName());
            for (auto& kv : *a[1].map) {
                if (kv.first.type != Value::Type::STRING) continue;
                const std::string& k = kv.first.str; bool found = false;
                if (k == "sep") { sep = kv.second; found = true; } else if (k == "pretty") { pretty = kv.second; found = true; }
                for (int i = 0; i < 40 && !found; i++) if (k == kAttrs[i]) { slots[i] = kv.second; found = true; }
                if (!found) throw diagErr(diag::Code::E0016_InvalidProperty, line, "'layoutText': خيار غير معروف \"" + k + "\"");
            }
        }
        Cfg c = buildCfg(sep, pretty, slots, "'layoutText'", line);
        return Value::string(render({a[0]}, c, line));
    };
    natives["fmtNumber"] = [](std::vector<Value>& a, int line) -> Value {
        if (a.size() != 2) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'fmtNumber' تتوقع (رقم، صيغة)");
        if (a[0].type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, "'fmtNumber': الأول يجب أن يكون رقماً، لكن وُجد نوع " + a[0].typeName());
        return Value::string(fmtNum(a[0].number, strArg(a[1], "'fmtNumber'", "spec", line), line, "'fmtNumber'"));
    };
    natives["showSpaces"] = [](std::vector<Value>& a, int line) -> Value {
        if (a.size() != 1) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'showSpaces' تتوقع نصاً واحداً");
        return Value::string(showWs(a[0].toDisplayString()));
    };
}

} // namespace rin
