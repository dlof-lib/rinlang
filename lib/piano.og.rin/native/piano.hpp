// piano.hpp — مُصيِّر C++17 لمشاهد piano (.puo / JSON) → SVG. ملف واحد، بلا تبعيات.
//
// هذا الملف نسخة حرفية من core/render.zpp (المواصفة المرجعية): ترتيب السمات وتنسيق الأرقام
// والمسافات متطابقة، وتفحص tools/piano_check.py أن مخرجاته تساوي مخرجات Rin بايتاً ببايت.
//
//   #include "piano.hpp"
//   std::string svg = piano::render_svg(piano::parse(json_text));
//
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace piano {

// ---------------------------------------------------------------------------
//  قيمة JSON (الحقول تحفظ ترتيب الإدراج)
// ---------------------------------------------------------------------------
struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    bool isNum() const { return t == Num; }
    bool isStr() const { return t == Str; }
    bool isArr() const { return t == Arr; }
    bool isObj() const { return t == Obj; }
    const J* find(const std::string& k) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    // مطابق لـ pn_has: موجود وغير null
    bool has(const std::string& k) const {
        const J* v = find(k);
        return v && v->t != Null;
    }
    const J& at(const std::string& k) const {
        const J* v = find(k);
        if (!v) throw std::runtime_error("piano: حقل مفقود: " + k);
        return *v;
    }
};

// ---------------------------------------------------------------------------
//  محلّل JSON
// ---------------------------------------------------------------------------
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    J parse() {
        J v = value();
        ws();
        if (i_ != s_.size()) fail("نص زائد بعد نهاية JSON");
        return v;
    }
private:
    const std::string& s_;
    size_t i_ = 0;
    [[noreturn]] void fail(const std::string& m) {
        throw std::runtime_error("piano: JSON غير صالح عند الموضع " + std::to_string(i_) + ": " + m);
    }
    void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\t' || s_[i_] == '\r')) ++i_; }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (s_.compare(i_, n, w) == 0) { i_ += n; return true; }
        return false;
    }
    J value() {
        ws();
        if (i_ >= s_.size()) fail("نهاية مفاجئة");
        char c = s_[i_];
        J v;
        if (c == '{') {
            ++i_; v.t = J::Obj; ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return v; }
            for (;;) {
                ws();
                if (i_ >= s_.size() || s_[i_] != '"') fail("مفتاح متوقَّع");
                std::string k = str();
                ws();
                if (i_ >= s_.size() || s_[i_] != ':') fail("«:» متوقَّعة");
                ++i_;
                v.o.emplace_back(k, value());
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
                fail("«,» أو «}» متوقَّعة");
            }
            return v;
        }
        if (c == '[') {
            ++i_; v.t = J::Arr; ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return v; }
            for (;;) {
                v.a.push_back(value());
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
                fail("«,» أو «]» متوقَّعة");
            }
            return v;
        }
        if (c == '"') { v.t = J::Str; v.s = str(); return v; }
        if (lit("true")) { v.t = J::Bool; v.b = true; return v; }
        if (lit("false")) { v.t = J::Bool; v.b = false; return v; }
        if (lit("null")) { return v; }
        char* end = nullptr;
        double d = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) fail("قيمة غير معروفة");
        i_ = static_cast<size_t>(end - s_.c_str());
        v.t = J::Num; v.n = d;
        return v;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("\\u ناقصة");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= h - '0';
            else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
            else fail("رقم سداسي غير صالح");
        }
        return v;
    }
    std::string str() {
        ++i_;  // "
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) fail("هروب ناقص");
            char e = s_[i_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: fail("هروب غير معروف");
            }
        }
        if (i_ >= s_.size()) fail("نص غير منتهٍ");
        ++i_;  // "
        return out;
    }
};

inline J parse(const std::string& text) { return Parser(text).parse(); }

// ---------------------------------------------------------------------------
//  أدوات التنسيق — مطابقة لـ pn_nf / pn_esc / pn_norm_color
// ---------------------------------------------------------------------------
inline std::string nf(double v, int d) {
    if (v != v) return "0";
    if (v > 1000000000) v = 1000000000;
    if (v < -1000000000) v = -1000000000;
    bool neg = v < 0;
    double n = std::floor(std::fabs(v) * std::pow(10.0, d) + 0.5);
    if (n == 0) return "0";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f", n);
    std::string s = buf;
    if (d > 0) {
        while (static_cast<int>(s.size()) <= d) s = "0" + s;
        std::string ip = s.substr(0, s.size() - d);
        std::string fp = s.substr(s.size() - d);
        while (!fp.empty() && fp.back() == '0') fp.pop_back();
        s = fp.empty() ? ip : ip + "." + fp;
    }
    if (neg) s = "-" + s;
    return s;
}
inline std::string n2(double v) { return nf(v, 2); }

inline std::string replace_all(std::string s, const std::string& a, const std::string& b) {
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
    return s;
}
inline std::string tostr(const J& v) {
    switch (v.t) {
        case J::Str: return v.s;
        case J::Num: return nf(v.n, 6);
        case J::Bool: return v.b ? "true" : "false";
        default: return "";
    }
}
inline std::string esc(const std::string& s) {
    std::string r = replace_all(s, "&", "&amp;");
    r = replace_all(r, "<", "&lt;");
    r = replace_all(r, ">", "&gt;");
    r = replace_all(r, "\"", "&quot;");
    return r;
}
inline int hex1(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline int hex2(const std::string& s, size_t i) { return hex1(s[i]) * 16 + hex1(s[i + 1]); }
inline std::string norm_color(const std::string& c) {
    if (c.size() == 4 && c[0] == '#') return std::string("#") + c[1] + c[1] + c[2] + c[2] + c[3] + c[3];
    return c;
}

inline std::string attr(const std::string& name, const std::string& v) { return " " + name + "=\"" + esc(v) + "\""; }
inline std::string attrn(const std::string& name, double v) { return " " + name + "=\"" + n2(v) + "\""; }
inline std::string anum(const J& n, const char* key, const char* name) {
    return n.has(key) ? attrn(name, n.at(key).n) : "";
}
inline std::string astr(const J& n, const char* key, const char* name) {
    return n.has(key) ? attr(name, tostr(n.at(key))) : "";
}
inline std::string url(const std::string& v) {
    if (!v.empty() && v[0] == '@') return "url(#" + v.substr(1) + ")";
    return v;
}

// طلاء + شفافية (من #rrggbbaa أو من الحقل الصريح opkey)
inline std::string paint_v(const J* val, const J* explicit_op, const char* attrName, const char* opAttr) {
    std::string s;
    bool haveOp = false;
    double op = 0;
    if (val && val->t != J::Null) {
        if (val->isStr()) {
            const std::string& v0 = val->s;
            if (!v0.empty() && v0[0] == '@') s = attr(attrName, url(v0));
            else {
                std::string v = norm_color(v0);
                if (v.size() == 9 && v[0] == '#') {
                    s = attr(attrName, v.substr(0, 7));
                    op = hex2(v, 7) / 255.0; haveOp = true;
                } else s = attr(attrName, v);
            }
        } else s = attr(attrName, tostr(*val));
    }
    if (explicit_op && explicit_op->t != J::Null) { op = explicit_op->n; haveOp = true; }
    if (haveOp) s += attr(opAttr, nf(op, 3));
    return s;
}
inline std::string paint(const J& n, const char* key, const char* attrName, const char* opkey, const char* opAttr) {
    return paint_v(n.has(key) ? &n.at(key) : nullptr, n.has(opkey) ? &n.at(opkey) : nullptr, attrName, opAttr);
}
inline std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) { if (i) r += sep; r += v[i]; }
    return r;
}
inline std::string tfstr(const J& tf) {
    std::vector<std::string> parts;
    for (auto& op : tf.a) {
        std::vector<std::string> args;
        for (size_t i = 1; i < op.a.size(); ++i) args.push_back(n2(op.a[i].n));
        parts.push_back(op.a[0].s + "(" + join(args, " ") + ")");
    }
    return join(parts, " ");
}
inline std::string tail(const J& n) {
    std::string s = paint(n, "fill", "fill", "fo", "fill-opacity");
    s += paint(n, "stroke", "stroke", "so", "stroke-opacity");
    s += anum(n, "sw", "stroke-width");
    if (n.has("dash")) {
        std::vector<std::string> d;
        for (auto& v : n.at("dash").a) d.push_back(n2(v.n));
        s += attr("stroke-dasharray", join(d, " "));
    }
    s += astr(n, "cap", "stroke-linecap");
    s += astr(n, "join", "stroke-linejoin");
    s += anum(n, "op", "opacity");
    if (n.has("tf")) s += attr("transform", tfstr(n.at("tf")));
    if (n.has("filter")) s += attr("filter", url(n.at("filter").s));
    if (n.has("clip")) s += attr("clip-path", url(n.at("clip").s));
    return s;
}
inline std::string head(const J& n) { return astr(n, "id", "id") + astr(n, "cls", "class"); }

// ---------------------------------------------------------------------------
//  الحركة
// ---------------------------------------------------------------------------
inline std::string av(const J& v) { return v.isNum() ? n2(v.n) : esc(tostr(v)); }
inline std::string repv(const J* v) {
    if (!v || v->t == J::Null) return "indefinite";
    if (v->isNum()) return n2(v->n);
    return v->s;
}
inline std::string anim_str(const J& a) {
    const J* k = a.find("k");
    if (k && k->isStr() && k->s == "rotate") {
        std::string cx = n2(a.has("cx") ? a.at("cx").n : 0), cy = n2(a.has("cy") ? a.at("cy").n : 0);
        return "<animateTransform attributeName=\"transform\" type=\"rotate\" from=\"" + n2(a.at("from").n) + " " + cx + " " + cy +
               "\" to=\"" + n2(a.at("to").n) + " " + cx + " " + cy + "\" dur=\"" + n2(a.at("dur").n) + "s\" repeatCount=\"" + repv(a.find("rep")) + "\"/>";
    }
    std::string s = "<animate attributeName=\"" + esc(a.at("name").s) + "\"";
    if (a.has("values")) {
        std::vector<std::string> vs;
        for (auto& v : a.at("values").a) vs.push_back(av(v));
        s += " values=\"" + join(vs, ";") + "\"";
    } else {
        s += " from=\"" + av(a.at("from")) + "\" to=\"" + av(a.at("to")) + "\"";
    }
    return s + " dur=\"" + n2(a.at("dur").n) + "s\" repeatCount=\"" + repv(a.find("rep")) + "\"/>";
}

// ---------------------------------------------------------------------------
//  العقد
// ---------------------------------------------------------------------------
inline std::string points(const J& f) {
    std::vector<std::string> parts;
    for (size_t i = 0; i + 1 < f.a.size(); i += 2) parts.push_back(n2(f.a[i].n) + "," + n2(f.a[i + 1].n));
    return join(parts, " ");
}
inline std::string text_attrs(const J& n) {
    std::string s = attrn("x", n.at("x").n) + attrn("y", n.at("y").n);
    s += anum(n, "size", "font-size");
    s += astr(n, "family", "font-family");
    if (n.has("weight")) s += attr("font-weight", av(n.at("weight")));
    if (n.has("italic") && n.at("italic").t == J::Bool && n.at("italic").b) s += attr("font-style", "italic");
    s += astr(n, "anchor", "text-anchor");
    s += astr(n, "base", "dominant-baseline");
    s += anum(n, "ls", "letter-spacing");
    s += astr(n, "dir", "direction");
    return s;
}
inline std::vector<std::string> split_nl(const std::string& s) {
    std::vector<std::string> out;
    size_t st = 0, p;
    while ((p = s.find('\n', st)) != std::string::npos) { out.push_back(s.substr(st, p - st)); st = p + 1; }
    out.push_back(s.substr(st));
    return out;
}
inline std::string text_body(const J& n) {
    std::string txt = tostr(n.at("text"));
    auto ls = split_nl(txt);
    if (ls.size() < 2) return esc(txt);
    double size = n.has("size") ? n.at("size").n : 16;
    double lh = n.has("lh") ? n.at("lh").n : 1.2;
    std::string out;
    for (size_t i = 0; i < ls.size(); ++i) {
        double dy = i > 0 ? size * lh : 0;
        out += "<tspan" + attrn("x", n.at("x").n) + attrn("dy", dy) + ">" + esc(ls[i]) + "</tspan>";
    }
    return out;
}

inline void emit(std::vector<std::string>& out, const J& n, int ind) {
    std::string pad(static_cast<size_t>(ind), ' ');
    const std::string& t = n.at("t").s;
    std::string a = head(n);
    if (t == "rect") {
        a += anum(n, "x", "x") + anum(n, "y", "y") + anum(n, "w", "width") + anum(n, "h", "height") + anum(n, "rx", "rx") + anum(n, "ry", "ry");
    } else if (t == "circle") {
        a += anum(n, "cx", "cx") + anum(n, "cy", "cy") + anum(n, "r", "r");
    } else if (t == "ellipse") {
        a += anum(n, "cx", "cx") + anum(n, "cy", "cy") + anum(n, "rx", "rx") + anum(n, "ry", "ry");
    } else if (t == "line") {
        a += anum(n, "x1", "x1") + anum(n, "y1", "y1") + anum(n, "x2", "x2") + anum(n, "y2", "y2");
    } else if (t == "polyline" || t == "polygon") {
        a += attr("points", points(n.at("pts")));
    } else if (t == "path") {
        a += attr("d", n.at("d").s);
    } else if (t == "text") {
        a += text_attrs(n);
    } else if (t == "image") {
        a += anum(n, "x", "x") + anum(n, "y", "y") + anum(n, "w", "width") + anum(n, "h", "height") + attr("href", n.at("href").s) + astr(n, "par", "preserveAspectRatio");
    }
    a += tail(n);

    std::vector<std::string> kids;
    if (n.has("title")) kids.push_back("<title>" + esc(tostr(n.at("title"))) + "</title>");
    if (n.has("anim")) for (auto& an : n.at("anim").a) kids.push_back(anim_str(an));

    if (t == "text") {
        out.push_back(pad + "<text" + a + ">" + text_body(n) + join(kids, "") + "</text>");
        return;
    }
    std::vector<std::string> sub;
    if (t == "g" && n.has("nodes")) for (auto& ch : n.at("nodes").a) emit(sub, ch, ind + 2);
    if (kids.empty() && sub.empty()) { out.push_back(pad + "<" + t + a + "/>"); return; }
    out.push_back(pad + "<" + t + a + ">");
    for (auto& k : kids) out.push_back(pad + "  " + k);
    for (auto& s : sub) out.push_back(s);
    out.push_back(pad + "</" + t + ">");
}

// ---------------------------------------------------------------------------
//  التعريفات
// ---------------------------------------------------------------------------
inline void stop_lines(std::vector<std::string>& out, const J& stops, const std::string& pad) {
    for (auto& s : stops.a) {
        std::string col = norm_color(s.a[1].s);
        std::string a = attrn("offset", s.a[0].n);
        if (col.size() == 9 && col[0] == '#')
            a += attr("stop-color", col.substr(0, 7)) + attr("stop-opacity", nf(hex2(col, 7) / 255.0, 3));
        else a += attr("stop-color", col);
        out.push_back(pad + "<stop" + a + "/>");
    }
}
inline std::string flood(const std::string& c, double op) {
    std::string col = norm_color(c);
    if (col.size() == 9 && col[0] == '#') col = col.substr(0, 7);
    return "<feFlood" + attr("flood-color", col) + attr("flood-opacity", nf(op, 3)) + "/>";
}
inline void emit_def(std::vector<std::string>& out, const J& d, int ind) {
    std::string pad(static_cast<size_t>(ind), ' ');
    const std::string& t = d.at("t").s;
    std::string id = attr("id", d.at("id").s);
    const std::string region = " x=\"-50%\" y=\"-50%\" width=\"200%\" height=\"200%\"";
    if (t == "linear") {
        out.push_back(pad + "<linearGradient" + id + attrn("x1", d.at("x1").n) + attrn("y1", d.at("y1").n) + attrn("x2", d.at("x2").n) + attrn("y2", d.at("y2").n) + ">");
        stop_lines(out, d.at("stops"), pad + "  ");
        out.push_back(pad + "</linearGradient>");
    } else if (t == "radial") {
        out.push_back(pad + "<radialGradient" + id + attrn("cx", d.at("cx").n) + attrn("cy", d.at("cy").n) + attrn("r", d.at("r").n) + ">");
        stop_lines(out, d.at("stops"), pad + "  ");
        out.push_back(pad + "</radialGradient>");
    } else if (t == "shadow") {
        out.push_back(pad + "<filter" + id + region + ">");
        out.push_back(pad + "  <feGaussianBlur in=\"SourceAlpha\"" + attrn("stdDeviation", d.at("blur").n / 2) + "/>");
        out.push_back(pad + "  <feOffset" + attrn("dx", d.at("dx").n) + attrn("dy", d.at("dy").n) + " result=\"o\"/>");
        out.push_back(pad + "  " + flood(d.at("c").s, d.has("op") ? d.at("op").n : 0.3));
        out.push_back(pad + "  <feComposite in2=\"o\" operator=\"in\"/>");
        out.push_back(pad + "  <feMerge><feMergeNode/><feMergeNode in=\"SourceGraphic\"/></feMerge>");
        out.push_back(pad + "</filter>");
    } else if (t == "glow") {
        out.push_back(pad + "<filter" + id + region + ">");
        out.push_back(pad + "  <feGaussianBlur in=\"SourceAlpha\"" + attrn("stdDeviation", d.at("r").n) + " result=\"b\"/>");
        out.push_back(pad + "  " + flood(d.at("c").s, 1));
        out.push_back(pad + "  <feComposite in2=\"b\" operator=\"in\" result=\"g\"/>");
        out.push_back(pad + "  <feMerge><feMergeNode in=\"g\"/><feMergeNode in=\"g\"/><feMergeNode in=\"SourceGraphic\"/></feMerge>");
        out.push_back(pad + "</filter>");
    } else if (t == "blur") {
        out.push_back(pad + "<filter" + id + region + ">");
        out.push_back(pad + "  <feGaussianBlur" + attrn("stdDeviation", d.at("r").n) + "/>");
        out.push_back(pad + "</filter>");
    } else if (t == "pattern") {
        out.push_back(pad + "<pattern" + id + attrn("width", d.at("w").n) + attrn("height", d.at("h").n) + " patternUnits=\"userSpaceOnUse\">");
        if (d.has("nodes")) for (auto& ch : d.at("nodes").a) emit(out, ch, ind + 2);
        out.push_back(pad + "</pattern>");
    } else if (t == "clip") {
        out.push_back(pad + "<clipPath" + id + ">");
        if (d.has("nodes")) for (auto& ch : d.at("nodes").a) emit(out, ch, ind + 2);
        out.push_back(pad + "</clipPath>");
    }
}

// ---------------------------------------------------------------------------
//  SVG
// ---------------------------------------------------------------------------
inline std::string render_svg(const J& c) {
    if (!c.isObj() || !c.has("piano")) throw std::runtime_error("piano: هذا ليس مشهد piano (حقل piano مفقود)");
    std::string w = n2(c.at("w").n), h = n2(c.at("h").n);
    std::vector<std::string> out;
    std::string root = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + w + "\" height=\"" + h + "\" viewBox=\"0 0 " + w + " " + h + "\"";
    if (c.has("font")) root += attr("font-family", c.at("font").s);
    out.push_back(root + ">");
    if (c.has("title") && tostr(c.at("title")) != "") out.push_back("  <title>" + esc(tostr(c.at("title"))) + "</title>");
    if (c.has("defs") && !c.at("defs").a.empty()) {
        out.push_back("  <defs>");
        for (auto& d : c.at("defs").a) emit_def(out, d, 4);
        out.push_back("  </defs>");
    }
    if (c.has("bg") && c.at("bg").s != "none")
        out.push_back("  <rect width=\"100%\" height=\"100%\"" + paint_v(&c.at("bg"), nullptr, "fill", "fill-opacity") + "/>");
    if (c.has("nodes")) for (auto& n : c.at("nodes").a) emit(out, n, 2);
    out.push_back("</svg>");
    return join(out, "\n");
}

}  // namespace piano
