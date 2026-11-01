// ============================================================================
//  rin_extra_natives11.cpp — مفهوم `algo`: أدوات تطوير الخوارزميات والحساب العددي (algo*)
// ----------------------------------------------------------------------------
//  دفعة حادية عشرة فوق rin_extra_natives*.cpp دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra11() وتُضمَّن (#include) في نهاية rin_interpreter.cpp
//  بعد rin_extra_natives10.cpp (التي تعطيها LU/SVD/القيم الذاتية). الشرح الكامل: docs/ALGO.md
//
//  الدمج لا الإنشاء من الصفر:
//    • algoPolyFit/algoNewton/algoPolyRoots تقوم على محرّك الجبر الخطي نفسه (SVD، LU، القيم الذاتية)
//      من rin_extra_natives10.cpp — لا نسخة ثانية من أي منها.
//    • التساوي التقريبي في algoCheck بدلالة numberApprox (rel=1e-9, abs=1e-12) من مفهوم number.
//    • الدوال المُمرَّرة (f, fn) هي دوال Rin العادية أو كائنات __call__ عبر callValue — نفس مسار
//      map/filter/Set.reduce — فتعمل معها lambdas والإغلاقات (closures) كما هي.
//    • المصفوفات/المتجهات هي مصفوفات Rin العادية (انظر docs/LINALG.md)، والأخطاء بنظام التشخيص نفسه.
//
//  حلّ المعادلات والتحسين:
//    algoRoot(f, a, b[, opts])  algoRoot(f, x0[, opts])  algoBracket(f, x0[, opts])
//    algoMinimize(f, a, b[, opts])   algoMinimize(f, x0Array[, opts])
//    algoNewton(F, x0[, opts])    جملة معادلات غير خطية F(x)=0 (جاكوبيان عددي + LU)
//  التفاضل والتكامل:
//    algoDiff(f, x[, opts])  algoGrad(f, x)  algoJacobian(F, x)
//    algoIntegrate(f, a, b[, opts])  algoTrapz(ys[, xs|dx])
//  المعادلات التفاضلية والاستيفاء والتوفيق:
//    algoOde(f, y0, t0, t1[, opts])  (rk45 | rk4 | stiff)  algoInterp(xs, ys, x[, opts])
//    algoPolyFit(xs, ys, deg)  algoPolyEval(c, x)  algoPolyRoots(c)
//  الإشارات والبيانات:
//    algoFFT(signal[, opts])  algoRandom(n[, opts])
//  القياس والتحقق (لتطوير خوارزميتك أنت):
//    algoBench(fn[, opts])  algoComplexity(fn, sizes[, opts])  algoCompare(fns[, opts])  algoCheck(fn, ref, inputs[, opts])
//  المخططات (على مصفوفة الأوزان):
//    algoGraphMatrix(edges, n[, opts])  algoDijkstra(W, src[, dst])  algoFloyd(W)
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <functional>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <random>

namespace rin {
namespace extra11 {

using namespace extra10;
using Cd = std::complex<double>;

static const double kPi11 = 3.14159265358979323846;
static bool isFn11(const Value& v) { return v.type == Value::Type::FUNCTION || v.type == Value::Type::INSTANCE; }
static void needFn11(const Value& v, const char* fn, const char* what, int line) {
    if (!isFn11(v)) failType10(fn, std::string("'") + what + "' must be a function but got " + v.typeName(), line);
}
static std::string fmt11(double v) {
    char buf[40]; std::snprintf(buf, sizeof buf, "%.10g", v); return buf;
}

// مقارنة عميقة بتسامح رقمي (دلالة numberApprox لكل رقم)
static bool approxEq11(const Value& p, const Value& q, double rel, double ab) {
    if (p.type != q.type) return false;
    switch (p.type) {
        case Value::Type::NIL: return true;
        case Value::Type::BOOL: return p.boolean == q.boolean;
        case Value::Type::STRING: return p.str == q.str;
        case Value::Type::NUMBER: {
            if (std::isnan(p.number) || std::isnan(q.number)) return false;
            if (p.number == q.number) return true;
            if (std::isinf(p.number) || std::isinf(q.number)) return false;
            return std::fabs(p.number - q.number) <= std::max(rel * std::max(std::fabs(p.number), std::fabs(q.number)), ab);
        }
        case Value::Type::ARRAY: {
            if (!p.array || !q.array || p.array->size() != q.array->size()) return false;
            for (size_t i = 0; i < p.array->size(); ++i) if (!approxEq11((*p.array)[i], (*q.array)[i], rel, ab)) return false;
            return true;
        }
        case Value::Type::MAP: {
            if (!p.map || !q.map || p.map->size() != q.map->size()) return false;
            for (const auto& kv : *p.map) { const Value* o = q.map->findValue(kv.first); if (!o || !approxEq11(kv.second, *o, rel, ab)) return false; }
            return true;
        }
        default: return false;
    }
}
static std::string brief11(const Value& v, int depth = 0) {
    switch (v.type) {
        case Value::Type::NIL: return "nil";
        case Value::Type::BOOL: return v.boolean ? "true" : "false";
        case Value::Type::NUMBER: return fmt11(v.number);
        case Value::Type::STRING: return "\"" + v.str + "\"";
        case Value::Type::ARRAY: {
            if (!v.array) return "[]"; std::string s = "[";
            for (size_t i = 0; i < v.array->size(); ++i) { if (i) s += ", "; if (i >= 8) { s += "... +" + std::to_string(v.array->size() - 8); break; } s += depth > 2 ? "..." : brief11((*v.array)[i], depth + 1); }
            return s + "]";
        }
        case Value::Type::MAP: {
            if (!v.map) return "{}"; std::string s = "{"; size_t i = 0;
            for (const auto& kv : *v.map) { if (i++) s += ", "; if (i > 8) { s += "..."; break; } s += brief11(kv.first, depth + 1) + ": " + (depth > 2 ? "..." : brief11(kv.second, depth + 1)); }
            return s + "}";
        }
        default: return v.typeName();
    }
}

// ------------------------------------------------------------------ Gauss–Kronrod (7,15)
static const double XGK[8] = {0.991455371120812639206854697526329, 0.949107912342758524526189684047851, 0.864864423359769072789712788640926, 0.741531185599394439863864773280788,
                              0.586087235467691130294144838258730, 0.405845151377397166906606412076961, 0.207784955007898467600689403773245, 0.0};
static const double WGK[8] = {0.022935322010529224963732008058970, 0.063092092629978553290700663189204, 0.104790010322250183839876322541518, 0.140653259715525918745189590510238,
                              0.169004726639267902826583426598550, 0.190350578064785409913256402421014, 0.204432940075298892414161999234649, 0.209482141084727828012999174891714};
static const double WG[4] = {0.129484966168869693270611432679082, 0.279705391489276667901467771423780, 0.381830050505118944950369775488975, 0.417959183673469387755102040816327};

struct Seg11 { double a, b, val, err; };

// ------------------------------------------------------------------ FFT
static void fftRadix2_11(std::vector<Cd>& a, bool inv) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi11 / static_cast<double>(len) * (inv ? 1 : -1);
        const Cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Cd w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                // w recalculated exactly every 64 steps to limit drift
                if (k % 64 == 0) w = Cd(std::cos(ang * static_cast<double>(k)), std::sin(ang * static_cast<double>(k)));
                const Cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}
static void fftAny11(std::vector<Cd>& a, bool inv) {
    const size_t n = a.size();
    if (n <= 1) return;
    if ((n & (n - 1)) == 0) { fftRadix2_11(a, inv); return; }
    if (inv) { for (auto& x : a) x = std::conj(x); fftAny11(a, false); for (auto& x : a) x = std::conj(x); return; }
    size_t m = 1; while (m < 2 * n - 1) m <<= 1;
    std::vector<Cd> c(n), A(m), B(m);
    for (size_t k = 0; k < n; ++k) {
        const unsigned long long k2 = (static_cast<unsigned long long>(k) * k) % (2ULL * n);
        const double ang = -kPi11 * static_cast<double>(k2) / static_cast<double>(n);
        c[k] = Cd(std::cos(ang), std::sin(ang));
    }
    for (size_t k = 0; k < n; ++k) A[k] = a[k] * c[k];
    B[0] = std::conj(c[0]);
    for (size_t k = 1; k < n; ++k) B[k] = B[m - k] = std::conj(c[k]);
    fftRadix2_11(A, false); fftRadix2_11(B, false);
    for (size_t i = 0; i < m; ++i) A[i] *= B[i];
    fftRadix2_11(A, true);
    for (size_t k = 0; k < n; ++k) a[k] = c[k] * A[k] / static_cast<double>(m);
}

static uint64_t seedFrom11(const Value* o, const char* fn, int line) {
    if (o && get10(*o, "seed")) return seedMix10(static_cast<uint64_t>(static_cast<int64_t>(optNum10(o, "seed", 0, fn, line))));
    return (static_cast<uint64_t>(std::random_device{}()) << 32) ^ std::random_device{}();
}

} // namespace extra11

// ============================================================================
void Interpreter::registerNativesExtra11() {
    using namespace extra10;
    using namespace extra11;

    // ------------------------------------------------------------ استدعاء دوال Rin
    struct CB {
        Interpreter* I; Value f; const char* fn; int line;
        Value call(std::vector<Value> args) const { return I->callValue(f, args, line); }
        double num(std::vector<Value> args, const char* what = "the function") const {
            Value v = call(std::move(args));
            if (v.type != Value::Type::NUMBER) failType10(fn, std::string(what) + " must return a number but returned " + v.typeName(), line);
            return v.number;
        }
        double at(double x) const { return num({N10(x)}); }
        std::vector<double> vec(const std::vector<double>& x, const char* what = "the function") const {
            Value v = call({vecVal(x)});
            return readVec10(v, fn, what, line, false, true);
        }
    };

    // ======================================================== algoRoot / algoBracket
    // algoRoot(f, a, b[, opts])  — قوسٌ يغيّر الإشارة: brent (افتراضي) | bisect
    // algoRoot(f, x0[, opts])    — من نقطة: newton (opts.df اختياري) | secant
    natives["algoRoot"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoRoot", a, 2, 4, line);
        needFn11(a[0], "algoRoot", "f", line);
        CB f{this, a[0], "algoRoot", line};
        const bool bracket = a.size() >= 3 && a[2].type == Value::Type::NUMBER;
        const Value* o = optMap10(a, bracket ? 3 : 2, "algoRoot", line);
        const double tol = optNum10(o, "tol", 1e-12, "algoRoot", line);
        const long maxIter = static_cast<long>(optNum10(o, "maxIter", 200, "algoRoot", line));
        std::string method = optStr10(o, "method", bracket ? "brent" : "newton");
        long evals = 0, it = 0; bool conv = false; double root = 0, fr = 0;
        auto F = [&](double x) { ++evals; return f.at(x); };
        if (bracket) {
            if (method != "brent" && method != "bisect") failArg10("algoRoot", "with an interval [a, b] the method must be \"brent\" or \"bisect\" (for \"newton\"/\"secant\" pass a starting point x0)", line);
            double lo = needNum10(a[1], "algoRoot", "a", line), hi = needNum10(a[2], "algoRoot", "b", line);
            if (!(std::isfinite(lo) && std::isfinite(hi)) || lo == hi) failArg10("algoRoot", "'a' and 'b' must be finite and different", line);
            double fa = F(lo), fb = F(hi);
            if (std::isnan(fa) || std::isnan(fb)) failRun10("algoRoot", "f returned NaN at an endpoint", line);
            if (fa == 0.0) { root = lo; fr = 0; conv = true; }
            else if (fb == 0.0) { root = hi; fr = 0; conv = true; }
            else {
                if ((fa > 0) == (fb > 0)) failRun10("algoRoot", "no sign change: f(" + fmt11(lo) + ")=" + fmt11(fa) + " and f(" + fmt11(hi) + ")=" + fmt11(fb) + " — widen the interval or use algoBracket", line);
                if (method == "bisect") {
                    if (lo > hi) { std::swap(lo, hi); std::swap(fa, fb); }
                    double mid = lo;
                    for (; it < maxIter * 5; ++it) {
                        mid = 0.5 * (lo + hi); const double fm = F(mid);
                        if (fm == 0.0 || 0.5 * (hi - lo) <= tol) { conv = true; fr = fm; break; }
                        if ((fm > 0) == (fa > 0)) { lo = mid; fa = fm; } else { hi = mid; fb = fm; }
                    }
                    root = mid; if (!conv) fr = F(mid);
                } else {
                    double A = lo, B = hi, FA = fa, FB = fb, C = hi, FC = fb, d = 0, e = 0;
                    for (it = 1; it <= maxIter; ++it) {
                        if ((FB > 0 && FC > 0) || (FB < 0 && FC < 0)) { C = A; FC = FA; e = d = B - A; }
                        if (std::fabs(FC) < std::fabs(FB)) { A = B; B = C; C = A; FA = FB; FB = FC; FC = FA; }
                        const double tol1 = 2.0 * kEps10 * std::fabs(B) + 0.5 * tol, xm = 0.5 * (C - B);
                        if (std::fabs(xm) <= tol1 || FB == 0.0) { conv = true; break; }
                        if (std::fabs(e) >= tol1 && std::fabs(FA) > std::fabs(FB)) {
                            double p, q, r; const double s = FB / FA;
                            if (A == C) { p = 2.0 * xm * s; q = 1.0 - s; }
                            else { q = FA / FC; r = FB / FC; p = s * (2.0 * xm * q * (q - r) - (B - A) * (r - 1.0)); q = (q - 1.0) * (r - 1.0) * (s - 1.0); }
                            if (p > 0) q = -q;
                            p = std::fabs(p);
                            const double m1 = 3.0 * xm * q - std::fabs(tol1 * q), m2 = std::fabs(e * q);
                            if (2.0 * p < std::min(m1, m2)) { e = d; d = p / q; } else { d = xm; e = d; }
                        } else { d = xm; e = d; }
                        A = B; FA = FB;
                        if (std::fabs(d) > tol1) B += d; else B += (xm >= 0 ? tol1 : -tol1);
                        FB = F(B);
                    }
                    root = B; fr = FB;
                }
            }
        } else {
            if (method != "newton" && method != "secant") failArg10("algoRoot", "from a starting point the method must be \"newton\" or \"secant\"", line);
            double x = needNum10(a[1], "algoRoot", "x0", line);
            if (method == "newton") {
                const Value* dfv = o ? get10(*o, "df") : nullptr;
                const bool hasDf = dfv && dfv->type != Value::Type::NIL;
                if (hasDf) needFn11(*dfv, "algoRoot", "df", line);
                CB df{this, hasDf ? *dfv : Value(), "algoRoot", line};
                double fx = F(x);
                for (it = 1; it <= maxIter; ++it) {
                    if (fx == 0.0) { conv = true; break; }
                    double d;
                    if (hasDf) { ++evals; d = df.at(x); }
                    else { const double h = std::cbrt(kEps10) * std::max(1.0, std::fabs(x)); d = (F(x + h) - F(x - h)) / (2.0 * h); }
                    if (d == 0.0 || !std::isfinite(d)) failRun10("algoRoot", "derivative is zero or not finite at x=" + fmt11(x) + " — try another x0 or a bracketing method", line);
                    const double dx = fx / d; x -= dx; fx = F(x);
                    if (!std::isfinite(x)) failRun10("algoRoot", "Newton diverged", line);
                    if (std::fabs(dx) <= tol * (1.0 + std::fabs(x))) { conv = true; break; }
                }
                root = x; fr = fx;
            } else {
                double x0 = x, x1 = x + (std::fabs(x) > 1e-8 ? 1e-4 * std::fabs(x) : 1e-4), f0 = F(x0), f1 = F(x1);
                for (it = 1; it <= maxIter; ++it) {
                    if (f1 == f0) { if (f1 == 0.0) { conv = true; } else failRun10("algoRoot", "secant: flat step (f(x1)==f(x0)) — try another x0", line); break; }
                    const double x2 = x1 - f1 * (x1 - x0) / (f1 - f0);
                    x0 = x1; f0 = f1; x1 = x2; f1 = F(x1);
                    if (!std::isfinite(x1)) failRun10("algoRoot", "secant diverged", line);
                    if (std::fabs(x1 - x0) <= tol * (1.0 + std::fabs(x1)) || f1 == 0.0) { conv = true; break; }
                }
                root = x1; fr = f1;
            }
        }
        auto m = std::make_shared<MapData>();
        put10(m, "root", N10(root)); put10(m, "fx", N10(fr)); put10(m, "iterations", N10(static_cast<double>(it)));
        put10(m, "evaluations", N10(static_cast<double>(evals))); put10(m, "converged", B10(conv)); put10(m, "method", S10(method));
        return Value::makeMap(m);
    };
    // algoBracket(f, x0[, {step=1, grow=1.6, maxTries=60}]) -> {a, b, fa, fb} حيث تتغيّر إشارة f
    natives["algoBracket"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoBracket", a, 2, 3, line);
        needFn11(a[0], "algoBracket", "f", line);
        CB f{this, a[0], "algoBracket", line};
        const double x0 = needNum10(a[1], "algoBracket", "x0", line);
        const Value* o = optMap10(a, 2, "algoBracket", line);
        double step = optNum10(o, "step", 1.0, "algoBracket", line); const double grow = optNum10(o, "grow", 1.6, "algoBracket", line);
        const long tries = static_cast<long>(optNum10(o, "maxTries", 60, "algoBracket", line));
        if (!(step > 0) || !(grow > 1)) failArg10("algoBracket", "'step' must be > 0 and 'grow' > 1", line);
        const double f0 = f.at(x0);
        if (f0 == 0.0) { auto m = std::make_shared<MapData>(); put10(m, "a", N10(x0)); put10(m, "b", N10(x0)); put10(m, "fa", N10(0)); put10(m, "fb", N10(0)); return Value::makeMap(m); }
        for (long i = 0; i < tries; ++i, step *= grow) {
            for (int dir = -1; dir <= 1; dir += 2) {
                const double x = x0 + dir * step, fx = f.at(x);
                if (std::isfinite(fx) && ((fx > 0) != (f0 > 0) || fx == 0.0)) {
                    auto m = std::make_shared<MapData>(); const double lo = std::min(x0, x), hi = std::max(x0, x);
                    put10(m, "a", N10(lo)); put10(m, "b", N10(hi)); put10(m, "fa", N10(lo == x0 ? f0 : fx)); put10(m, "fb", N10(lo == x0 ? fx : f0));
                    return Value::makeMap(m);
                }
            }
        }
        failRun10("algoBracket", "no sign change found around x0=" + fmt11(x0) + " after " + std::to_string(tries) + " expansions", line);
    };

    // ======================================================== algoMinimize
    // algoMinimize(f, a, b[, opts])  — متغيّر واحد على [a,b] (Brent)
    // algoMinimize(f, x0[, opts])    — عدة متغيّرات (Nelder–Mead)؛ f تستقبل مصفوفة
    natives["algoMinimize"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoMinimize", a, 2, 4, line);
        needFn11(a[0], "algoMinimize", "f", line);
        CB f{this, a[0], "algoMinimize", line};
        long evals = 0, it = 0; bool conv = false;
        auto m = std::make_shared<MapData>();
        if (a[1].type == Value::Type::NUMBER) {
            if (a.size() < 3 || a[2].type != Value::Type::NUMBER) failArg10("algoMinimize", "one variable: algoMinimize(f, a, b[, opts]); several: algoMinimize(f, [x0, ...][, opts])", line);
            double lo = a[1].number, hi = a[2].number;
            if (!(std::isfinite(lo) && std::isfinite(hi)) || lo >= hi) failArg10("algoMinimize", "need finite a < b", line);
            const Value* o = optMap10(a, 3, "algoMinimize", line);
            const double tol = optNum10(o, "tol", 1e-10, "algoMinimize", line); const long maxIter = static_cast<long>(optNum10(o, "maxIter", 500, "algoMinimize", line));
            auto F = [&](double x) { ++evals; return f.at(x); };
            const double c = 0.3819660112501051, eps = std::sqrt(kEps10);
            double v = lo + c * (hi - lo), w = v, x = v, e = 0, d = 0, fx = F(x), fv = fx, fw = fx;
            for (it = 1; it <= maxIter; ++it) {
                const double xm = 0.5 * (lo + hi), tol1 = eps * std::fabs(x) + tol / 3.0, t2 = 2.0 * tol1;
                if (std::fabs(x - xm) <= t2 - 0.5 * (hi - lo)) { conv = true; break; }
                double p = 0, q = 0, r = 0;
                if (std::fabs(e) > tol1) {
                    r = (x - w) * (fx - fv); q = (x - v) * (fx - fw); p = (x - v) * q - (x - w) * r; q = 2.0 * (q - r);
                    if (q > 0) p = -p; q = std::fabs(q); r = e; e = d;
                }
                double u;
                if (std::fabs(p) >= std::fabs(0.5 * q * r) || p <= q * (lo - x) || p >= q * (hi - x)) { e = x < xm ? hi - x : lo - x; d = c * e; }
                else { d = p / q; u = x + d; if (u - lo < t2 || hi - u < t2) d = xm >= x ? tol1 : -tol1; }
                u = std::fabs(d) >= tol1 ? x + d : x + (d >= 0 ? tol1 : -tol1);
                const double fu = F(u);
                if (fu <= fx) { if (u < x) hi = x; else lo = x; v = w; w = x; x = u; fv = fw; fw = fx; fx = fu; }
                else {
                    if (u < x) lo = u; else hi = u;
                    if (fu <= fw || w == x) { v = w; fv = fw; w = u; fw = fu; }
                    else if (fu <= fv || v == x || v == w) { v = u; fv = fu; }
                }
            }
            put10(m, "x", N10(x)); put10(m, "fx", N10(fx));
        } else {
            std::vector<double> x0 = readVec10(a[1], "algoMinimize", "x0", line, true);
            const Value* o = optMap10(a, 2, "algoMinimize", line);
            const size_t n = x0.size();
            const double xtol = optNum10(o, "tol", 1e-9, "algoMinimize", line), ftol = optNum10(o, "ftol", 1e-12, "algoMinimize", line);
            const long maxIter = static_cast<long>(optNum10(o, "maxIter", 400.0 * static_cast<double>(n), "algoMinimize", line));
            auto F = [&](const std::vector<double>& x) { ++evals; double v = f.num({vecVal(x)}, "the objective"); return std::isnan(v) ? INFINITY : v; };
            if (optStr10(o, "method", "nelder-mead") == "bfgs") {
                const Value* gv = o ? get10(*o, "grad") : nullptr; const bool hasG = gv && gv->type != Value::Type::NIL;
                if (hasG) needFn11(*gv, "algoMinimize", "grad", line);
                CB gf{this, hasG ? *gv : Value(), "algoMinimize", line};
                auto Fx = [&](const std::vector<double>& x) { ++evals; return f.num({vecVal(x)}, "the objective"); };
                auto Gx = [&](const std::vector<double>& x) {
                    if (hasG) { auto g = gf.vec(x, "grad"); if (g.size() != n) failRun10("algoMinimize", "grad must return " + std::to_string(n) + " components", line); return g; }
                    std::vector<double> g(n);
                    for (size_t i = 0; i < n; ++i) { const double h = std::cbrt(kEps10) * std::max(1.0, std::fabs(x[i])); auto p = x, q = x; p[i] += h; q[i] -= h; g[i] = (Fx(p) - Fx(q)) / (2.0 * h); }
                    return g;
                };
                const double gtol = optNum10(o, "gtol", 1e-8, "algoMinimize", line);
                std::vector<double> x = x0, g = Gx(x); double fx = Fx(x); Mat H = identity10(n); bool first = true;
                for (it = 1; it <= maxIter; ++it) {
                    double gi = 0; for (double e : g) gi = std::max(gi, std::fabs(e));
                    if (gi <= gtol * (1.0 + std::fabs(fx))) { conv = true; break; }
                    std::vector<double> p(n, 0.0); for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) p[i] -= H(i, j) * g[j];
                    double gp = 0; for (size_t i = 0; i < n; ++i) gp += g[i] * p[i];
                    if (!(gp < 0)) { H = identity10(n); for (size_t i = 0; i < n; ++i) p[i] = -g[i]; gp = 0; for (size_t i = 0; i < n; ++i) gp += g[i] * p[i]; }
                    double t = 1.0, fn = fx; std::vector<double> xn(n); bool okLs = false;
                    for (int ls = 0; ls < 50; ++ls) { for (size_t i = 0; i < n; ++i) xn[i] = x[i] + t * p[i]; fn = Fx(xn); if (std::isfinite(fn) && fn <= fx + 1e-4 * t * gp) { okLs = true; break; } t *= 0.5; }
                    if (!okLs) break;
                    auto gn = Gx(xn); std::vector<double> sv(n), yv(n); double sy = 0, yy = 0, ss = 0;
                    for (size_t i = 0; i < n; ++i) { sv[i] = xn[i] - x[i]; yv[i] = gn[i] - g[i]; sy += sv[i] * yv[i]; yy += yv[i] * yv[i]; ss += sv[i] * sv[i]; }
                    const double fprev = fx; x = xn; g = gn; fx = fn;
                    if (sy > 1e-12 * std::sqrt(ss * yy)) {
                        if (first) { H = identity10(n); for (size_t i = 0; i < n; ++i) H(i, i) = sy / yy; first = false; }
                        const double rho = 1.0 / sy; Mat Hy(n, 1); for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) Hy(i, 0) += H(i, j) * yv[j];
                        double yHy = 0; for (size_t i = 0; i < n; ++i) yHy += yv[i] * Hy(i, 0);
                        for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) H(i, j) += (1.0 + rho * yHy) * rho * sv[i] * sv[j] - rho * (Hy(i, 0) * sv[j] + sv[i] * Hy(j, 0));
                    }
                    if (std::fabs(fprev - fx) <= 1e-16 * (1.0 + std::fabs(fx)) && std::sqrt(ss) <= xtol * 1e-3) { conv = true; break; }
                }
                put10(m, "x", vecVal(x)); put10(m, "fx", N10(fx));
                put10(m, "iterations", N10(static_cast<double>(std::min(it, maxIter)))); put10(m, "evaluations", N10(static_cast<double>(evals))); put10(m, "converged", B10(conv)); put10(m, "method", S10("bfgs"));
                return Value::makeMap(m);
            }
            std::vector<std::vector<double>> S(n + 1, x0); std::vector<double> fs(n + 1);
            for (size_t i = 0; i < n; ++i) { const double st = optNum10(o, "step", 0.0, "algoMinimize", line); S[i + 1][i] = x0[i] != 0.0 ? x0[i] * 1.05 : 0.00025; if (st > 0) S[i + 1][i] = x0[i] + st; }
            for (size_t i = 0; i <= n; ++i) fs[i] = F(S[i]);
            std::vector<size_t> ord(n + 1);
            for (it = 1; it <= maxIter; ++it) {
                std::iota(ord.begin(), ord.end(), 0); std::sort(ord.begin(), ord.end(), [&](size_t p, size_t q) { return fs[p] < fs[q]; });
                { auto S2 = S; auto f2 = fs; for (size_t i = 0; i <= n; ++i) { S[i] = S2[ord[i]]; fs[i] = f2[ord[i]]; } }
                double sz = 0; for (size_t i = 1; i <= n; ++i) for (size_t k = 0; k < n; ++k) sz = std::max(sz, std::fabs(S[i][k] - S[0][k]));
                if (sz <= xtol * (1.0 + norm2_10(S[0].data(), n)) && std::fabs(fs[n] - fs[0]) <= ftol * (1.0 + std::fabs(fs[0]))) { conv = true; break; }
                std::vector<double> cen(n, 0.0); for (size_t i = 0; i < n; ++i) for (size_t k = 0; k < n; ++k) cen[k] += S[i][k] / static_cast<double>(n);
                auto pt = [&](double t) { std::vector<double> p(n); for (size_t k = 0; k < n; ++k) p[k] = cen[k] + t * (S[n][k] - cen[k]); return p; };
                auto xr = pt(-1.0); double fr = F(xr);
                if (fr < fs[0]) { auto xe = pt(-2.0); double fe = F(xe); if (fe < fr) { S[n] = xe; fs[n] = fe; } else { S[n] = xr; fs[n] = fr; } }
                else if (fr < fs[n - 1]) { S[n] = xr; fs[n] = fr; }
                else {
                    const bool outside = fr < fs[n];
                    auto xc = pt(outside ? -0.5 : 0.5); double fc = F(xc);
                    if (fc < (outside ? fr : fs[n])) { S[n] = xc; fs[n] = fc; }
                    else { for (size_t i = 1; i <= n; ++i) { for (size_t k = 0; k < n; ++k) S[i][k] = S[0][k] + 0.5 * (S[i][k] - S[0][k]); fs[i] = F(S[i]); } }
                }
            }
            size_t best = 0; for (size_t i = 1; i <= n; ++i) if (fs[i] < fs[best]) best = i;
            put10(m, "x", vecVal(S[best])); put10(m, "fx", N10(fs[best]));
        }
        put10(m, "iterations", N10(static_cast<double>(it))); put10(m, "evaluations", N10(static_cast<double>(evals))); put10(m, "converged", B10(conv));
        return Value::makeMap(m);
    };

    // ======================================================== التفاضل والتكامل
    // algoDiff(f, x[, {order:1|2, h, info}]) — ريدرز (استكمال ريتشاردسون)
    natives["algoDiff"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoDiff", a, 2, 3, line);
        needFn11(a[0], "algoDiff", "f", line);
        CB f{this, a[0], "algoDiff", line};
        const double x = needNum10(a[1], "algoDiff", "x", line);
        const Value* o = optMap10(a, 2, "algoDiff", line);
        const int order = static_cast<int>(optNum10(o, "order", 1, "algoDiff", line));
        if (order != 1 && order != 2) failArg10("algoDiff", "'order' must be 1 or 2", line);
        double hh = optNum10(o, "h", 0.1 * std::max(1.0, std::fabs(x)), "algoDiff", line);
        if (!(hh > 0)) failArg10("algoDiff", "'h' must be > 0", line);
        const double CON = 1.4, CON2 = CON * CON, SAFE = 2.0; const int NTAB = 10;
        const double f0 = order == 2 ? f.at(x) : 0.0;
        auto D = [&](double h) { return order == 1 ? (f.at(x + h) - f.at(x - h)) / (2.0 * h) : (f.at(x + h) - 2.0 * f0 + f.at(x - h)) / (h * h); };
        double tab[NTAB + 1][NTAB + 1]; double err = 1e300, ans = 0;
        tab[1][1] = D(hh);
        for (int i = 2; i <= NTAB; ++i) {
            hh /= CON; tab[1][i] = D(hh); double fac = CON2;
            for (int j = 2; j <= i; ++j) {
                tab[j][i] = (tab[j - 1][i] * fac - tab[j - 1][i - 1]) / (fac - 1.0); fac *= CON2;
                const double errt = std::max(std::fabs(tab[j][i] - tab[j - 1][i]), std::fabs(tab[j][i] - tab[j - 1][i - 1]));
                if (errt <= err) { err = errt; ans = tab[j][i]; }
            }
            if (std::fabs(tab[i][i] - tab[i - 1][i - 1]) >= SAFE * err) break;
        }
        if (!optBool10(o, "info", false)) return N10(ans);
        auto m = std::make_shared<MapData>(); put10(m, "value", N10(ans)); put10(m, "error", N10(err));
        return Value::makeMap(m);
    };
    // algoGrad(f, x[, {h}]) — f: مصفوفة -> رقم
    natives["algoGrad"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoGrad", a, 2, 3, line);
        needFn11(a[0], "algoGrad", "f", line);
        CB f{this, a[0], "algoGrad", line};
        std::vector<double> x = readVec10(a[1], "algoGrad", "x", line, true);
        const Value* o = optMap10(a, 2, "algoGrad", line);
        std::vector<double> g(x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            const double h = optNum10(o, "h", std::cbrt(kEps10) * std::max(1.0, std::fabs(x[i])), "algoGrad", line);
            auto p = x, q = x; p[i] += h; q[i] -= h;
            g[i] = (f.num({vecVal(p)}, "f") - f.num({vecVal(q)}, "f")) / (2.0 * h);
        }
        return vecVal(g);
    };
    // algoJacobian(F, x[, {h}]) — F: مصفوفة -> مصفوفة (m) ؛ الناتج m×n
    natives["algoJacobian"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoJacobian", a, 2, 3, line);
        needFn11(a[0], "algoJacobian", "F", line);
        CB f{this, a[0], "algoJacobian", line};
        std::vector<double> x = readVec10(a[1], "algoJacobian", "x", line, true);
        const Value* o = optMap10(a, 2, "algoJacobian", line);
        const size_t n = x.size(); std::vector<std::vector<double>> cols(n); size_t m = 0;
        for (size_t j = 0; j < n; ++j) {
            const double h = optNum10(o, "h", std::cbrt(kEps10) * std::max(1.0, std::fabs(x[j])), "algoJacobian", line);
            auto p = x, q = x; p[j] += h; q[j] -= h;
            auto fp = f.vec(p, "F"), fq = f.vec(q, "F");
            if (fp.size() != fq.size() || fp.empty()) failRun10("algoJacobian", "F must return a non-empty vector of constant length", line);
            m = fp.size(); cols[j].resize(m);
            for (size_t i = 0; i < m; ++i) cols[j][i] = (fp[i] - fq[i]) / (2.0 * h);
        }
        Mat J(m, n); for (size_t j = 0; j < n; ++j) for (size_t i = 0; i < m; ++i) J(i, j) = cols[j][i];
        return matVal(J);
    };
    // algoNewton(F, x0[, {tol, maxIter, jacobian: fn(x) -> matrix}]) -> {x, fx, iterations, converged}
    natives["algoNewton"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoNewton", a, 2, 3, line);
        needFn11(a[0], "algoNewton", "F", line);
        CB f{this, a[0], "algoNewton", line};
        std::vector<double> x = readVec10(a[1], "algoNewton", "x0", line, true);
        const Value* o = optMap10(a, 2, "algoNewton", line);
        const double tol = optNum10(o, "tol", 1e-12, "algoNewton", line); const long maxIter = static_cast<long>(optNum10(o, "maxIter", 100, "algoNewton", line));
        const Value* jv = o ? get10(*o, "jacobian") : nullptr; const bool hasJ = jv && jv->type != Value::Type::NIL;
        if (hasJ) needFn11(*jv, "algoNewton", "jacobian", line);
        CB jf{this, hasJ ? *jv : Value(), "algoNewton", line};
        const size_t n = x.size();
        auto Fv = [&](const std::vector<double>& p) { auto r = f.vec(p, "F"); if (r.size() != n) failRun10("algoNewton", "F must return " + std::to_string(n) + " components (one per unknown) but returned " + std::to_string(r.size()), line); return r; };
        auto fx = Fv(x); double fn0 = norm2_10(fx.data(), n); long iters = 0; bool conv = fn0 <= tol;
        while (!conv && iters < maxIter) {
            ++iters;
            Mat J(n, n);
            if (hasJ) { Value jm = jf.call({vecVal(x)}); J = readMat10(jm, "algoNewton", "jacobian(x)", line, true); if (J.r != n || J.c != n) failRun10("algoNewton", "jacobian must return an " + std::to_string(n) + "x" + std::to_string(n) + " matrix", line); }
            else for (size_t j = 0; j < n; ++j) { const double h = std::cbrt(kEps10) * std::max(1.0, std::fabs(x[j])); auto p = x, q = x; p[j] += h; q[j] -= h; auto fp = Fv(p), fq = Fv(q); for (size_t i = 0; i < n; ++i) J(i, j) = (fp[i] - fq[i]) / (2.0 * h); }
            LU10 lu = luDecompose10(J);
            if (lu.singular) failRun10("algoNewton", "Jacobian is singular at iteration " + std::to_string(iters) + " — try another starting point", line);
            Mat rhs(n, 1); for (size_t i = 0; i < n; ++i) rhs(i, 0) = -fx[i];
            Mat dx = luSolve10(lu, rhs);
            double t = 1.0; std::vector<double> xn(n), fn1; double nn = fn0;
            for (int ls = 0; ls < 30; ++ls) { for (size_t i = 0; i < n; ++i) xn[i] = x[i] + t * dx(i, 0); fn1 = Fv(xn); nn = norm2_10(fn1.data(), n); if (std::isfinite(nn) && nn < fn0 * (1.0 - 1e-4 * t) + 1e-300) break; t *= 0.5; }
            const double step = t * norm2_10(dx.d.data(), n);
            x = xn; fx = fn1; fn0 = nn;
            if (fn0 <= tol) { conv = true; break; }
            if (step <= tol * (1.0 + norm2_10(x.data(), n))) { conv = fn0 <= 1e-8; break; }
        }
        auto m = std::make_shared<MapData>();
        put10(m, "x", vecVal(x)); put10(m, "fx", N10(fn0)); put10(m, "iterations", N10(static_cast<double>(iters))); put10(m, "converged", B10(conv));
        return Value::makeMap(m);
    };
    // algoIntegrate(f, a, b[, {tol=1e-10, limit=500}]) -> {value, error, evaluations, converged}  (Gauss–Kronrod 7/15 تكيّفي؛ يدعم حدوداً لا نهائية)
    natives["algoIntegrate"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoIntegrate", a, 3, 4, line);
        needFn11(a[0], "algoIntegrate", "f", line);
        CB f{this, a[0], "algoIntegrate", line};
        double lo = needNum10(a[1], "algoIntegrate", "a", line), hi = needNum10(a[2], "algoIntegrate", "b", line);
        const Value* o = optMap10(a, 3, "algoIntegrate", line);
        const double tol = optNum10(o, "tol", 1e-10, "algoIntegrate", line); const size_t limit = static_cast<size_t>(optNum10(o, "limit", 500, "algoIntegrate", line));
        if (std::isnan(lo) || std::isnan(hi)) failArg10("algoIntegrate", "limits must not be NaN", line);
        double sign = 1.0; if (lo > hi) { std::swap(lo, hi); sign = -1.0; }
        long evals = 0;
        // تحويل الحدود اللانهائية إلى t في (0,1) أو (-1,1)
        std::function<double(double)> g;
        double A = lo, B = hi;
        if (std::isinf(lo) && std::isinf(hi)) { A = -1; B = 1; g = [&](double t) { const double d = 1.0 - t * t; const double x = t / d; return f.at(x) * (1.0 + t * t) / (d * d); }; }
        else if (std::isinf(hi)) { A = 0; B = 1; const double L = lo; g = [&, L](double t) { const double d = 1.0 - t; return f.at(L + t / d) / (d * d); }; }
        else if (std::isinf(lo)) { A = 0; B = 1; const double H = hi; g = [&, H](double t) { return f.at(H - (1.0 - t) / t) / (t * t); }; }
        else g = [&](double x) { return f.at(x); };
        auto rule = [&](double p, double q, double& err) {
            const double c = 0.5 * (p + q), h = 0.5 * (q - p);
            double fc = g(c); evals += 15;
            double rk = fc * WGK[7], rg = fc * WG[3];
            for (int j = 0; j < 7; ++j) {
                const double dx = h * XGK[j]; const double f1 = g(c - dx), f2 = g(c + dx); const double s = f1 + f2;
                rk += WGK[j] * s; if (j % 2 == 1) rg += WG[j / 2] * s;
            }
            rk *= h; rg *= h; err = std::fabs(rk - rg); return rk;
        };
        std::vector<Seg11> segs; { double e; double v = rule(A, B, e); segs.push_back({A, B, v, e}); }
        double total = segs[0].val, terr = segs[0].err; bool conv = false;
        for (;;) {
            total = 0; terr = 0; for (auto& s : segs) { total += s.val; terr += s.err; }
            if (!std::isfinite(total)) failRun10("algoIntegrate", "integrand is not finite on the interval (singularity?)", line);
            if (terr <= std::max(tol, tol * std::fabs(total))) { conv = true; break; }
            if (segs.size() >= limit) break;
            size_t w = 0; for (size_t i = 1; i < segs.size(); ++i) if (segs[i].err > segs[w].err) w = i;
            const Seg11 s = segs[w]; const double mid = 0.5 * (s.a + s.b);
            if (!(mid > s.a && mid < s.b)) break;
            double e1, e2; const double v1 = rule(s.a, mid, e1), v2 = rule(mid, s.b, e2);
            segs[w] = {s.a, mid, v1, e1}; segs.push_back({mid, s.b, v2, e2});
        }
        auto m = std::make_shared<MapData>();
        put10(m, "value", N10(sign * total)); put10(m, "error", N10(terr)); put10(m, "evaluations", N10(static_cast<double>(evals))); put10(m, "converged", B10(conv));
        put10(m, "intervals", N10(static_cast<double>(segs.size())));
        return Value::makeMap(m);
    };
    // algoTrapz(ys[, xs | dx=1]) — تكامل بيانات مُعاينة بالمنحرف
    natives["algoTrapz"] = [](Args& a, int line) -> Value {
        expectArgsRange("algoTrapz", a, 1, 2, line);
        auto y = readVec10(a[0], "algoTrapz", "ys", line, true);
        std::vector<double> terms;
        if (a.size() == 2 && a[1].type == Value::Type::ARRAY) {
            auto x = readVec10(a[1], "algoTrapz", "xs", line, true);
            if (x.size() != y.size()) failArg10("algoTrapz", "xs and ys must have the same length", line);
            for (size_t i = 1; i < y.size(); ++i) terms.push_back(0.5 * (y[i] + y[i - 1]) * (x[i] - x[i - 1]));
        } else {
            const double dx = a.size() == 2 ? needNum10(a[1], "algoTrapz", "dx", line) : 1.0;
            for (size_t i = 1; i < y.size(); ++i) terms.push_back(0.5 * (y[i] + y[i - 1]) * dx);
        }
        return N10(neumaier10(terms));
    };

    // ======================================================== المعادلات التفاضلية
    // algoOde(f, y0, t0, t1[, {method:"rk45"|"rk4", rtol, atol, h, steps, maxSteps}]) -> {t, y, steps, rejected, evaluations}
    // f(t, y) تعيد مشتقّة الحالة؛ y رقم أو متجه (والمُخرجات بنفس الشكل)
    natives["algoOde"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoOde", a, 4, 5, line);
        needFn11(a[0], "algoOde", "f", line);
        CB f{this, a[0], "algoOde", line};
        const bool scalar = a[1].type == Value::Type::NUMBER;
        std::vector<double> y = scalar ? std::vector<double>{a[1].number} : readVec10(a[1], "algoOde", "y0", line, true);
        const double t0 = needNum10(a[2], "algoOde", "t0", line), t1 = needNum10(a[3], "algoOde", "t1", line);
        if (!std::isfinite(t0) || !std::isfinite(t1)) failArg10("algoOde", "t0 and t1 must be finite", line);
        const Value* o = optMap10(a, 4, "algoOde", line);
        std::string method = optStr10(o, "method", "rk45");
        const size_t n = y.size(); const double dir = t1 >= t0 ? 1.0 : -1.0, span = std::fabs(t1 - t0);
        const long maxSteps = static_cast<long>(optNum10(o, "maxSteps", 200000, "algoOde", line));
        long evals = 0, rejected = 0, steps = 0;
        auto rhs = [&](double t, const std::vector<double>& s) {
            ++evals; Value r = f.call({N10(t), scalar ? N10(s[0]) : vecVal(s)});
            std::vector<double> d;
            if (scalar) { if (r.type != Value::Type::NUMBER) failType10("algoOde", "f(t, y) must return a number for a scalar state but returned " + r.typeName(), line); d = {r.number}; }
            else d = readVec10(r, "algoOde", "f(t, y)", line);
            if (d.size() != n) failRun10("algoOde", "f(t, y) returned " + std::to_string(d.size()) + " components but the state has " + std::to_string(n), line);
            for (double v : d) if (!std::isfinite(v)) failRun10("algoOde", "f(t, y) is not finite at t=" + fmt11(t), line);
            return d;
        };
        std::vector<double> ts{t0}; std::vector<std::vector<double>> ys{y};
        if (span == 0.0) { /* مجرد الحالة الابتدائية */ }
        else if (method == "rk4") {
            long N = static_cast<long>(optNum10(o, "steps", 100, "algoOde", line));
            if (o && get10(*o, "h")) { const double h = optNum10(o, "h", 0, "algoOde", line); if (!(h > 0)) failArg10("algoOde", "'h' must be > 0", line); N = static_cast<long>(std::ceil(span / h - 1e-12)); }
            if (N < 1 || N > maxSteps) failArg10("algoOde", "number of steps must be in 1.." + std::to_string(maxSteps), line);
            const double h = dir * span / static_cast<double>(N); double t = t0;
            for (long i = 0; i < N; ++i) {
                auto k1 = rhs(t, y); std::vector<double> tmp(n);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + 0.5 * h * k1[j]; auto k2 = rhs(t + 0.5 * h, tmp);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + 0.5 * h * k2[j]; auto k3 = rhs(t + 0.5 * h, tmp);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + h * k3[j]; auto k4 = rhs(t + h, tmp);
                for (size_t j = 0; j < n; ++j) y[j] += h / 6.0 * (k1[j] + 2 * k2[j] + 2 * k3[j] + k4[j]);
                t = (i == N - 1) ? t1 : t0 + h * static_cast<double>(i + 1); ++steps; ts.push_back(t); ys.push_back(y);
            }
        } else if (method == "rk45") {
            const double rtol = optNum10(o, "rtol", 1e-8, "algoOde", line), atol = optNum10(o, "atol", 1e-10, "algoOde", line);
            double h = optNum10(o, "h", span * 1e-3, "algoOde", line); if (!(h > 0)) failArg10("algoOde", "'h' must be > 0", line);
            const double hmax = optNum10(o, "hmax", span, "algoOde", line);
            static const double c2 = 1.0 / 5, c3 = 3.0 / 10, c4 = 4.0 / 5, c5 = 8.0 / 9;
            static const double a21 = 1.0 / 5, a31 = 3.0 / 40, a32 = 9.0 / 40, a41 = 44.0 / 45, a42 = -56.0 / 15, a43 = 32.0 / 9,
                a51 = 19372.0 / 6561, a52 = -25360.0 / 2187, a53 = 64448.0 / 6561, a54 = -212.0 / 729,
                a61 = 9017.0 / 3168, a62 = -355.0 / 33, a63 = 46732.0 / 5247, a64 = 49.0 / 176, a65 = -5103.0 / 18656,
                b1 = 35.0 / 384, b3 = 500.0 / 1113, b4 = 125.0 / 192, b5 = -2187.0 / 6784, b6 = 11.0 / 84,
                e1 = 71.0 / 57600, e3 = -71.0 / 16695, e4 = 71.0 / 1920, e5 = -17253.0 / 339200, e6 = 22.0 / 525, e7 = -1.0 / 40;
            double t = t0; auto k1 = rhs(t, y);
            std::vector<double> tmp(n), yn(n), k2, k3, k4, k5, k6, k7;
            while ((t1 - t) * dir > 1e-15 * std::max(1.0, std::fabs(t1))) {
                if (steps + rejected > maxSteps) failRun10("algoOde", "exceeded maxSteps=" + std::to_string(maxSteps) + " at t=" + fmt11(t) + " (stiff problem? loosen rtol/atol or raise maxSteps)", line);
                h = std::min({h, hmax, std::fabs(t1 - t)}); const double hs = dir * h;
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + hs * a21 * k1[j]; k2 = rhs(t + c2 * hs, tmp);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + hs * (a31 * k1[j] + a32 * k2[j]); k3 = rhs(t + c3 * hs, tmp);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + hs * (a41 * k1[j] + a42 * k2[j] + a43 * k3[j]); k4 = rhs(t + c4 * hs, tmp);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + hs * (a51 * k1[j] + a52 * k2[j] + a53 * k3[j] + a54 * k4[j]); k5 = rhs(t + c5 * hs, tmp);
                for (size_t j = 0; j < n; ++j) tmp[j] = y[j] + hs * (a61 * k1[j] + a62 * k2[j] + a63 * k3[j] + a64 * k4[j] + a65 * k5[j]); k6 = rhs(t + hs, tmp);
                for (size_t j = 0; j < n; ++j) yn[j] = y[j] + hs * (b1 * k1[j] + b3 * k3[j] + b4 * k4[j] + b5 * k5[j] + b6 * k6[j]);
                k7 = rhs(t + hs, yn);
                double err = 0;
                for (size_t j = 0; j < n; ++j) {
                    const double ej = hs * (e1 * k1[j] + e3 * k3[j] + e4 * k4[j] + e5 * k5[j] + e6 * k6[j] + e7 * k7[j]);
                    err = std::max(err, std::fabs(ej) / (atol + rtol * std::max(std::fabs(y[j]), std::fabs(yn[j]))));
                }
                if (err <= 1.0) {
                    t = (std::fabs(t1 - (t + hs)) <= 1e-14 * std::max(1.0, std::fabs(t1))) ? t1 : t + hs; y = yn; k1 = k7; ++steps; ts.push_back(t); ys.push_back(y);
                } else ++rejected;
                const double fac = err == 0.0 ? 5.0 : std::min(5.0, std::max(0.2, 0.9 * std::pow(err, -0.2)));
                h *= fac;
                if (h < 1e-14 * std::max(1.0, std::fabs(t))) failRun10("algoOde", "step size underflow at t=" + fmt11(t), line);
            }
        } else if (method == "stiff") { // Euler الضمني التكيّفي بمضاعفة الخطوة + استكمال محلي (رتبة 2)؛ للمسائل الصلبة
            const double rtol = optNum10(o, "rtol", 1e-6, "algoOde", line), atol = optNum10(o, "atol", 1e-9, "algoOde", line);
            double h = optNum10(o, "h", span * 1e-4, "algoOde", line); if (!(h > 0)) failArg10("algoOde", "'h' must be > 0", line);
            const double hmax = optNum10(o, "hmax", span, "algoOde", line);
            auto implicitEuler = [&](double tt, const std::vector<double>& y0, double hh, std::vector<double>& out) -> bool {
                auto f0 = rhs(tt, y0); std::vector<double> yk(n);
                for (size_t j = 0; j < n; ++j) yk[j] = y0[j] + hh * f0[j];
                auto fg = rhs(tt + hh, yk); Mat Mx = identity10(n);
                for (size_t j = 0; j < n; ++j) {
                    const double hj = std::sqrt(kEps10) * std::max(1.0, std::fabs(yk[j])); auto sp = yk; sp[j] += hj; auto fp = rhs(tt + hh, sp);
                    for (size_t i = 0; i < n; ++i) Mx(i, j) -= hh * (fp[i] - fg[i]) / hj;
                }
                LU10 lu = luDecompose10(Mx); if (lu.singular) return false;
                for (int k = 0; k < 25; ++k) {
                    auto fk = rhs(tt + hh, yk); Mat g(n, 1); for (size_t j = 0; j < n; ++j) g(j, 0) = -(yk[j] - y0[j] - hh * fk[j]);
                    Mat d = luSolve10(lu, g); double dn = 0, yn = 0;
                    for (size_t j = 0; j < n; ++j) { yk[j] += d(j, 0); dn = std::max(dn, std::fabs(d(j, 0))); yn = std::max(yn, std::fabs(yk[j])); }
                    if (!std::isfinite(dn)) return false;
                    if (dn <= 1e-11 * (1.0 + yn)) { out = yk; return true; }
                }
                return false;
            };
            double t = t0;
            while ((t1 - t) * dir > 1e-15 * std::max(1.0, std::fabs(t1))) {
                if (steps + rejected > maxSteps) failRun10("algoOde", "exceeded maxSteps=" + std::to_string(maxSteps) + " at t=" + fmt11(t), line);
                h = std::min({h, hmax, std::fabs(t1 - t)}); const double hs = dir * h;
                std::vector<double> yF, yM, yH;
                if (!implicitEuler(t, y, hs, yF) || !implicitEuler(t, y, 0.5 * hs, yM) || !implicitEuler(t + 0.5 * hs, yM, 0.5 * hs, yH)) { ++rejected; h *= 0.25; if (h < 1e-14 * std::max(1.0, std::fabs(t))) failRun10("algoOde", "step size underflow at t=" + fmt11(t), line); continue; }
                double err = 0; for (size_t j = 0; j < n; ++j) err = std::max(err, std::fabs(yH[j] - yF[j]) / (atol + rtol * std::max(std::fabs(y[j]), std::fabs(yH[j]))));
                if (err <= 1.0) {
                    for (size_t j = 0; j < n; ++j) y[j] = 2.0 * yH[j] - yF[j];
                    t = (std::fabs(t1 - (t + hs)) <= 1e-14 * std::max(1.0, std::fabs(t1))) ? t1 : t + hs; ++steps; ts.push_back(t); ys.push_back(y);
                } else ++rejected;
                h *= err == 0.0 ? 5.0 : std::min(5.0, std::max(0.2, 0.9 * std::pow(err, -0.5)));
                if (h < 1e-14 * std::max(1.0, std::fabs(t))) failRun10("algoOde", "step size underflow at t=" + fmt11(t), line);
            }
        } else failArg10("algoOde", "unknown method '" + method + "' (rk45, rk4, stiff)", line);
        auto tv = vecVal(ts); auto yv = std::make_shared<ArrayData>();
        for (auto& s : ys) yv->push_back(scalar ? N10(s[0]) : vecVal(s));
        auto m = std::make_shared<MapData>();
        put10(m, "t", tv); put10(m, "y", Value::makeArray(yv)); put10(m, "steps", N10(static_cast<double>(steps)));
        put10(m, "rejected", N10(static_cast<double>(rejected))); put10(m, "evaluations", N10(static_cast<double>(evals))); put10(m, "method", S10(method));
        return Value::makeMap(m);
    };

    // ======================================================== الاستيفاء والتوفيق
    // algoInterp(xs, ys, x[, {method:"linear"|"spline"|"nearest", extrapolate}]) — x رقم أو متجه
    natives["algoInterp"] = [](Args& a, int line) -> Value {
        expectArgsRange("algoInterp", a, 3, 4, line);
        auto xs = readVec10(a[0], "algoInterp", "xs", line, true), ys = readVec10(a[1], "algoInterp", "ys", line, true);
        if (xs.size() != ys.size()) failArg10("algoInterp", "xs and ys must have the same length", line);
        if (xs.size() < 2) failArg10("algoInterp", "need at least 2 points", line);
        for (size_t i = 1; i < xs.size(); ++i) if (!(xs[i] > xs[i - 1])) failArg10("algoInterp", "xs must be strictly increasing (problem at index " + std::to_string(i) + ")", line);
        const Value* o = optMap10(a, 3, "algoInterp", line);
        const std::string method = optStr10(o, "method", "linear"); const bool extra = optBool10(o, "extrapolate", false);
        const size_t n = xs.size();
        std::vector<double> M(n, 0.0); // المشتقات الثانية للـ spline الطبيعي
        if (method == "spline") {
            if (n > 2) { // نظام ثلاثي القطر للمشتقات الثانية الداخلية (الأطراف = 0): خوارزمية توماس
                std::vector<double> cp(n, 0.0), dp(n, 0.0);
                for (size_t i = 1; i + 1 < n; ++i) {
                    const double h0 = xs[i] - xs[i - 1], h1 = xs[i + 1] - xs[i];
                    const double bi = 2.0 * (h0 + h1), ri = 6.0 * ((ys[i + 1] - ys[i]) / h1 - (ys[i] - ys[i - 1]) / h0);
                    const double den = bi - (i > 1 ? h0 * cp[i - 1] : 0.0);
                    cp[i] = h1 / den; dp[i] = (ri - (i > 1 ? h0 * dp[i - 1] : 0.0)) / den;
                }
                for (size_t i = n - 1; i-- > 1;) M[i] = dp[i] - cp[i] * M[i + 1];
            }
        } else if (method != "linear" && method != "nearest") failArg10("algoInterp", "unknown method '" + method + "' (linear, spline, nearest)", line);
        auto eval = [&](double x) {
            if (std::isnan(x)) return x;
            if (!extra && (x < xs.front() - 1e-12 * std::fabs(xs.front()) || x > xs.back() + 1e-12 * std::fabs(xs.back())))
                failRun10("algoInterp", "x=" + fmt11(x) + " is outside [" + fmt11(xs.front()) + ", " + fmt11(xs.back()) + "] (pass {extrapolate:true} to allow it)", line);
            size_t i = static_cast<size_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin());
            i = i == 0 ? 0 : (i >= n ? n - 2 : i - 1);
            const double h = xs[i + 1] - xs[i], t = (x - xs[i]) / h;
            if (method == "nearest") return (t < 0.5 ? ys[i] : ys[i + 1]);
            if (method == "linear") return ys[i] + t * (ys[i + 1] - ys[i]);
            const double A = 1.0 - t, B = t;
            return A * ys[i] + B * ys[i + 1] + ((A * A * A - A) * M[i] + (B * B * B - B) * M[i + 1]) * h * h / 6.0;
        };
        if (a[2].type == Value::Type::NUMBER) return N10(eval(a[2].number));
        auto q = readVec10(a[2], "algoInterp", "x", line, false, true);
        for (double& v : q) v = eval(v);
        return vecVal(q);
    };
    // algoPolyFit(xs, ys, deg) -> {coeffs (من الدرجة 0 صعوداً), r2, rmse, rank}
    natives["algoPolyFit"] = [](Args& a, int line) -> Value {
        expectArgs("algoPolyFit", a, 3, line);
        auto xs = readVec10(a[0], "algoPolyFit", "xs", line, true), ys = readVec10(a[1], "algoPolyFit", "ys", line, true);
        if (xs.size() != ys.size()) failArg10("algoPolyFit", "xs and ys must have the same length", line);
        const size_t deg = needDim10(a[2], "algoPolyFit", "deg", line, 0);
        if (xs.size() < deg + 1) failArg10("algoPolyFit", "need at least " + std::to_string(deg + 1) + " points for degree " + std::to_string(deg), line);
        // توسيط وتحجيم x لتحسين الشرطية ثم إعادة التعبير بالمتغيّر الأصلي
        double mean = 0, sc = 0; { std::vector<double> t(xs); mean = neumaier10(t) / static_cast<double>(xs.size()); for (double v : xs) sc = std::max(sc, std::fabs(v - mean)); if (sc == 0) sc = 1; }
        Mat V(xs.size(), deg + 1);
        for (size_t i = 0; i < xs.size(); ++i) { const double u = (xs[i] - mean) / sc; double p = 1; for (size_t j = 0; j <= deg; ++j) { V(i, j) = p; p *= u; } }
        SVD10 f = svd10(V); const double tol = svdTol10(f, V.r, V.c, 0);
        Mat bm(ys.size(), 1); for (size_t i = 0; i < ys.size(); ++i) bm(i, 0) = ys[i];
        Mat cu = mul10(pinvFromSvd10(f, V.r, V.c, tol), bm);
        std::vector<double> c(deg + 1, 0.0); // معاملات u = (x-mean)/sc  -> معاملات x بالتوسيع ذي الحدّين
        std::vector<std::vector<double>> binom(deg + 1, std::vector<double>(deg + 1, 0.0));
        for (size_t i = 0; i <= deg; ++i) { binom[i][0] = 1; for (size_t j = 1; j <= i; ++j) binom[i][j] = binom[i - 1][j - 1] + (j <= i - 1 ? binom[i - 1][j] : 0.0); }
        for (size_t k = 0; k <= deg; ++k) { const double ck = cu(k, 0) / std::pow(sc, static_cast<double>(k)); for (size_t j = 0; j <= k; ++j) c[j] += ck * binom[k][j] * std::pow(-mean, static_cast<double>(k - j)); }
        double sse = 0, sst = 0, ym = 0; { std::vector<double> t(ys); ym = neumaier10(t) / static_cast<double>(ys.size()); }
        for (size_t i = 0; i < xs.size(); ++i) { double p = 0; for (size_t j = deg + 1; j-- > 0;) p = p * xs[i] + c[j]; sse += (ys[i] - p) * (ys[i] - p); sst += (ys[i] - ym) * (ys[i] - ym); }
        auto m = std::make_shared<MapData>();
        put10(m, "coeffs", vecVal(c)); put10(m, "r2", N10(sst == 0.0 ? (sse == 0.0 ? 1.0 : 0.0) : 1.0 - sse / sst));
        put10(m, "rmse", N10(std::sqrt(sse / static_cast<double>(xs.size())))); put10(m, "rank", N10(static_cast<double>(rankFromSvd10(f, tol))));
        return Value::makeMap(m);
    };
    // algoPolyEval(coeffs, x) — Horner؛ x رقم أو متجه
    natives["algoPolyEval"] = [](Args& a, int line) -> Value {
        expectArgs("algoPolyEval", a, 2, line);
        auto c = readVec10(a[0], "algoPolyEval", "coeffs", line);
        auto ev = [&](double x) { double p = 0; for (size_t j = c.size(); j-- > 0;) p = p * x + c[j]; return p; };
        if (a[1].type == Value::Type::NUMBER) return N10(ev(a[1].number));
        auto q = readVec10(a[1], "algoPolyEval", "x", line, false, true);
        for (double& v : q) v = ev(v);
        return vecVal(q);
    };
    // algoPolyRoots(coeffs) -> [{re, im}] — الجذور عبر القيم الذاتية للمصفوفة المرافقة (coeffs من الدرجة 0 صعوداً)
    natives["algoPolyRoots"] = [](Args& a, int line) -> Value {
        expectArgs("algoPolyRoots", a, 1, line);
        auto c = readVec10(a[0], "algoPolyRoots", "coeffs", line, true);
        while (!c.empty() && c.back() == 0.0) c.pop_back();
        if (c.size() < 2) failArg10("algoPolyRoots", "the polynomial must have degree >= 1 (non-zero leading coefficient)", line);
        size_t zeros = 0; while (zeros + 1 < c.size() && c[zeros] == 0.0) ++zeros;
        std::vector<Cx10> roots; for (size_t i = 0; i < zeros; ++i) roots.push_back({0.0, 0.0});
        std::vector<double> d(c.begin() + static_cast<long>(zeros), c.end());
        const size_t n = d.size() - 1;
        if (n >= 1) {
            if (n == 1) roots.push_back({-d[0] / d[1], 0.0});
            else {
                Mat C(n, n);
                for (size_t i = 1; i < n; ++i) C(i, i - 1) = 1.0;
                for (size_t i = 0; i < n; ++i) C(i, n - 1) = -d[i] / d[n];
                std::vector<Cx10> ev; if (!eigvals10(C, ev)) failRun10("algoPolyRoots", "root finding did not converge", line);
                for (auto& e : ev) roots.push_back(e);
            }
        }
        std::stable_sort(roots.begin(), roots.end(), [](const Cx10& p, const Cx10& q) { if (std::fabs(p.re - q.re) > 1e-9 * std::max(1.0, std::fabs(p.re))) return p.re < q.re; return p.im < q.im; });
        auto arr = std::make_shared<ArrayData>();
        for (auto& r : roots) { auto m = std::make_shared<MapData>(); put10(m, "re", N10(r.re)); put10(m, "im", N10(std::fabs(r.im) <= 1e-12 * std::max(1.0, std::fabs(r.re)) ? 0.0 : r.im)); arr->push_back(Value::makeMap(m)); }
        return Value::makeArray(arr);
    };

    // ======================================================== الإشارات
    // algoFFT(signal[, {inverse:false}]) -> {re, im}  — signal: مصفوفة أرقام | {re, im} | مصفوفة {re, im}؛ أي طول (Bluestein)
    natives["algoFFT"] = [](Args& a, int line) -> Value {
        expectArgsRange("algoFFT", a, 1, 2, line);
        const Value* o = optMap10(a, 1, "algoFFT", line);
        std::vector<Cd> x;
        if (a[0].type == Value::Type::MAP) {
            const Value* re = get10(a[0], "re"); const Value* im = get10(a[0], "im");
            if (!re) failArg10("algoFFT", "a map signal needs a 're' array (and optional 'im')", line);
            auto r = readVec10(*re, "algoFFT", "re", line, true); std::vector<double> i2(r.size(), 0.0);
            if (im) { i2 = readVec10(*im, "algoFFT", "im", line, true); if (i2.size() != r.size()) failArg10("algoFFT", "'re' and 'im' must have the same length", line); }
            for (size_t k = 0; k < r.size(); ++k) x.emplace_back(r[k], i2[k]);
        } else if (a[0].type == Value::Type::ARRAY && a[0].array && !a[0].array->empty() && (*a[0].array)[0].type == Value::Type::MAP) {
            for (size_t k = 0; k < a[0].array->size(); ++k) {
                const Value& e = (*a[0].array)[k]; const Value* re = get10(e, "re"); const Value* im = get10(e, "im");
                if (!re || re->type != Value::Type::NUMBER) failArg10("algoFFT", "element " + std::to_string(k) + " must be {re, im}", line);
                x.emplace_back(re->number, im && im->type == Value::Type::NUMBER ? im->number : 0.0);
            }
        } else { auto r = readVec10(a[0], "algoFFT", "signal", line, true); for (double v : r) x.emplace_back(v, 0.0); }
        if (x.size() > 16000000) failArg10("algoFFT", "signal too long", line);
        const bool inv = optBool10(o, "inverse", false);
        fftAny11(x, inv);
        std::vector<double> re(x.size()), im(x.size());
        const double s = inv ? 1.0 / static_cast<double>(x.size()) : 1.0;
        for (size_t k = 0; k < x.size(); ++k) { re[k] = x[k].real() * s; im[k] = x[k].imag() * s; }
        auto m = std::make_shared<MapData>(); put10(m, "re", vecVal(re)); put10(m, "im", vecVal(im));
        return Value::makeMap(m);
    };
    // algoRandom(n[, {seed, min, max, int, sorted, unique}]) — مصفوفة عشوائية حتمية عند seed
    natives["algoRandom"] = [](Args& a, int line) -> Value {
        expectArgsRange("algoRandom", a, 1, 2, line);
        const size_t n = needDim10(a[0], "algoRandom", "n", line, 0);
        if (n > 50000000) failArg10("algoRandom", "n too large", line);
        const Value* o = optMap10(a, 1, "algoRandom", line);
        const bool asInt = optBool10(o, "int", false), uniq = optBool10(o, "unique", false), sorted = optBool10(o, "sorted", false);
        const double lo = optNum10(o, "min", 0.0, "algoRandom", line), hi = optNum10(o, "max", asInt ? 100.0 : 1.0, "algoRandom", line);
        if (hi < lo) failArg10("algoRandom", "'max' must be >= 'min'", line);
        std::mt19937_64 g(seedFrom11(o, "algoRandom", line));
        auto uni = [&]() { return static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0); };
        std::vector<double> v;
        if (asInt) {
            const double L = std::ceil(lo), H = std::floor(hi), span = H - L + 1.0;
            if (span < 1) failArg10("algoRandom", "no integer in [min, max]", line);
            if (uniq) {
                if (static_cast<double>(n) > span) failArg10("algoRandom", "cannot draw " + std::to_string(n) + " unique integers from a range of " + fmt11(span), line);
                std::vector<double> pool; if (span <= 5e7) { pool.resize(static_cast<size_t>(span)); std::iota(pool.begin(), pool.end(), L); for (size_t i = 0; i < n; ++i) { const size_t j = i + static_cast<size_t>(uni() * static_cast<double>(pool.size() - i)); std::swap(pool[i], pool[std::min(j, pool.size() - 1)]); v.push_back(pool[i]); } }
                else failArg10("algoRandom", "range too large for unique sampling", line);
            } else for (size_t i = 0; i < n; ++i) v.push_back(L + std::floor(uni() * span));
        } else {
            if (uniq) failArg10("algoRandom", "'unique' needs {int:true}", line);
            for (size_t i = 0; i < n; ++i) v.push_back(lo + uni() * (hi - lo));
        }
        if (sorted) std::sort(v.begin(), v.end());
        return vecVal(v);
    };

    // ======================================================== القياس والتحقق
    using Clock = std::chrono::steady_clock;
    // يقيس n تشغيلاً (ms)
    struct Timing { std::vector<double> ms; };
    auto benchRuns = [this](const Value& fn, const std::vector<Value>& args, long runs, long warmup, int line) {
        Timing t; Value out;
        for (long i = 0; i < warmup; ++i) { std::vector<Value> aa = args; callValue(fn, aa, line); }
        for (long i = 0; i < runs; ++i) {
            std::vector<Value> aa = args; const auto t0 = Clock::now(); out = callValue(fn, aa, line); const auto t1 = Clock::now();
            t.ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        return t;
    };
    auto summarize = [](const Timing& t) {
        auto m = std::make_shared<MapData>(); std::vector<double> s = t.ms; std::sort(s.begin(), s.end());
        const double n = static_cast<double>(s.size()); std::vector<double> tmp(s); const double tot = neumaier10(tmp), mean = tot / n;
        double var = 0; for (double x : s) var += (x - mean) * (x - mean); var = s.size() > 1 ? var / (n - 1) : 0;
        const double med = s.size() % 2 ? s[s.size() / 2] : 0.5 * (s[s.size() / 2 - 1] + s[s.size() / 2]);
        put10(m, "runs", N10(n)); put10(m, "meanMs", N10(mean)); put10(m, "minMs", N10(s.front())); put10(m, "maxMs", N10(s.back()));
        put10(m, "medianMs", N10(med)); put10(m, "stdevMs", N10(std::sqrt(var))); put10(m, "totalMs", N10(tot));
        return m;
    };
    // algoBench(fn[, {runs=10, warmup=1, args:[...]}]) -> {runs, meanMs, minMs, maxMs, medianMs, stdevMs, totalMs}
    natives["algoBench"] = [benchRuns, summarize](Args& a, int line) -> Value {
        expectArgsRange("algoBench", a, 1, 2, line);
        needFn11(a[0], "algoBench", "fn", line);
        const Value* o = optMap10(a, 1, "algoBench", line);
        const long runs = static_cast<long>(optNum10(o, "runs", 10, "algoBench", line)), warm = static_cast<long>(optNum10(o, "warmup", 1, "algoBench", line));
        if (runs < 1 || runs > 1000000 || warm < 0 || warm > 1000000) failArg10("algoBench", "'runs' must be 1..1000000 and 'warmup' >= 0", line);
        std::vector<Value> args; if (o) if (const Value* av = get10(*o, "args")) { if (av->type != Value::Type::ARRAY || !av->array) failType10("algoBench", "'args' must be an array", line); args = *av->array; }
        return Value::makeMap(summarize(benchRuns(a[0], args, runs, warm, line)));
    };
    // algoCompare({name: fn, ...}[, {runs, warmup, args}]) -> مصفوفة مرتّبة من الأسرع: {name, meanMs, minMs, medianMs, ratio}
    natives["algoCompare"] = [benchRuns, summarize](Args& a, int line) -> Value {
        expectArgsRange("algoCompare", a, 1, 2, line);
        if (a[0].type != Value::Type::MAP || !a[0].map || a[0].map->size() < 2) failType10("algoCompare", "expects a map {name: fn, ...} with at least 2 functions", line);
        const Value* o = optMap10(a, 1, "algoCompare", line);
        const long runs = static_cast<long>(optNum10(o, "runs", 10, "algoCompare", line)), warm = static_cast<long>(optNum10(o, "warmup", 1, "algoCompare", line));
        if (runs < 1 || runs > 1000000 || warm < 0) failArg10("algoCompare", "'runs' must be 1..1000000 and 'warmup' >= 0", line);
        std::vector<Value> args; if (o) if (const Value* av = get10(*o, "args")) { if (av->type != Value::Type::ARRAY || !av->array) failType10("algoCompare", "'args' must be an array", line); args = *av->array; }
        struct R { std::string name; std::shared_ptr<MapData> s; double mean; };
        std::vector<R> rs;
        for (const auto& kv : *a[0].map) {
            if (kv.first.type != Value::Type::STRING) failType10("algoCompare", "names must be strings", line);
            needFn11(kv.second, "algoCompare", kv.first.str.c_str(), line);
            auto s = summarize(benchRuns(kv.second, args, runs, warm, line));
            rs.push_back({kv.first.str, s, s->findValue(Value::string("meanMs"))->number});
        }
        std::stable_sort(rs.begin(), rs.end(), [](const R& p, const R& q) { return p.mean < q.mean; });
        auto arr = std::make_shared<ArrayData>(); const double best = std::max(rs.front().mean, 1e-12);
        for (auto& r : rs) {
            auto m = std::make_shared<MapData>(); put10(m, "name", S10(r.name));
            for (const char* k : {"meanMs", "minMs", "medianMs", "stdevMs"}) put10(m, k, *r.s->findValue(Value::string(k)));
            put10(m, "ratio", N10(r.mean / best)); arr->push_back(Value::makeMap(m));
        }
        return Value::makeArray(arr);
    };
    // algoComplexity(fn, sizes[, {gen: fn(n), runs=3, seed}]) -> {samples:[{n, ms}], exponent, best, fits:[{model, spread}]}
    // يقيس زمن fn(gen(n)) لكل حجم ويُقدّر رتبة النمو. fn تستقبل ما تُرجعه gen (افتراضياً: مصفوفة n أرقام عشوائية).
    natives["algoComplexity"] = [this, benchRuns](Args& a, int line) -> Value {
        expectArgsRange("algoComplexity", a, 2, 3, line);
        needFn11(a[0], "algoComplexity", "fn", line);
        auto sizes = readVec10(a[1], "algoComplexity", "sizes", line, true);
        if (sizes.size() < 3) failArg10("algoComplexity", "need at least 3 sizes (e.g. [1000, 2000, 4000, 8000])", line);
        for (size_t i = 0; i < sizes.size(); ++i) { if (sizes[i] < 2 || sizes[i] != std::floor(sizes[i]) || sizes[i] > 1e8) failArg10("algoComplexity", "sizes must be integers >= 2", line); if (i && sizes[i] <= sizes[i - 1]) failArg10("algoComplexity", "sizes must be strictly increasing", line); }
        const Value* o = optMap10(a, 2, "algoComplexity", line);
        const long runs = static_cast<long>(optNum10(o, "runs", 3, "algoComplexity", line));
        if (runs < 1 || runs > 1000) failArg10("algoComplexity", "'runs' must be 1..1000", line);
        const Value* gv = o ? get10(*o, "gen") : nullptr; const bool hasGen = gv && gv->type != Value::Type::NIL;
        if (hasGen) needFn11(*gv, "algoComplexity", "gen", line);
        std::mt19937_64 g(seedFrom11(o, "algoComplexity", line));
        std::vector<double> ns, ts;
        auto samples = std::make_shared<ArrayData>();
        for (double sz : sizes) {
            Value input;
            if (hasGen) { std::vector<Value> ga{N10(sz)}; input = callValue(*gv, ga, line); }
            else { auto arr = std::make_shared<ArrayData>(); arr->reserve(static_cast<size_t>(sz)); for (size_t i = 0; i < static_cast<size_t>(sz); ++i) arr->push_back(N10(static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0))); input = Value::makeArray(arr); }
            // fn قد تعدّل مدخلها: نُمرّر نسخة سطحية جديدة لكل تشغيل
            double best = INFINITY;
            for (long r = 0; r < runs; ++r) {
                Value in2 = input; if (input.type == Value::Type::ARRAY && input.array) in2 = Value::makeArray(std::make_shared<ArrayData>(*input.array));
                Timing t = benchRuns(a[0], {in2}, 1, 0, line); best = std::min(best, t.ms[0]);
            }
            ns.push_back(sz); ts.push_back(std::max(best, 1e-6));
            auto m = std::make_shared<MapData>(); put10(m, "n", N10(sz)); put10(m, "ms", N10(best)); samples->push_back(Value::makeMap(m));
        }
        const size_t k = ns.size();
        // الأُسّ: ميل log-log بالمربعات الصغرى
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < k; ++i) { const double lx = std::log(ns[i]), ly = std::log(ts[i]); sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly; }
        const double slope = (static_cast<double>(k) * sxy - sx * sy) / (static_cast<double>(k) * sxx - sx * sx);
        struct Model { const char* name; std::function<double(double)> g; };
        std::vector<Model> models = {
            {"O(1)", [](double) { return 1.0; }}, {"O(log n)", [](double n) { return std::log(n); }}, {"O(n)", [](double n) { return n; }},
            {"O(n log n)", [](double n) { return n * std::log(n); }}, {"O(n^2)", [](double n) { return n * n; }}, {"O(n^3)", [](double n) { return n * n * n; }}};
        struct Fit { const char* name; double spread; };
        std::vector<Fit> fits;  // نفس ترتيب models (من الأبطأ نمواً إلى الأسرع)
        for (auto& md : models) {
            std::vector<double> r; for (size_t i = 0; i < k; ++i) r.push_back(std::log(ts[i]) - std::log(md.g(ns[i])));
            double mean = 0; for (double v : r) mean += v; mean /= static_cast<double>(k);
            double var = 0; for (double v : r) var += (v - mean) * (v - mean);
            fits.push_back({md.name, std::sqrt(var / static_cast<double>(k))});
        }
        // الأبسط المتّسق مع القياس: أي نموذج spread له ≤ أقل spread + 0.08 (≈ ضجيج توقيت 8%) مقبول، ونختار أبطأها نمواً
        const size_t nm = fits.size();
        double minSpread = INFINITY; for (auto& f : fits) minSpread = std::min(minSpread, f.spread);
        size_t pick = 0;
        auto cands = std::make_shared<ArrayData>();
        for (size_t i = 0; i < nm; ++i) if (fits[i].spread <= minSpread + 0.08) { if (cands->empty()) pick = i; cands->push_back(S10(fits[i].name)); }
        auto sortedFits = fits; std::stable_sort(sortedFits.begin(), sortedFits.end(), [](const Fit& p, const Fit& q) { return p.spread < q.spread; });
        auto fa = std::make_shared<ArrayData>();
        for (auto& f : sortedFits) { auto m = std::make_shared<MapData>(); put10(m, "model", S10(f.name)); put10(m, "spread", N10(f.spread)); fa->push_back(Value::makeMap(m)); }
        auto m = std::make_shared<MapData>();
        put10(m, "samples", Value::makeArray(samples)); put10(m, "exponent", N10(slope)); put10(m, "best", S10(fits[pick].name));
        put10(m, "candidates", Value::makeArray(cands)); put10(m, "fits", Value::makeArray(fa));
        return Value::makeMap(m);
    };
    // algoCheck(fn, ref, inputs[, {spread:false, rel, abs, maxFailures:10}]) — اختبار تفاضلي: fn مقابل مرجع على كل مُدخَل
    natives["algoCheck"] = [this](Args& a, int line) -> Value {
        expectArgsRange("algoCheck", a, 3, 4, line);
        needFn11(a[0], "algoCheck", "fn", line); needFn11(a[1], "algoCheck", "ref", line);
        if (a[2].type != Value::Type::ARRAY || !a[2].array) failType10("algoCheck", "'inputs' must be an array of test inputs", line);
        const Value* o = optMap10(a, 3, "algoCheck", line);
        const double rel = optNum10(o, "rel", 1e-9, "algoCheck", line), ab = optNum10(o, "abs", 1e-12, "algoCheck", line);
        const bool spread = optBool10(o, "spread", false); const size_t maxF = static_cast<size_t>(optNum10(o, "maxFailures", 10, "algoCheck", line));
        auto runOne = [&](const Value& fn, const Value& input, Value& out, std::string& err) -> bool {
            std::vector<Value> args;
            if (spread) { if (input.type != Value::Type::ARRAY || !input.array) { err = "with {spread:true} every input must be an array of arguments"; return false; } args = *input.array; }
            else {
                // قد تعدّل الدالة مدخلها؛ نمرّر نسخة سطحية
                args.push_back(input.type == Value::Type::ARRAY && input.array ? Value::makeArray(std::make_shared<ArrayData>(*input.array)) : input);
            }
            try { out = callValue(fn, args, line); return true; }
            catch (const RinError& e) { err = e.message; return false; }
            catch (const ThrowSignal& t) { err = "threw " + brief11(t.value); return false; }
        };
        auto failures = std::make_shared<ArrayData>(); size_t failed = 0, idx = 0;
        for (const Value& in : *a[2].array) {
            Value got, want; std::string e1, e2;
            const bool ok1 = runOne(a[0], in, got, e1), ok2 = runOne(a[1], in, want, e2);
            bool same = (ok1 && ok2) ? approxEq11(got, want, rel, ab) : (!ok1 && !ok2);
            if (!same) {
                ++failed;
                if (failures->size() < maxF) {
                    auto m = std::make_shared<MapData>(); put10(m, "index", N10(static_cast<double>(idx))); put10(m, "input", in);
                    put10(m, "got", ok1 ? got : S10("error: " + e1)); put10(m, "expected", ok2 ? want : S10("error: " + e2));
                    failures->push_back(Value::makeMap(m));
                }
            }
            ++idx;
        }
        auto m = std::make_shared<MapData>();
        put10(m, "ok", B10(failed == 0)); put10(m, "tested", N10(static_cast<double>(a[2].array->size()))); put10(m, "failed", N10(static_cast<double>(failed))); put10(m, "failures", Value::makeArray(failures));
        return Value::makeMap(m);
    };

    // ======================================================== المخططات على مصفوفة الأوزان
    // algoGraphMatrix(edges, n[, {directed:true}]) — edges: [[u, v, w=1], ...] (عقد من 0) -> مصفوفة n×n (القطر 0، غياب الحافة Infinity)
    natives["algoGraphMatrix"] = [](Args& a, int line) -> Value {
        expectArgsRange("algoGraphMatrix", a, 2, 3, line);
        if (a[0].type != Value::Type::ARRAY || !a[0].array) failType10("algoGraphMatrix", "'edges' must be an array of [u, v, weight]", line);
        const size_t n = needDim10(a[1], "algoGraphMatrix", "n", line);
        if (n > 5000) failArg10("algoGraphMatrix", "n too large", line);
        const Value* o = optMap10(a, 2, "algoGraphMatrix", line); const bool directed = optBool10(o, "directed", true);
        Mat W(n, n, INFINITY); for (size_t i = 0; i < n; ++i) W(i, i) = 0.0;
        for (size_t k = 0; k < a[0].array->size(); ++k) {
            auto e = readVec10((*a[0].array)[k], "algoGraphMatrix", ("edge " + std::to_string(k)).c_str(), line, true);
            if (e.size() < 2 || e.size() > 3) failArg10("algoGraphMatrix", "edge " + std::to_string(k) + " must be [u, v] or [u, v, weight]", line);
            if (e[0] < 0 || e[1] < 0 || e[0] >= static_cast<double>(n) || e[1] >= static_cast<double>(n) || e[0] != std::floor(e[0]) || e[1] != std::floor(e[1]))
                failArg10("algoGraphMatrix", "edge " + std::to_string(k) + " has a node outside 0.." + std::to_string(n - 1), line);
            const double w = e.size() == 3 ? e[2] : 1.0; const size_t u = static_cast<size_t>(e[0]), v = static_cast<size_t>(e[1]);
            if (u == v) continue;
            W(u, v) = std::min(W(u, v), w); if (!directed) W(v, u) = std::min(W(v, u), w);
        }
        return matVal(W);
    };
    // algoDijkstra(W, src[, dst]) -> {dist, prev, path?}  — أوزان غير سالبة؛ الحافة الغائبة = أي قيمة غير منتهية
    natives["algoDijkstra"] = [](Args& a, int line) -> Value {
        expectArgsRange("algoDijkstra", a, 2, 3, line);
        Mat W = readMat10(a[0], "algoDijkstra", "W", line); requireSquare10(W, "algoDijkstra", "W", line);
        const size_t n = W.r; const size_t src = needDim10(a[1], "algoDijkstra", "src", line, 0);
        if (src >= n) failArg10("algoDijkstra", "src out of range 0.." + std::to_string(n - 1), line);
        for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) if (i != j && std::isfinite(W(i, j)) && W(i, j) < 0) failRun10("algoDijkstra", "negative weight at [" + std::to_string(i) + "][" + std::to_string(j) + "] — use algoFloyd", line);
        std::vector<double> dist(n, INFINITY), prev(n, -1.0); dist[src] = 0;
        using PQ = std::pair<double, size_t>; std::priority_queue<PQ, std::vector<PQ>, std::greater<PQ>> pq; pq.push({0.0, src});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop(); if (d > dist[u]) continue;
            for (size_t v = 0; v < n; ++v) { if (v == u || !std::isfinite(W(u, v))) continue; const double nd = d + W(u, v); if (nd < dist[v]) { dist[v] = nd; prev[v] = static_cast<double>(u); pq.push({nd, v}); } }
        }
        auto m = std::make_shared<MapData>(); put10(m, "dist", vecVal(dist)); put10(m, "prev", vecVal(prev));
        if (a.size() == 3) {
            const size_t dst = needDim10(a[2], "algoDijkstra", "dst", line, 0);
            if (dst >= n) failArg10("algoDijkstra", "dst out of range 0.." + std::to_string(n - 1), line);
            std::vector<double> path;
            if (std::isfinite(dist[dst])) { for (double c = static_cast<double>(dst); c >= 0; c = prev[static_cast<size_t>(c)]) { path.push_back(c); if (static_cast<size_t>(c) == src) break; } std::reverse(path.begin(), path.end()); }
            put10(m, "path", vecVal(path));
        }
        return Value::makeMap(m);
    };
    // algoFloyd(W) -> {dist, negativeCycle}  — كل الأزواج؛ يقبل أوزاناً سالبة
    natives["algoFloyd"] = [](Args& a, int line) -> Value {
        expectArgs("algoFloyd", a, 1, line);
        Mat D = readMat10(a[0], "algoFloyd", "W", line); requireSquare10(D, "algoFloyd", "W", line);
        const size_t n = D.r; if (n > 3000) failArg10("algoFloyd", "graph too large (n^3)", line);
        for (double& x : D.d) if (!std::isfinite(x)) x = INFINITY;
        for (size_t i = 0; i < n; ++i) D(i, i) = std::min(D(i, i), 0.0);
        for (size_t k = 0; k < n; ++k) for (size_t i = 0; i < n; ++i) { const double dik = D(i, k); if (!std::isfinite(dik)) continue; for (size_t j = 0; j < n; ++j) { const double v = dik + D(k, j); if (v < D(i, j)) D(i, j) = v; } }
        bool neg = false; for (size_t i = 0; i < n; ++i) if (D(i, i) < 0) neg = true;
        auto m = std::make_shared<MapData>(); put10(m, "dist", matVal(D)); put10(m, "negativeCycle", B10(neg));
        return Value::makeMap(m);
    };
}

} // namespace rin
