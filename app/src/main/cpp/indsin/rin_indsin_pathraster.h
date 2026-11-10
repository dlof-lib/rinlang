// indsin/rin_indsin_pathraster.h — تسطيح مسارات SVG (M L H V C S Q T A Z، مطلقة ونسبية) إلى خطوط متعدّدة النقاط
// لعملية الرسم DrawOp::STROKE_PATH. بلا اعتماديات؛ يتسامح مع الأخطاء (يتوقف عند أول أمر غير مفهوم ويعيد ما تراكم).
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

namespace indsin {

struct PathPt { double x, y; };
using PathPoly = std::vector<PathPt>;

inline std::vector<PathPoly> flattenSvgPath(const std::string& d, int maxPoints = 20000) {
    std::vector<PathPoly> out; PathPoly cur;
    size_t i = 0; char cmd = 0; double cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0; char prev = 0;
    auto skip = [&] { while (i < d.size() && (std::isspace((unsigned char)d[i]) || d[i] == ',')) ++i; };
    auto num = [&](double& v) -> bool {
        skip(); if (i >= d.size()) return false;
        const char* s = d.c_str() + i; char* e = nullptr; v = std::strtod(s, &e);
        if (e == s) return false; i += (size_t)(e - s); return true;
    };
    auto flag = [&](double& v) -> bool {
        skip(); if (i >= d.size() || (d[i] != '0' && d[i] != '1')) return false; v = d[i] - '0'; ++i; return true;
    };
    auto flush = [&] { if (cur.size() >= 1) out.push_back(cur); cur.clear(); };
    auto add = [&](double x, double y) { if ((int)cur.size() < maxPoints) cur.push_back({x, y}); cx = x; cy = y; };
    auto cubic = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
        double x0 = cx, y0 = cy;
        for (int k = 1; k <= 16; ++k) {
            double t = k / 16.0, u = 1 - t;
            add(u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3, u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3);
        }
        lcx = x2; lcy = y2;
    };
    auto quad = [&](double x1, double y1, double x2, double y2) {
        double x0 = cx, y0 = cy;
        for (int k = 1; k <= 12; ++k) { double t = k / 12.0, u = 1 - t; add(u*u*x0 + 2*u*t*x1 + t*t*x2, u*u*y0 + 2*u*t*y1 + t*t*y2); }
        lcx = x1; lcy = y1;
    };
    auto arc = [&](double rx, double ry, double rot, double fa, double fs, double x, double y) {
        double x0 = cx, y0 = cy;
        if (rx == 0 || ry == 0 || (x0 == x && y0 == y)) { add(x, y); return; }
        rx = std::fabs(rx); ry = std::fabs(ry);
        double phi = rot * M_PI / 180.0, cp = std::cos(phi), sp = std::sin(phi);
        double dx = (x0 - x) / 2, dy = (y0 - y) / 2;
        double x1p = cp*dx + sp*dy, y1p = -sp*dx + cp*dy;
        double lam = (x1p*x1p)/(rx*rx) + (y1p*y1p)/(ry*ry);
        if (lam > 1) { double s = std::sqrt(lam); rx *= s; ry *= s; }
        double num_ = rx*rx*ry*ry - rx*rx*y1p*y1p - ry*ry*x1p*x1p, den = rx*rx*y1p*y1p + ry*ry*x1p*x1p;
        double co = den == 0 ? 0 : std::sqrt(std::fmax(0.0, num_ / den)); if (fa == fs) co = -co;
        double cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
        double ccx = cp*cxp - sp*cyp + (x0 + x) / 2, ccy = sp*cxp + cp*cyp + (y0 + y) / 2;
        auto ang = [](double ux, double uy, double vx, double vy) {
            double a = std::atan2(ux*vy - uy*vx, ux*vx + uy*vy); return a;
        };
        double th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        double dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!fs && dth > 0) dth -= 2 * M_PI; else if (fs && dth < 0) dth += 2 * M_PI;
        int n = std::max(4, (int)std::ceil(std::fabs(dth) / (M_PI / 16)));
        for (int k = 1; k <= n; ++k) {
            double t = th1 + dth * k / n, ex = rx * std::cos(t), ey = ry * std::sin(t);
            add(cp*ex - sp*ey + ccx, sp*ex + cp*ey + ccy);
        }
        cx = x; cy = y;
    };
    while (true) {
        skip(); if (i >= d.size()) break;
        if (std::isalpha((unsigned char)d[i])) { cmd = d[i++]; } else if (!cmd) break;
        const bool rel = std::islower((unsigned char)cmd); const char C = (char)std::toupper((unsigned char)cmd);
        double a, b, c2, d2, e, f, g;
        if (C == 'Z') { if (!cur.empty()) { cur.push_back({sx, sy}); cx = sx; cy = sy; } flush(); prev = 'Z'; cmd = 0; continue; }
        if (C == 'M') {
            if (!num(a) || !num(b)) break;
            flush(); if (rel) { a += cx; b += cy; }
            add(a, b); sx = a; sy = b; cmd = rel ? 'l' : 'L'; prev = 'M'; continue;
        }
        if (!std::strchr("LHVCSQTA", C)) break;
        if (cur.empty()) { cur.push_back({cx, cy}); }
        if (C == 'L') { if (!num(a) || !num(b)) break; if (rel) { a += cx; b += cy; } add(a, b); }
        else if (C == 'H') { if (!num(a)) break; add(rel ? cx + a : a, cy); }
        else if (C == 'V') { if (!num(a)) break; add(cx, rel ? cy + a : a); }
        else if (C == 'C') {
            if (!num(a) || !num(b) || !num(c2) || !num(d2) || !num(e) || !num(f)) break;
            if (rel) { a += cx; b += cy; c2 += cx; d2 += cy; e += cx; f += cy; }
            cubic(a, b, c2, d2, e, f);
        } else if (C == 'S') {
            if (!num(c2) || !num(d2) || !num(e) || !num(f)) break;
            if (rel) { c2 += cx; d2 += cy; e += cx; f += cy; }
            double x1 = cx, y1 = cy; if (prev == 'C' || prev == 'S') { x1 = 2*cx - lcx; y1 = 2*cy - lcy; }
            cubic(x1, y1, c2, d2, e, f);
        } else if (C == 'Q') {
            if (!num(a) || !num(b) || !num(c2) || !num(d2)) break;
            if (rel) { a += cx; b += cy; c2 += cx; d2 += cy; }
            quad(a, b, c2, d2);
        } else if (C == 'T') {
            if (!num(c2) || !num(d2)) break;
            if (rel) { c2 += cx; d2 += cy; }
            double x1 = cx, y1 = cy; if (prev == 'Q' || prev == 'T') { x1 = 2*cx - lcx; y1 = 2*cy - lcy; }
            quad(x1, y1, c2, d2);
        } else if (C == 'A') {
            if (!num(a) || !num(b) || !num(c2) || !flag(d2) || !flag(e) || !num(f) || !num(g)) break;
            if (rel) { f += cx; g += cy; }
            arc(a, b, c2, d2, e, f, g);
        } else break;
        prev = C;
        if (C != 'C' && C != 'S' && C != 'Q' && C != 'T') { lcx = cx; lcy = cy; }
    }
    flush();
    return out;
}

} // namespace indsin
