// loom/rin_loom_paint.h — Dye: paint engine (Strand geometry -> DrawList -> raster/JSON).
#pragma once
#include "rin_loom_strand.h"
#include <fstream>
#include <sstream>

namespace loom {

struct Color { unsigned char r,g,b; };
inline Color colorForKind(StrandKind k) {
    switch (k) {
        case StrandKind::CARD:   return {40,42,54};
        case StrandKind::BUTTON: return {124,92,255};
        case StrandKind::TEXT:   return {230,230,240};
        case StrandKind::IMAGE:  return {70,70,90};
        case StrandKind::DIVIDER:return {51,51,63};
        case StrandKind::BANNER: return {44,47,61}; // neutral/"custom" default; see bannerTypeColor() below
        default: return {24,25,32};
    }
}
// Banner-specific palette for its `type` attr (info/success/warning/error/action/progress/custom),
// used only when the .rin source didn't already override with an explicit color="#RRGGBB".
inline Color bannerTypeColor(const std::string& type) {
    if (type == "success")  return {46,160,67};
    if (type == "warning")  return {212,167,44};
    if (type == "error")    return {209,69,69};
    if (type == "action")   return {124,92,255};
    if (type == "progress") return {58,110,196};
    if (type == "info")     return {58,110,196};
    return colorForKind(StrandKind::BANNER); // "custom" / unset -> neutral
}
inline Color parseHexColor(const std::string& hex, Color fallback) {
    if (hex.size() < 7 || hex[0] != '#') return fallback;
    auto hx = [&](int i){ return (unsigned char)std::stoul(hex.substr(i,2), nullptr, 16); };
    try { return {hx(1), hx(3), hx(5)}; } catch (...) { return fallback; }
}
inline Color resolveColor(const StrandPtr& s) {
    Color fallback = (s->kind == StrandKind::BANNER)
        ? bannerTypeColor(s->attrStr("type", ""))
        : colorForKind(s->kind);
    auto c = s->attr("color");
    if (c && c->kind == Value::Kind::STRING && !c->str.empty() && c->str[0] == '#')
        return parseHexColor(c->str, fallback);
    return fallback;
}

enum class DrawOp { FILL_RECT, TEXT_RUN };
struct DrawCommand { DrawOp op; Rect bounds; Color color; std::string text; StrandId owner; };
using DrawList = std::vector<DrawCommand>;

struct Dye {
    DrawList paint(const StrandPtr& s) { DrawList list; paintInto(s, list); return list; }
    void paintInto(const StrandPtr& s, DrawList& list) {
        if (s->kind != StrandKind::TEXT)
            list.push_back({DrawOp::FILL_RECT, s->geometry, resolveColor(s), "", s->id});
        if (s->kind == StrandKind::TEXT)
            list.push_back({DrawOp::TEXT_RUN, s->geometry, resolveColor(s), s->attrStr("text"), s->id});
        if (s->kind == StrandKind::BUTTON)
            list.push_back({DrawOp::TEXT_RUN, s->geometry, {255,255,255}, s->attrStr("label"), s->id});
        for (auto& c : s->children) paintInto(c, list);
    }
};

inline void rasterizeToPPM(const DrawList& list, int W, int H, const std::string& path) {
    std::vector<unsigned char> buf(W*H*3, 18);
    auto setPx = [&](int x,int y, Color c){ if (x<0||y<0||x>=W||y>=H) return; int i=(y*W+x)*3; buf[i]=c.r; buf[i+1]=c.g; buf[i+2]=c.b; };
    for (auto& cmd : list) {
        if (cmd.op == DrawOp::FILL_RECT) {
            int x0=(int)cmd.bounds.x, y0=(int)cmd.bounds.y, x1=(int)(cmd.bounds.x+cmd.bounds.w), y1=(int)(cmd.bounds.y+cmd.bounds.h);
            for (int y=y0;y<y1;y++) for (int x=x0;x<x1;x++) setPx(x,y,cmd.color);
        } else {
            int x0=(int)cmd.bounds.x+4, y=(int)(cmd.bounds.y + cmd.bounds.h/2);
            for (size_t i=0;i<cmd.text.size();i++) for (int dx=0; dx<6; dx++) setPx(x0 + (int)i*8 + dx, y, cmd.color);
        }
    }
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    f.write((char*)buf.data(), buf.size());
}

// JSON serialization of the full Fabric (geometry + resolved attrs) — this is what crosses the
// C API / JNI boundary to a Kotlin Canvas client (see rin_loom_c_api.cpp / jni_bridge.cpp).
inline std::string jsonEscape(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (c=='"'||c=='\\') { out+='\\'; out+=c; }
        else if (c=='\n') out += "\\n";
        else out += c;
    }
    return out;
}
inline void fabricToJson(const StrandPtr& s, std::ostringstream& os) {
    os << "{\"kind\":\"" << strandKindName(s->kind) << "\",\"name\":\"" << jsonEscape(s->name) << "\""
       << ",\"line\":" << s->sourceLine
       << ",\"x\":" << s->geometry.x << ",\"y\":" << s->geometry.y
       << ",\"w\":" << s->geometry.w << ",\"h\":" << s->geometry.h
       << ",\"attrs\":{";
    for (size_t i=0;i<s->attrs.size();i++) {
        if (i) os << ",";
        os << "\"" << jsonEscape(s->attrs[i].key) << "\":\"" << jsonEscape(s->attrs[i].value.asString()) << "\"";
    }
    os << "},\"children\":[";
    for (size_t i=0;i<s->children.size();i++) { if (i) os << ","; fabricToJson(s->children[i], os); }
    os << "]}";
}
inline std::string fabricToJsonString(const StrandPtr& s) {
    std::ostringstream os; fabricToJson(s, os); return os.str();
}

} // namespace loom
