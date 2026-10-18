// ============================================================================
//  rin terminal — batch 4 (part 3/4): math, statistics, finance, geometry, dates.
//  Header-only, POSIX + std only. No process is spawned (works on Android).
//  Numbers may be written 1_000, 1,000, 0xFF, 0b101 (see parseNum).
//  A command that is given bad input prints one line to stderr and returns 1.
// ============================================================================
#pragma once

#include "rin_terminal_xcmds5.h"

#include <stdexcept>

namespace rin {
namespace terminal {
namespace xcmds {

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------
struct MathErr : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] inline void mfail(const std::string& m) { throw MathErr(m); }

inline long long asInt(double d, const char* what = "value") {
    if (!(std::fabs(d) < 9.0e15) || d != std::floor(d)) mfail(std::string(what) + " must be an integer (|n| < 9e15): " + fnum(d));
    return static_cast<long long>(d);
}
using NumFn = std::function<std::string(const std::vector<double>&)>;

// numbers in -> text out; exactly `n` numbers (n == 0: any count >= minCount)
inline void addMath(Reg& r, const std::string& name, const std::string& hint, const std::string& desc, size_t n, size_t minCount, NumFn f) {
    r.add(name, "$ " + name + " " + hint, desc, false, [f, n, minCount, name, hint](Ctx& c) -> int {
        std::vector<double> v;
        if (!numsIn(c, v, std::max<size_t>(minCount, n))) return 1;
        if (n && v.size() != n) return c.fail("usage: $ " + name + " " + hint);
        try { c.say(f(v)); } catch (const MathErr& e) { return c.fail(e.what()); }
        return 0;
    });
}
inline void addMap(Reg& r, const std::string& name, const std::string& desc, std::function<std::string(double)> f) {
    addMath(r, name, "<number…>", desc, 0, 1, [f](const std::vector<double>& v) {
        std::string o;
        for (size_t i = 0; i < v.size(); ++i) { if (i) o += '\n'; o += f(v[i]); }
        return o;
    });
}
inline void addPure(Reg& r, const std::string& name, const std::string& desc, double (*fn)(double)) {
    addMap(r, name, desc, [fn, name](double x) { double y = fn(x); if (std::isnan(y)) mfail("domain error: " + name + "(" + fnum(x) + ") is undefined"); return fnum(y); });
}

inline std::string bigFactorial(unsigned n) {
    std::vector<uint32_t> d{1};                       // base 1e9 little endian
    for (unsigned k = 2; k <= n; ++k) {
        uint64_t carry = 0;
        for (auto& x : d) { uint64_t t = static_cast<uint64_t>(x) * k + carry; x = static_cast<uint32_t>(t % 1000000000u); carry = t / 1000000000u; }
        while (carry) { d.push_back(static_cast<uint32_t>(carry % 1000000000u)); carry /= 1000000000u; }
    }
    std::string s = std::to_string(d.back());
    for (size_t i = d.size() - 1; i-- > 0;) { char b[16]; std::snprintf(b, sizeof(b), "%09u", d[i]); s += b; }
    return s;
}
inline bool isPrimeLL(unsigned long long n) {
    if (n < 2) return false;
    for (unsigned long long p : {2ULL, 3ULL, 5ULL, 7ULL, 11ULL, 13ULL, 17ULL, 19ULL, 23ULL, 29ULL, 31ULL, 37ULL}) { if (n == p) return true; if (n % p == 0) return false; }
    unsigned long long d = n - 1; int s = 0;
    while ((d & 1) == 0) { d >>= 1; ++s; }
    auto mulmod = [&](unsigned long long a, unsigned long long b) { return static_cast<unsigned long long>((static_cast<unsigned __int128>(a) * b) % n); };
    auto powmod = [&](unsigned long long b, unsigned long long e) { unsigned long long r2 = 1; b %= n; while (e) { if (e & 1) r2 = mulmod(r2, b); b = mulmod(b, b); e >>= 1; } return r2; };
    for (unsigned long long a : {2ULL, 3ULL, 5ULL, 7ULL, 11ULL, 13ULL, 17ULL, 19ULL, 23ULL, 29ULL, 31ULL, 37ULL}) {   // deterministic below 3.3e24
        unsigned long long x = powmod(a, d);
        if (x == 1 || x == n - 1) continue;
        bool comp = true;
        for (int i = 1; i < s; ++i) { x = mulmod(x, x); if (x == n - 1) { comp = false; break; } }
        if (comp) return false;
    }
    return true;
}
inline std::vector<std::pair<unsigned long long, int>> factorize(unsigned long long n) {
    std::vector<std::pair<unsigned long long, int>> f;
    for (unsigned long long p = 2; p * p <= n && p < 4000000000ULL; p += (p == 2 ? 1 : 2)) {
        if (n % p) continue;
        int e = 0;
        while (n % p == 0) { n /= p; ++e; }
        f.push_back({p, e});
    }
    if (n > 1) f.push_back({n, 1});
    return f;
}
inline long long gcdLL(long long a, long long b) { a = std::llabs(a); b = std::llabs(b); while (b) { long long t = a % b; a = b; b = t; } return a; }
inline double sumOf(const std::vector<double>& v) { double s = 0, comp = 0; for (double x : v) { double y = x - comp; double t = s + y; comp = (t - s) - y; s = t; } return s; }   // Kahan
inline double meanOf(const std::vector<double>& v) { return sumOf(v) / static_cast<double>(v.size()); }
inline double varOf(const std::vector<double>& v, bool sample) {
    double m = meanOf(v), s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return s / static_cast<double>(v.size() - (sample ? 1 : 0));
}
inline double quantileOf(std::vector<double> v, double q) {            // linear interpolation (numpy default)
    std::sort(v.begin(), v.end());
    double pos = q * static_cast<double>(v.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(pos)), hi = static_cast<size_t>(std::ceil(pos));
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}
inline std::string joinNums(const std::vector<double>& v, const char* sep = " ") {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += fnum(v[i]); }
    return o;
}
inline std::string money(double v) { char b[64]; std::snprintf(b, sizeof(b), "%.2f", v); return b; }

// ----------------------------------------------------------------------------
// B. math
// ----------------------------------------------------------------------------
inline void addMathCmds(Reg& r) {
    // --- element-wise functions -------------------------------------------------
    addPure(r, "sqrt",  "square root of each number", [](double x) { return std::sqrt(x); });
    addPure(r, "cbrt",  "cube root of each number", [](double x) { return std::cbrt(x); });
    addPure(r, "abs",   "absolute value", [](double x) { return std::fabs(x); });
    addPure(r, "ceil",  "round up to an integer", [](double x) { return std::ceil(x); });
    addPure(r, "floor", "round down to an integer", [](double x) { return std::floor(x); });
    addPure(r, "round", "round to nearest integer (half away from zero)", [](double x) { return std::round(x); });
    addPure(r, "trunc", "drop the fractional part", [](double x) { return std::trunc(x); });
    addPure(r, "sign",  "-1, 0 or 1", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); });
    addPure(r, "square", "x squared", [](double x) { return x * x; });
    addPure(r, "cube",  "x cubed", [](double x) { return x * x * x; });
    addPure(r, "exp",   "e to the power x", [](double x) { return std::exp(x); });
    addPure(r, "exp2",  "2 to the power x", [](double x) { return std::exp2(x); });
    addPure(r, "loge",  "natural logarithm (ln)", [](double x) { return std::log(x); });
    addPure(r, "log10", "base-10 logarithm", [](double x) { return std::log10(x); });
    addPure(r, "log2",  "base-2 logarithm", [](double x) { return std::log2(x); });
    addPure(r, "sin",   "sine (radians)", [](double x) { return std::sin(x); });
    addPure(r, "cos",   "cosine (radians)", [](double x) { return std::cos(x); });
    addPure(r, "tan",   "tangent (radians)", [](double x) { return std::tan(x); });
    addPure(r, "asin",  "arc sine (radians)", [](double x) { return std::asin(x); });
    addPure(r, "acos",  "arc cosine (radians)", [](double x) { return std::acos(x); });
    addPure(r, "atan",  "arc tangent (radians)", [](double x) { return std::atan(x); });
    addPure(r, "sinh",  "hyperbolic sine", [](double x) { return std::sinh(x); });
    addPure(r, "cosh",  "hyperbolic cosine", [](double x) { return std::cosh(x); });
    addPure(r, "tanh",  "hyperbolic tangent", [](double x) { return std::tanh(x); });
    addPure(r, "sind",  "sine of degrees", [](double x) { double m = std::fmod(x, 360.0); if (m == 0 || std::fabs(m) == 180) return 0.0; return std::sin(x * M_PI / 180.0); });
    addPure(r, "cosd",  "cosine of degrees", [](double x) { double m = std::fabs(std::fmod(x, 360.0)); if (m == 90 || m == 270) return 0.0; return std::cos(x * M_PI / 180.0); });
    addPure(r, "tand",  "tangent of degrees", [](double x) { return std::tan(x * M_PI / 180.0); });
    addPure(r, "gamma", "gamma function", [](double x) { return std::tgamma(x); });
    addPure(r, "erf",   "error function", [](double x) { return std::erf(x); });

    // --- integer functions -------------------------------------------------------
    addMap(r, "fact", "exact factorial (n <= 1000)", [](double x) {
        long long n = asInt(x, "n");
        if (n < 0 || n > 1000) mfail("n must be 0..1000");
        return bigFactorial(static_cast<unsigned>(n));
    });
    addMap(r, "isprime", "prime test (deterministic Miller-Rabin, up to 2^63)", [](double x) {
        long long n = asInt(x, "n");
        return std::string(n >= 2 && isPrimeLL(static_cast<unsigned long long>(n)) ? "true" : "false");
    });
    addMap(r, "nextprime", "smallest prime greater than n", [](double x) {
        long long n = asInt(x, "n");
        if (n > 4000000000000000LL) mfail("n too large");
        for (long long k = std::max(n + 1, 2LL);; ++k) if (isPrimeLL(static_cast<unsigned long long>(k))) return std::to_string(k);
    });
    addMap(r, "prevprime", "largest prime smaller than n", [](double x) {
        long long n = asInt(x, "n");
        for (long long k = n - 1; k >= 2; --k) if (isPrimeLL(static_cast<unsigned long long>(k))) return std::to_string(k);
        mfail("no prime below " + std::to_string(n));
    });
    addMap(r, "totient", "Euler's phi function", [](double x) {
        long long n = asInt(x, "n");
        if (n < 1) mfail("n must be >= 1");
        unsigned long long res = static_cast<unsigned long long>(n);
        for (auto& pe : factorize(static_cast<unsigned long long>(n))) res = res / pe.first * (pe.first - 1);
        return std::to_string(res);
    });
    addMap(r, "divisors", "all divisors of n, ascending", [](double x) {
        long long n = asInt(x, "n");
        if (n < 1) mfail("n must be >= 1");
        std::vector<long long> d{1};
        for (auto& pe : factorize(static_cast<unsigned long long>(n))) {
            size_t sz = d.size();
            long long pw = 1;
            for (int e = 1; e <= pe.second; ++e) { pw *= static_cast<long long>(pe.first); for (size_t i = 0; i < sz; ++i) d.push_back(d[i] * pw); }
        }
        std::sort(d.begin(), d.end());
        std::string o;
        for (size_t i = 0; i < d.size(); ++i) { if (i) o += ' '; o += std::to_string(d[i]); }
        return o;
    });
    addMap(r, "sumdigits", "sum of the decimal digits", [](double x) {
        long long n = std::llabs(asInt(x, "n")); long long s = 0;
        for (; n; n /= 10) s += n % 10;
        return std::to_string(s);
    });
    addMap(r, "digitroot", "repeated digit sum down to one digit", [](double x) {
        long long n = std::llabs(asInt(x, "n"));
        return std::to_string(n == 0 ? 0 : 1 + (n - 1) % 9);
    });
    addMap(r, "revnum", "reverse the digits of an integer", [](double x) {
        long long n = asInt(x, "n"); bool neg = n < 0; n = std::llabs(n);
        std::string s = std::to_string(n); std::reverse(s.begin(), s.end());
        size_t nz = s.find_first_not_of('0');
        s = nz == std::string::npos ? "0" : s.substr(nz);
        return (neg && s != "0" ? "-" : "") + s;
    });
    addMap(r, "collatz", "Collatz steps to reach 1 and the peak value", [](double x) {
        long long n = asInt(x, "n");
        if (n < 1) mfail("n must be >= 1");
        long long steps = 0, peak = n;
        while (n != 1) { n = (n % 2) ? 3 * n + 1 : n / 2; peak = std::max(peak, n); if (++steps > 100000000) mfail("too long"); }
        return std::to_string(steps) + " steps, peak " + std::to_string(peak);
    });
    addMap(r, "isperfect", "is n a perfect number?", [](double x) {
        long long n = asInt(x, "n");
        if (n < 2) return std::string("false");
        long long s = 1;
        for (long long k = 2; k * k <= n; ++k) if (n % k == 0) { s += k; if (k != n / k) s += n / k; }
        return std::string(s == n ? "true" : "false");
    });
    addMap(r, "ispow2", "is n a power of two?", [](double x) { long long n = asInt(x, "n"); return std::string(n > 0 && (n & (n - 1)) == 0 ? "true" : "false"); });
    addMap(r, "isarmstrong", "is n an Armstrong (narcissistic) number?", [](double x) {
        long long n = asInt(x, "n");
        if (n < 0) return std::string("false");
        std::string s = std::to_string(n); long long t = 0;
        for (char ch : s) { long long p = 1; for (size_t i = 0; i < s.size(); ++i) p *= ch - '0'; t += p; }
        return std::string(t == n ? "true" : "false");
    });
    addMap(r, "triangular", "n-th triangular number", [](double x) { long long n = asInt(x, "n"); return std::to_string(n * (n + 1) / 2); });
    addMap(r, "catalan", "n-th Catalan number (n <= 33)", [](double x) {
        long long n = asInt(x, "n");
        if (n < 0 || n > 33) mfail("n must be 0..33");
        unsigned long long c = 1;
        for (long long k = 0; k < n; ++k) c = c * 2 * (2 * k + 1) / (k + 2);
        return std::to_string(c);
    });
    addMap(r, "pascaltri", "row n of Pascal's triangle (n <= 60)", [](double x) {
        long long n = asInt(x, "n");
        if (n < 0 || n > 60) mfail("n must be 0..60");
        std::string o; unsigned long long c = 1;
        for (long long k = 0; k <= n; ++k) { if (k) o += ' '; o += std::to_string(c); c = c * static_cast<unsigned long long>(n - k) / static_cast<unsigned long long>(k + 1); }
        return o;
    });
    addMap(r, "bits", "binary form, popcount and bit length of an integer", [](double x) {
        long long n = asInt(x, "n");
        if (n < 0) mfail("n must be >= 0");
        std::string b; unsigned long long u = static_cast<unsigned long long>(n);
        if (!u) b = "0";
        while (u) { b.insert(b.begin(), static_cast<char>('0' + (u & 1))); u >>= 1; }
        return b + "  (ones " + std::to_string(__builtin_popcountll(static_cast<unsigned long long>(n))) + ", length " + std::to_string(b.size()) + ")";
    });
    addMap(r, "gray", "Gray code of an integer", [](double x) {
        long long n = asInt(x, "n");
        if (n < 0) mfail("n must be >= 0");
        unsigned long long g = static_cast<unsigned long long>(n) ^ (static_cast<unsigned long long>(n) >> 1);
        std::string b; if (!g) b = "0";
        while (g) { b.insert(b.begin(), static_cast<char>('0' + (g & 1))); g >>= 1; }
        return b;
    });

    // --- aggregates ----------------------------------------------------------------
    addMath(r, "sum",  "<numbers…>", "sum (Kahan-compensated)", 0, 1, [](const std::vector<double>& v) { return fnum(sumOf(v)); });
    addMath(r, "prod", "<numbers…>", "product", 0, 1, [](const std::vector<double>& v) { double p = 1; for (double x : v) p *= x; return fnum(p); });
    addMath(r, "mean", "<numbers…>", "arithmetic mean", 0, 1, [](const std::vector<double>& v) { return fnum(meanOf(v)); });
    addMath(r, "median", "<numbers…>", "median", 0, 1, [](const std::vector<double>& v) { return fnum(quantileOf(v, 0.5)); });
    addMath(r, "mode", "<numbers…>", "most frequent value(s)", 0, 1, [](const std::vector<double>& v) {
        std::map<double, int> m; int best = 0;
        for (double x : v) best = std::max(best, ++m[x]);
        std::string o;
        for (auto& kv : m) if (kv.second == best) { if (!o.empty()) o += ' '; o += fnum(kv.first); }
        return o + "  (x" + std::to_string(best) + ")";
    });
    addMath(r, "min", "<numbers…>", "smallest value", 0, 1, [](const std::vector<double>& v) { return fnum(*std::min_element(v.begin(), v.end())); });
    addMath(r, "max", "<numbers…>", "largest value", 0, 1, [](const std::vector<double>& v) { return fnum(*std::max_element(v.begin(), v.end())); });
    addMath(r, "range", "<numbers…>", "max - min", 0, 1, [](const std::vector<double>& v) { auto mm = std::minmax_element(v.begin(), v.end()); return fnum(*mm.second - *mm.first); });
    addMath(r, "variance", "<numbers…>", "sample variance (n-1)", 0, 2, [](const std::vector<double>& v) { return fnum(varOf(v, true)); });
    addMath(r, "stdev", "<numbers…>", "sample standard deviation (n-1)", 0, 2, [](const std::vector<double>& v) { return fnum(std::sqrt(varOf(v, true))); });
    addMath(r, "pvariance", "<numbers…>", "population variance (n)", 0, 1, [](const std::vector<double>& v) { return fnum(varOf(v, false)); });
    addMath(r, "pstdev", "<numbers…>", "population standard deviation (n)", 0, 1, [](const std::vector<double>& v) { return fnum(std::sqrt(varOf(v, false))); });
    addMath(r, "geomean", "<positive numbers…>", "geometric mean", 0, 1, [](const std::vector<double>& v) {
        double s = 0; for (double x : v) { if (x <= 0) mfail("all values must be > 0"); s += std::log(x); }
        return fnum(std::exp(s / static_cast<double>(v.size())));
    });
    addMath(r, "harmean", "<positive numbers…>", "harmonic mean", 0, 1, [](const std::vector<double>& v) {
        double s = 0; for (double x : v) { if (x <= 0) mfail("all values must be > 0"); s += 1 / x; }
        return fnum(static_cast<double>(v.size()) / s);
    });
    addMath(r, "rms", "<numbers…>", "root mean square", 0, 1, [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x * x; return fnum(std::sqrt(s / static_cast<double>(v.size()))); });
    addMath(r, "sumsq", "<numbers…>", "sum of squares", 0, 1, [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x * x; return fnum(s); });
    addMath(r, "mad", "<numbers…>", "mean absolute deviation", 0, 1, [](const std::vector<double>& v) { double m = meanOf(v), s = 0; for (double x : v) s += std::fabs(x - m); return fnum(s / static_cast<double>(v.size())); });
    addMath(r, "cv", "<numbers…>", "coefficient of variation in % (sample stdev / mean)", 0, 2, [](const std::vector<double>& v) {
        double m = meanOf(v); if (m == 0) mfail("mean is 0");
        return fnum(std::sqrt(varOf(v, true)) / std::fabs(m) * 100) + "%";
    });
    addMath(r, "iqr", "<numbers…>", "interquartile range (Q3 - Q1, linear interpolation)", 0, 1, [](const std::vector<double>& v) {
        return "Q1 " + fnum(quantileOf(v, .25)) + "  Q3 " + fnum(quantileOf(v, .75)) + "  IQR " + fnum(quantileOf(v, .75) - quantileOf(v, .25));
    });
    addMath(r, "percentile", "<P> <numbers…>", "P-th percentile (0..100, linear interpolation)", 0, 2, [](const std::vector<double>& v) {
        if (v[0] < 0 || v[0] > 100) mfail("P must be 0..100");
        return fnum(quantileOf(std::vector<double>(v.begin() + 1, v.end()), v[0] / 100));
    });
    addMath(r, "skew", "<numbers…>", "population skewness", 0, 3, [](const std::vector<double>& v) {
        double m = meanOf(v), m2 = 0, m3 = 0;
        for (double x : v) { m2 += (x - m) * (x - m); m3 += (x - m) * (x - m) * (x - m); }
        double n = static_cast<double>(v.size()); m2 /= n; m3 /= n;
        if (m2 == 0) mfail("zero variance");
        return fnum(m3 / std::pow(m2, 1.5));
    });
    addMath(r, "kurt", "<numbers…>", "population excess kurtosis", 0, 4, [](const std::vector<double>& v) {
        double m = meanOf(v), m2 = 0, m4 = 0;
        for (double x : v) { double d = x - m; m2 += d * d; m4 += d * d * d * d; }
        double n = static_cast<double>(v.size()); m2 /= n; m4 /= n;
        if (m2 == 0) mfail("zero variance");
        return fnum(m4 / (m2 * m2) - 3);
    });
    addMath(r, "cumsum", "<numbers…>", "running totals", 0, 1, [](const std::vector<double>& v) { std::vector<double> o; double s = 0; for (double x : v) { s += x; o.push_back(s); } return joinNums(o); });
    addMath(r, "diffs", "<numbers…>", "differences between neighbours", 0, 2, [](const std::vector<double>& v) { std::vector<double> o; for (size_t i = 1; i < v.size(); ++i) o.push_back(v[i] - v[i - 1]); return joinNums(o); });
    addMath(r, "sortnum", "<numbers…>", "sort ascending", 0, 1, [](const std::vector<double>& v) { auto s = v; std::sort(s.begin(), s.end()); return joinNums(s); });
    addMath(r, "rsortnum", "<numbers…>", "sort descending", 0, 1, [](const std::vector<double>& v) { auto s = v; std::sort(s.begin(), s.end(), std::greater<double>()); return joinNums(s); });
    addMath(r, "zscore", "<numbers…>", "standard score of each value (population stdev)", 0, 2, [](const std::vector<double>& v) {
        double m = meanOf(v), sd = std::sqrt(varOf(v, false));
        if (sd == 0) mfail("zero variance");
        std::vector<double> o; for (double x : v) o.push_back((x - m) / sd);
        return joinNums(o);
    });
    addMath(r, "normalize", "<numbers…>", "rescale to 0..1 (min-max)", 0, 2, [](const std::vector<double>& v) {
        auto mm = std::minmax_element(v.begin(), v.end());
        double lo = *mm.first, hi = *mm.second;
        if (hi == lo) mfail("all values are equal");
        std::vector<double> o; for (double x : v) o.push_back((x - lo) / (hi - lo));
        return joinNums(o);
    });
    addMath(r, "linreg", "<x1 y1 x2 y2 …>", "least-squares line through (x,y) pairs: slope, intercept, r", 0, 4, [](const std::vector<double>& v) {
        if (v.size() % 2) mfail("give an even count: x1 y1 x2 y2 …");
        std::vector<double> xs, ys;
        for (size_t i = 0; i < v.size(); i += 2) { xs.push_back(v[i]); ys.push_back(v[i + 1]); }
        double mx = meanOf(xs), my = meanOf(ys), sxx = 0, sxy = 0, syy = 0;
        for (size_t i = 0; i < xs.size(); ++i) { sxx += (xs[i] - mx) * (xs[i] - mx); sxy += (xs[i] - mx) * (ys[i] - my); syy += (ys[i] - my) * (ys[i] - my); }
        if (sxx == 0) mfail("all x are equal");
        double slope = sxy / sxx, icpt = my - slope * mx;
        double rr = syy == 0 ? 1.0 : sxy / std::sqrt(sxx * syy);
        return "y = " + fnum(slope) + " x + " + fnum(icpt) + "   r = " + fnum(rr) + "   r2 = " + fnum(rr * rr);
    });

    // --- two / three arguments -----------------------------------------------------------
    addMath(r, "pow",  "<x> <y>", "x to the power y", 2, 2, [](const std::vector<double>& v) { return fnum(std::pow(v[0], v[1])); });
    addMath(r, "mod",  "<a> <b>", "remainder: truncated (C %) and floored (Python %)", 2, 2, [](const std::vector<double>& v) {
        if (v[1] == 0) mfail("division by zero");
        double t = std::fmod(v[0], v[1]), f = t; if (f != 0 && ((f < 0) != (v[1] < 0))) f += v[1];
        return fnum(t) + " (truncated)   " + fnum(f) + " (floored)";
    });
    addMath(r, "root", "<x> <n>", "n-th root of x", 2, 2, [](const std::vector<double>& v) {
        if (v[1] == 0) mfail("n must not be 0");
        if (v[0] < 0 && std::fmod(v[1], 2) != 0) return fnum(-std::pow(-v[0], 1 / v[1]));
        return fnum(std::pow(v[0], 1 / v[1]));
    });
    addMath(r, "logb", "<x> <base>", "logarithm of x in the given base", 2, 2, [](const std::vector<double>& v) {
        if (v[0] <= 0 || v[1] <= 0 || v[1] == 1) mfail("x > 0, base > 0 and base != 1 required");
        return fnum(std::log(v[0]) / std::log(v[1]));
    });
    addMath(r, "atan2", "<y> <x>", "angle of the point (x,y): radians and degrees", 2, 2, [](const std::vector<double>& v) {
        double a = std::atan2(v[0], v[1]); return fnum(a) + " rad   " + fnum(a * 180 / M_PI) + " deg";
    });
    addMath(r, "hypot", "<a> <b>", "sqrt(a^2 + b^2)", 2, 2, [](const std::vector<double>& v) { return fnum(std::hypot(v[0], v[1])); });
    addMath(r, "perm", "<n> <k>", "permutations nPk", 2, 2, [](const std::vector<double>& v) {
        long long n = asInt(v[0], "n"), k = asInt(v[1], "k");
        if (n < 0 || k < 0 || k > n) mfail("need 0 <= k <= n");
        double lg = 0; for (long long i = 0; i < k; ++i) lg += std::log(static_cast<double>(n - i));
        if (lg < 43.0) { unsigned long long p = 1; for (long long i = 0; i < k; ++i) p *= static_cast<unsigned long long>(n - i); return std::to_string(p); }
        return fnum(std::exp(lg), 15) + " (approx)";
    });
    addMath(r, "comb", "<n> <k>", "combinations nCk", 2, 2, [](const std::vector<double>& v) {
        long long n = asInt(v[0], "n"), k = asInt(v[1], "k");
        if (n < 0 || k < 0 || k > n) mfail("need 0 <= k <= n");
        k = std::min(k, n - k);
        unsigned __int128 c = 1;
        for (long long i = 1; i <= k; ++i) {
            c = c * static_cast<unsigned __int128>(n - k + i) / static_cast<unsigned __int128>(i);
            if (c > (static_cast<unsigned __int128>(1) << 100)) mfail("result too large");
        }
        std::string s; if (!c) s = "0";
        while (c) { s.insert(s.begin(), static_cast<char>('0' + static_cast<int>(c % 10))); c /= 10; }
        return s;
    });
    addMath(r, "lcm", "<numbers…>", "least common multiple", 0, 2, [](const std::vector<double>& v) {
        unsigned __int128 l = 1;
        for (double x : v) {
            long long n = std::llabs(asInt(x, "value"));
            if (n == 0) return std::string("0");
            l = l / static_cast<unsigned __int128>(gcdLL(static_cast<long long>(l > 0x7fffffffffffffffULL ? 1 : l), n)) * static_cast<unsigned __int128>(n);
            if (l > static_cast<unsigned __int128>(9000000000000000000ULL)) mfail("result too large");
        }
        return std::to_string(static_cast<unsigned long long>(l));
    });
    addMath(r, "clamp", "<x> <lo> <hi>", "limit x to lo..hi", 3, 3, [](const std::vector<double>& v) {
        if (v[1] > v[2]) mfail("lo must be <= hi");
        return fnum(std::min(std::max(v[0], v[1]), v[2]));
    });
    addMath(r, "lerp", "<a> <b> <t>", "linear interpolation a + (b-a)*t", 3, 3, [](const std::vector<double>& v) { return fnum(v[0] + (v[1] - v[0]) * v[2]); });
    addMath(r, "remap", "<x> <a> <b> <c> <d>", "map x from range a..b to range c..d", 5, 5, [](const std::vector<double>& v) {
        if (v[1] == v[2]) mfail("source range is empty");
        return fnum(v[3] + (v[0] - v[1]) * (v[4] - v[3]) / (v[2] - v[1]));
    });
    addMath(r, "roundto", "<x> <digits>", "round x to a number of decimal places", 2, 2, [](const std::vector<double>& v) {
        long long d = asInt(v[1], "digits");
        if (d < 0 || d > 15) mfail("digits must be 0..15");
        char b[64]; std::snprintf(b, sizeof(b), "%.*f", static_cast<int>(d), v[0]); return std::string(b);
    });
    addMath(r, "tobase", "<n> <base>", "write an integer in base 2..36", 2, 2, [](const std::vector<double>& v) {
        long long n = asInt(v[0], "n"), b = asInt(v[1], "base");
        if (b < 2 || b > 36) mfail("base must be 2..36");
        bool neg = n < 0; unsigned long long u = static_cast<unsigned long long>(neg ? -n : n);
        std::string s; if (!u) s = "0";
        while (u) { s.insert(s.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[u % static_cast<unsigned long long>(b)]); u /= static_cast<unsigned long long>(b); }
        return (neg ? "-" : "") + s;
    });
    addMath(r, "modpow", "<base> <exp> <mod>", "modular exponentiation base^exp mod m", 3, 3, [](const std::vector<double>& v) {
        long long b = asInt(v[0], "base"), e = asInt(v[1], "exp"), m = asInt(v[2], "mod");
        if (e < 0 || m < 1) mfail("need exp >= 0 and mod >= 1");
        unsigned __int128 res = 1 % m, x = static_cast<unsigned __int128>(((b % m) + m) % m);
        while (e) { if (e & 1) res = res * x % static_cast<unsigned __int128>(m); x = x * x % static_cast<unsigned __int128>(m); e >>= 1; }
        return std::to_string(static_cast<unsigned long long>(res));
    });
    addMath(r, "egcd", "<a> <b>", "extended Euclid: g and x,y with a*x + b*y = g", 2, 2, [](const std::vector<double>& v) {
        long long a = asInt(v[0], "a"), b = asInt(v[1], "b");
        long long x0 = 1, y0 = 0, x1 = 0, y1 = 1, aa = a, bb = b;
        while (bb) { long long q = aa / bb; long long t = aa - q * bb; aa = bb; bb = t; t = x0 - q * x1; x0 = x1; x1 = t; t = y0 - q * y1; y0 = y1; y1 = t; }
        if (aa < 0) { aa = -aa; x0 = -x0; y0 = -y0; }
        return "g = " + std::to_string(aa) + "   x = " + std::to_string(x0) + "   y = " + std::to_string(y0);
    });
    addMath(r, "modinv", "<a> <m>", "modular inverse of a modulo m", 2, 2, [](const std::vector<double>& v) {
        long long a = asInt(v[0], "a"), m = asInt(v[1], "m");
        if (m < 2) mfail("m must be >= 2");
        long long g = m, x = 0, y = 1, aa = ((a % m) + m) % m;
        long long r0 = m, r1 = aa, t0 = 0, t1 = 1;
        while (r1) { long long q = r0 / r1; long long t = r0 - q * r1; r0 = r1; r1 = t; t = t0 - q * t1; t0 = t1; t1 = t; }
        (void)g; (void)x; (void)y;
        if (r0 != 1) mfail("no inverse: gcd(a, m) = " + std::to_string(r0));
        return std::to_string(((t0 % m) + m) % m);
    });

    // --- algebra / vectors ------------------------------------------------------------------
    addMath(r, "solve2", "<a> <b> <c>", "roots of a*x^2 + b*x + c = 0 (complex roots supported)", 3, 3, [](const std::vector<double>& v) {
        double a = v[0], b = v[1], c = v[2];
        if (a == 0) { if (b == 0) mfail("no equation"); return "x = " + fnum(-c / b); }
        double d = b * b - 4 * a * c;
        if (d > 0) return "x1 = " + fnum((-b + std::sqrt(d)) / (2 * a)) + "   x2 = " + fnum((-b - std::sqrt(d)) / (2 * a));
        if (d == 0) return "x = " + fnum(-b / (2 * a)) + "  (double root)";
        return "x1 = " + fnum(-b / (2 * a)) + " + " + fnum(std::sqrt(-d) / (2 * std::fabs(a))) + "i   x2 = " + fnum(-b / (2 * a)) + " - " + fnum(std::sqrt(-d) / (2 * std::fabs(a))) + "i";
    });
    addMath(r, "solvelin", "<a b c d e f>", "solve  a*x + b*y = e  and  c*x + d*y = f", 6, 6, [](const std::vector<double>& v) {
        double det = v[0] * v[3] - v[1] * v[2];
        if (det == 0) mfail("no unique solution (determinant is 0)");
        return "x = " + fnum((v[4] * v[3] - v[1] * v[5]) / det) + "   y = " + fnum((v[0] * v[5] - v[4] * v[2]) / det);
    });
    addMath(r, "det", "<n*n numbers, row by row>", "determinant of a 2x2, 3x3 or 4x4 matrix", 0, 4, [](const std::vector<double>& v) {
        size_t n = 0; while (n * n < v.size()) ++n;
        if (n * n != v.size() || n > 4) mfail("give 4, 9 or 16 numbers (row by row)");
        std::vector<std::vector<double>> m(n, std::vector<double>(n));
        for (size_t i = 0; i < v.size(); ++i) m[i / n][i % n] = v[i];
        double det = 1;
        for (size_t c = 0; c < n; ++c) {
            size_t p = c; for (size_t i = c + 1; i < n; ++i) if (std::fabs(m[i][c]) > std::fabs(m[p][c])) p = i;
            if (m[p][c] == 0) return std::string("0");
            if (p != c) { std::swap(m[p], m[c]); det = -det; }
            det *= m[c][c];
            for (size_t i = c + 1; i < n; ++i) { double f = m[i][c] / m[c][c]; for (size_t j = c; j < n; ++j) m[i][j] -= f * m[c][j]; }
        }
        return fnum(std::round(det * 1e9) / 1e9);
    });
    addMath(r, "dot", "<a1 a2 … b1 b2 …>", "dot product (first half · second half)", 0, 2, [](const std::vector<double>& v) {
        if (v.size() % 2) mfail("give two vectors of equal length");
        size_t h = v.size() / 2; double s = 0; for (size_t i = 0; i < h; ++i) s += v[i] * v[h + i];
        return fnum(s);
    });
    addMath(r, "cross", "<ax ay az bx by bz>", "3-D cross product", 6, 6, [](const std::vector<double>& v) {
        return fnum(v[1] * v[5] - v[2] * v[4]) + " " + fnum(v[2] * v[3] - v[0] * v[5]) + " " + fnum(v[0] * v[4] - v[1] * v[3]);
    });
    addMath(r, "norm", "<components…>", "Euclidean length of a vector", 0, 1, [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x * x; return fnum(std::sqrt(s)); });
    addMath(r, "dist", "<a1 a2 … b1 b2 …>", "distance between two points (first half, second half)", 0, 2, [](const std::vector<double>& v) {
        if (v.size() % 2) mfail("give two points with the same number of coordinates");
        size_t h = v.size() / 2; double s = 0; for (size_t i = 0; i < h; ++i) s += (v[i] - v[h + i]) * (v[i] - v[h + i]);
        return fnum(std::sqrt(s));
    });

    // --- geometry -----------------------------------------------------------------------------
    addMath(r, "circle", "<radius>", "circumference and area of a circle", 1, 1, [](const std::vector<double>& v) {
        if (v[0] < 0) mfail("radius must be >= 0");
        return "circumference " + fnum(2 * M_PI * v[0]) + "   area " + fnum(M_PI * v[0] * v[0]);
    });
    addMath(r, "sphere", "<radius>", "surface area and volume of a sphere", 1, 1, [](const std::vector<double>& v) {
        if (v[0] < 0) mfail("radius must be >= 0");
        return "surface " + fnum(4 * M_PI * v[0] * v[0]) + "   volume " + fnum(4.0 / 3 * M_PI * v[0] * v[0] * v[0]);
    });
    addMath(r, "cylinder", "<radius> <height>", "surface area and volume of a cylinder", 2, 2, [](const std::vector<double>& v) {
        if (v[0] < 0 || v[1] < 0) mfail("radius and height must be >= 0");
        return "surface " + fnum(2 * M_PI * v[0] * (v[0] + v[1])) + "   volume " + fnum(M_PI * v[0] * v[0] * v[1]);
    });
    addMath(r, "cone", "<radius> <height>", "surface area and volume of a cone", 2, 2, [](const std::vector<double>& v) {
        if (v[0] < 0 || v[1] < 0) mfail("radius and height must be >= 0");
        double sl = std::hypot(v[0], v[1]);
        return "surface " + fnum(M_PI * v[0] * (v[0] + sl)) + "   volume " + fnum(M_PI * v[0] * v[0] * v[1] / 3);
    });
    addMath(r, "ellipse", "<a> <b>", "area and perimeter (Ramanujan) of an ellipse with semi-axes a, b", 2, 2, [](const std::vector<double>& v) {
        if (v[0] < 0 || v[1] < 0) mfail("axes must be >= 0");
        double a = v[0], b = v[1], h = (a - b) * (a - b) / ((a + b) * (a + b));
        return "area " + fnum(M_PI * a * b) + "   perimeter " + fnum(M_PI * (a + b) * (1 + 3 * h / (10 + std::sqrt(4 - 3 * h))));
    });
    addMath(r, "heron", "<a> <b> <c>", "area, perimeter and angles of a triangle from its three sides", 3, 3, [](const std::vector<double>& v) {
        double a = v[0], b = v[1], c = v[2];
        if (a <= 0 || b <= 0 || c <= 0 || a + b <= c || a + c <= b || b + c <= a) mfail("these sides do not form a triangle");
        double s = (a + b + c) / 2, area = std::sqrt(s * (s - a) * (s - b) * (s - c));
        auto ang = [](double x, double y, double z) { return std::acos((y * y + z * z - x * x) / (2 * y * z)) * 180 / M_PI; };
        return "area " + fnum(area) + "   perimeter " + fnum(a + b + c) + "   angles " + fnum(ang(a, b, c), 6) + " " + fnum(ang(b, a, c), 6) + " " + fnum(ang(c, a, b), 6);
    });
    addMath(r, "pyth", "<a> <b>", "hypotenuse from legs, and the angles", 2, 2, [](const std::vector<double>& v) {
        if (v[0] <= 0 || v[1] <= 0) mfail("legs must be > 0");
        return "hypotenuse " + fnum(std::hypot(v[0], v[1])) + "   angles " + fnum(std::atan2(v[0], v[1]) * 180 / M_PI, 8) + " " + fnum(std::atan2(v[1], v[0]) * 180 / M_PI, 8);
    });
    addMath(r, "polygon", "<sides> <side-length>", "area, perimeter and angles of a regular polygon", 2, 2, [](const std::vector<double>& v) {
        long long n = asInt(v[0], "sides");
        if (n < 3 || v[1] <= 0) mfail("need sides >= 3 and length > 0");
        double nn = static_cast<double>(n);
        return "area " + fnum(nn * v[1] * v[1] / (4 * std::tan(M_PI / nn))) + "   perimeter " + fnum(nn * v[1]) + "   interior angle " + fnum((nn - 2) * 180 / nn, 10);
    });

    // --- finance / percentages ------------------------------------------------------------------
    addMath(r, "percent", "<p> <x>", "p percent of x", 2, 2, [](const std::vector<double>& v) { return fnum(v[0] / 100 * v[1]); });
    addMath(r, "pctof", "<a> <b>", "a as a percentage of b", 2, 2, [](const std::vector<double>& v) {
        if (v[1] == 0) mfail("b must not be 0");
        return fnum(v[0] / v[1] * 100) + "%";
    });
    addMath(r, "pctchange", "<old> <new>", "percentage change from old to new", 2, 2, [](const std::vector<double>& v) {
        if (v[0] == 0) mfail("old value must not be 0");
        return fnum((v[1] - v[0]) / std::fabs(v[0]) * 100) + "%";
    });
    addMath(r, "discount", "<price> <percent>", "price after a percentage discount", 2, 2, [](const std::vector<double>& v) {
        return "pay " + money(v[0] * (1 - v[1] / 100)) + "   save " + money(v[0] * v[1] / 100);
    });
    addMath(r, "tip", "<bill> <percent> [people]", "tip, total and per-person share", 0, 2, [](const std::vector<double>& v) {
        if (v.size() > 3) mfail("usage: $ tip <bill> <percent> [people]");
        double ppl = v.size() == 3 ? v[2] : 1;
        if (ppl < 1) mfail("people must be >= 1");
        double t = v[0] * v[1] / 100;
        return "tip " + money(t) + "   total " + money(v[0] + t) + "   each " + money((v[0] + t) / ppl);
    });
    addMath(r, "vat", "<net-price> <rate-percent>", "VAT amount and gross price from a net price", 2, 2, [](const std::vector<double>& v) {
        return "vat " + money(v[0] * v[1] / 100) + "   gross " + money(v[0] * (1 + v[1] / 100));
    });
    addMath(r, "unvat", "<gross-price> <rate-percent>", "net price and VAT amount from a gross price", 2, 2, [](const std::vector<double>& v) {
        double net = v[0] / (1 + v[1] / 100);
        return "net " + money(net) + "   vat " + money(v[0] - net);
    });
    addMath(r, "compound", "<principal> <rate%> <years> [per-year]", "compound interest (default: yearly)", 0, 3, [](const std::vector<double>& v) {
        if (v.size() > 4) mfail("usage: $ compound <principal> <rate%> <years> [per-year]");
        double n = v.size() == 4 ? v[3] : 1;
        if (n < 1) mfail("per-year must be >= 1");
        double fv = v[0] * std::pow(1 + v[1] / 100 / n, n * v[2]);
        return "final " + money(fv) + "   interest " + money(fv - v[0]);
    });
    addMath(r, "loan", "<principal> <annual-rate%> <years>", "monthly payment, total paid and total interest", 3, 3, [](const std::vector<double>& v) {
        double n = v[2] * 12, i = v[1] / 100 / 12;
        if (n < 1 || v[0] <= 0 || v[1] < 0) mfail("need principal > 0, rate >= 0, years >= 1/12");
        double pay = i == 0 ? v[0] / n : v[0] * i / (1 - std::pow(1 + i, -n));
        return "monthly " + money(pay) + "   total " + money(pay * n) + "   interest " + money(pay * n - v[0]);
    });
    addMath(r, "cagr", "<start> <end> <years>", "compound annual growth rate", 3, 3, [](const std::vector<double>& v) {
        if (v[0] <= 0 || v[1] <= 0 || v[2] <= 0) mfail("all values must be > 0");
        return fnum((std::pow(v[1] / v[0], 1 / v[2]) - 1) * 100, 8) + "% per year";
    });
    addMath(r, "roi", "<cost> <final-value>", "return on investment", 2, 2, [](const std::vector<double>& v) {
        if (v[0] == 0) mfail("cost must not be 0");
        return "profit " + money(v[1] - v[0]) + "   roi " + fnum((v[1] - v[0]) / v[0] * 100, 8) + "%";
    });
    addMath(r, "rule72", "<rate%>", "years to double an investment (rule of 72, and exact)", 1, 1, [](const std::vector<double>& v) {
        if (v[0] <= 0) mfail("rate must be > 0");
        return "about " + fnum(72 / v[0], 6) + " years   (exact " + fnum(std::log(2) / std::log(1 + v[0] / 100), 6) + ")";
    });
    addMath(r, "breakeven", "<fixed-cost> <price-per-unit> <cost-per-unit>", "units to sell to break even", 3, 3, [](const std::vector<double>& v) {
        if (v[1] <= v[2]) mfail("price must be greater than unit cost");
        double u = v[0] / (v[1] - v[2]);
        return fnum(std::ceil(u)) + " units   (exact " + fnum(u, 8) + ")";
    });

    // --- constants + RPN -----------------------------------------------------------------------------
    r.add("const", "$ const [name]", "physical and mathematical constants (no name: list them all)", false, [](Ctx& c) -> int {
        static const struct { const char* n; const char* v; const char* d; } T[] = {
            {"pi", "3.14159265358979323846", "ratio of circumference to diameter"}, {"e", "2.71828182845904523536", "Euler's number"},
            {"phi", "1.61803398874989484820", "golden ratio"}, {"sqrt2", "1.41421356237309504880", "square root of 2"},
            {"ln2", "0.69314718055994530942", "natural log of 2"}, {"euler", "0.57721566490153286061", "Euler-Mascheroni constant"},
            {"c", "299792458", "speed of light, m/s"}, {"g", "9.80665", "standard gravity, m/s^2"},
            {"G", "6.67430e-11", "gravitational constant, m^3 kg^-1 s^-2"}, {"h", "6.62607015e-34", "Planck constant, J s"},
            {"k", "1.380649e-23", "Boltzmann constant, J/K"}, {"NA", "6.02214076e23", "Avogadro constant, 1/mol"},
            {"R", "8.314462618", "gas constant, J/(mol K)"}, {"qe", "1.602176634e-19", "elementary charge, C"},
            {"me", "9.1093837015e-31", "electron mass, kg"}, {"mp", "1.67262192369e-27", "proton mass, kg"},
            {"eps0", "8.8541878128e-12", "vacuum permittivity, F/m"}, {"sigma", "5.670374419e-8", "Stefan-Boltzmann constant, W m^-2 K^-4"},
            {"au", "149597870700", "astronomical unit, m"}, {"ly", "9460730472580800", "light year, m"},
        };
        if (!c.parse("", "")) return 1;
        if (c.pos.empty()) { for (auto& t : T) { char b[256]; std::snprintf(b, sizeof(b), "%-6s %-24s %s", t.n, t.v, t.d); c.say(b); } return 0; }
        int rc = 0;
        for (auto& n : c.pos) {
            bool found = false;
            for (auto& t : T) if (n == t.n) { c.say(std::string(t.n) + " = " + t.v + "   (" + t.d + ")"); found = true; }
            if (!found) { c.warn("unknown constant: " + n + "   (run $ const for the list)"); rc = 1; }
        }
        return rc;
    });
    r.add("rpn", "$ rpn <tokens…>", "reverse-Polish calculator:  $ rpn 3 4 + 2 *   (+ - * / ^ % sqrt neg abs dup swap pi e)", false, [](Ctx& c) -> int {
        std::vector<double> st;
        auto need = [&](size_t n) { if (st.size() < n) mfail("stack underflow"); };
        try {
            if (c.toks.empty()) mfail("usage: $ rpn <tokens…>   e.g. $ rpn 3 4 + 2 *");
            for (auto& tk : c.toks) {
                const std::string& t = tk.s; double d;
                if (t == "+" || t == "-" || t == "*" || t == "x" || t == "/" || t == "^" || t == "%") {
                    need(2); double b = st.back(); st.pop_back(); double a = st.back(); st.pop_back();
                    if (t == "+") a += b; else if (t == "-") a -= b; else if (t == "*" || t == "x") a *= b;
                    else if (t == "/") { if (b == 0) mfail("division by zero"); a /= b; }
                    else if (t == "%") { if (b == 0) mfail("division by zero"); a = std::fmod(a, b); }
                    else a = std::pow(a, b);
                    st.push_back(a);
                } else if (t == "sqrt") { need(1); if (st.back() < 0) mfail("sqrt of a negative number"); st.back() = std::sqrt(st.back()); }
                else if (t == "neg") { need(1); st.back() = -st.back(); }
                else if (t == "abs") { need(1); st.back() = std::fabs(st.back()); }
                else if (t == "dup") { need(1); st.push_back(st.back()); }
                else if (t == "swap") { need(2); std::swap(st[st.size() - 1], st[st.size() - 2]); }
                else if (t == "pi") st.push_back(M_PI);
                else if (t == "e") st.push_back(M_E);
                else if (parseNum(t, d)) st.push_back(d);
                else mfail("unknown token: " + t);
            }
            if (st.size() != 1) mfail("expression left " + std::to_string(st.size()) + " values on the stack");
        } catch (const MathErr& e) { return c.fail(e.what()); }
        c.say(fnum(st[0]));
        return 0;
    });
    r.add("frombase", "$ frombase <digits> <base>", "read digits written in base 2..36 as a decimal integer", false, [](Ctx& c) -> int {
        if (c.toks.size() != 2) return c.fail("usage: $ frombase <digits> <base>   e.g. $ frombase ff 16");
        char* e = nullptr; long b = std::strtol(c.toks[1].s.c_str(), &e, 10);
        if (!e || *e || b < 2 || b > 36) return c.fail("base must be 2..36");
        std::string s = c.toks[0].s; bool neg = !s.empty() && s[0] == '-'; if (neg) s.erase(0, 1);
        if (s.empty()) return c.fail("no digits");
        unsigned __int128 v = 0;
        for (char ch : s) {
            int dv = std::isdigit(static_cast<unsigned char>(ch)) ? ch - '0' : (std::isalpha(static_cast<unsigned char>(ch)) ? std::tolower(ch) - 'a' + 10 : 99);
            if (dv >= b) return c.fail(std::string("invalid digit '") + ch + "' for base " + std::to_string(b));
            v = v * static_cast<unsigned __int128>(b) + static_cast<unsigned __int128>(dv);
            if (v > (static_cast<unsigned __int128>(1) << 100)) return c.fail("number too large");
        }
        std::string o; if (!v) o = "0";
        while (v) { o.insert(o.begin(), static_cast<char>('0' + static_cast<int>(v % 10))); v /= 10; }
        c.say((neg && o != "0" ? "-" : "") + o);
        return 0;
    });
}

// ----------------------------------------------------------------------------
// C. dates (proleptic Gregorian; Hinnant's civil-from-days)
// ----------------------------------------------------------------------------
struct Ymd { long y; int m, d; };
inline long daysFromCivil(long y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline Ymd civilFromDays(long z) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    return {y + (m <= 2), m, d};
}
inline bool leapYear(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int monthDays(long y, int m) { static const int t[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; return m == 2 && leapYear(y) ? 29 : t[m - 1]; }
inline long todayDays() { std::time_t t = std::time(nullptr); std::tm lt{}; localtime_r(&t, &lt); return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday); }
inline bool parseDate(const std::string& s, long& days, std::string& err) {
    if (s == "today") { days = todayDays(); return true; }
    if (s == "tomorrow") { days = todayDays() + 1; return true; }
    if (s == "yesterday") { days = todayDays() - 1; return true; }
    long y; int m, d; char x = 0;
    if (std::sscanf(s.c_str(), "%ld-%d-%d%c", &y, &m, &d, &x) != 3 || m < 1 || m > 12 || d < 1 || d > monthDays(y, m)) {
        err = "bad date '" + s + "'   (use YYYY-MM-DD, today, tomorrow or yesterday)"; return false;
    }
    days = daysFromCivil(y, m, d);
    return true;
}
inline int weekdayOf(long days) { long w = (days + 4) % 7; if (w < 0) w += 7; return static_cast<int>(w); }     // 0 = Sunday
inline const char* dayName(int w) { static const char* n[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}; return n[w]; }
inline const char* monthName(int m) { static const char* n[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}; return n[m - 1]; }
inline std::string ymdStr(long days) { Ymd c = civilFromDays(days); char b[48]; std::snprintf(b, sizeof(b), "%04ld-%02d-%02d", c.y, c.m, c.d); return b; }
inline std::string dateLine(long days) { return ymdStr(days) + " " + dayName(weekdayOf(days)); }
inline void isoWeek(long days, long& isoYear, int& week) {
    int wd = weekdayOf(days); if (wd == 0) wd = 7;
    long thu = days + (4 - wd);                               // Thursday of this ISO week decides the year
    Ymd c = civilFromDays(thu);
    isoYear = c.y;
    week = static_cast<int>((thu - daysFromCivil(c.y, 1, 1)) / 7 + 1);
}
inline std::string humanDur(long long s) {
    if (s == 0) return "0s";
    std::string o; bool neg = s < 0; if (neg) s = -s;
    long long d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60, x = s % 60;
    if (d) o += std::to_string(d) + "d ";
    if (h) o += std::to_string(h) + "h ";
    if (m) o += std::to_string(m) + "m ";
    if (x) o += std::to_string(x) + "s ";
    o.pop_back();
    return (neg ? "-" : "") + o;
}
// Islamic tabular (civil) calendar, Fourmilab algorithm. JD values carry the .5 of astronomical JD.
inline double islamicToJd(long y, int m, int d) { return d + std::ceil(29.5 * (m - 1)) + (y - 1) * 354 + std::floor((3 + 11.0 * y) / 30) + 1948439.5 - 1; }
inline Ymd jdToIslamic(double jd) {
    jd = std::floor(jd) + 0.5;
    long y = static_cast<long>(std::floor((30 * (jd - 1948439.5) + 10646) / 10631));
    int m = static_cast<int>(std::min(12.0, std::ceil((jd - (29 + islamicToJd(y, 1, 1))) / 29.5) + 1));
    int d = static_cast<int>(jd - islamicToJd(y, m, 1) + 1);
    return {y, m, d};
}
inline double daysToJd(long days) { return static_cast<double>(days) + 2440587.5; }
inline const char* hijriMonth(int m) {
    static const char* n[] = {"Muharram", "Safar", "Rabi' al-awwal", "Rabi' al-thani", "Jumada al-awwal", "Jumada al-thani", "Rajab", "Sha'ban", "Ramadan", "Shawwal", "Dhu al-Qi'dah", "Dhu al-Hijjah"};
    return n[m - 1];
}

// date commands share one wrapper: positional words in, one text out, errors via MathErr
using StrFn = std::function<std::string(const std::vector<std::string>&)>;
inline void addDate(Reg& r, const std::string& name, const std::string& hint, const std::string& desc, size_t minArgs, size_t maxArgs, StrFn f) {
    r.add(name, "$ " + name + " " + hint, desc, false, [f, name, hint, minArgs, maxArgs](Ctx& c) -> int {
        std::vector<std::string> a;
        for (auto& t : c.toks) a.push_back(t.s);
        if (a.size() < minArgs || a.size() > maxArgs) return c.fail("usage: $ " + name + " " + hint);
        try { c.say(f(a)); } catch (const MathErr& e) { return c.fail(e.what()); }
        return 0;
    });
}
inline long needDate(const std::vector<std::string>& a, size_t i) {
    long d; std::string err;
    if (i >= a.size()) return todayDays();
    if (!parseDate(a[i], d, err)) mfail(err);
    return d;
}
inline long needLong(const std::string& s, const char* what) {
    char* e = nullptr; long v = std::strtol(s.c_str(), &e, 10);
    if (s.empty() || !e || *e) mfail(std::string(what) + " must be an integer: '" + s + "'");
    return v;
}

inline void addDateCmds(Reg& r) {
    addDate(r, "now", "[utc]", "current local (or UTC) date and time, ISO 8601", 0, 1, [](const std::vector<std::string>& a) {
        std::time_t t = std::time(nullptr); std::tm tm{};
        bool utc = !a.empty() && a[0] == "utc";
        if (!a.empty() && !utc) mfail("usage: $ now [utc]");
        if (utc) gmtime_r(&t, &tm); else localtime_r(&t, &tm);
        char b[64]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &tm);
        std::string z = "Z";
        if (!utc) { char o[16]; std::strftime(o, sizeof(o), "%z", &tm); z = std::string(o).insert(3, ":"); }
        return std::string(b) + z;
    });
    addDate(r, "epoch", "<YYYY-MM-DD> [HH:MM:SS]", "Unix time (UTC) of a date", 1, 2, [](const std::vector<std::string>& a) {
        long d = needDate(a, 0); long secs = 0;
        if (a.size() == 2) { int h, m, s = 0; char x = 0; if (std::sscanf(a[1].c_str(), "%d:%d:%d%c", &h, &m, &s, &x) < 2 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 60) mfail("bad time '" + a[1] + "'"); secs = h * 3600L + m * 60 + s; }
        return std::to_string(d * 86400L + secs);
    });
    addDate(r, "fromepoch", "<seconds>", "Unix time to a UTC date and time", 1, 1, [](const std::vector<std::string>& a) {
        long long t = needLong(a[0], "seconds"); long long d = t >= 0 ? t / 86400 : -((-t + 86399) / 86400); long long s = t - d * 86400;
        char b[32]; std::snprintf(b, sizeof(b), " %02d:%02d:%02d UTC", static_cast<int>(s / 3600), static_cast<int>(s % 3600 / 60), static_cast<int>(s % 60));
        return dateLine(static_cast<long>(d)) + b;
    });
    addDate(r, "isleap", "<year…>", "is it a leap year?", 1, 64, [](const std::vector<std::string>& a) {
        std::string o; for (auto& y : a) { long v = needLong(y, "year"); if (!o.empty()) o += '\n'; o += y + (leapYear(v) ? "  leap year" : "  common year"); } return o;
    });
    addDate(r, "dayofweek", "[date]", "weekday of a date (default today)", 0, 1, [](const std::vector<std::string>& a) { return dateLine(needDate(a, 0)); });
    addDate(r, "dayofyear", "[date]", "day number within the year, and days left", 0, 1, [](const std::vector<std::string>& a) {
        long d = needDate(a, 0); Ymd c = civilFromDays(d); long n = d - daysFromCivil(c.y, 1, 1) + 1; long total = leapYear(c.y) ? 366 : 365;
        return "day " + std::to_string(n) + " of " + std::to_string(total) + "   (" + std::to_string(total - n) + " left)";
    });
    addDate(r, "weekno", "[date]", "ISO 8601 week number", 0, 1, [](const std::vector<std::string>& a) {
        long y; int w; isoWeek(needDate(a, 0), y, w); char b[32]; std::snprintf(b, sizeof(b), "%ld-W%02d", y, w); return std::string(b);
    });
    addDate(r, "daysinmonth", "<year> <month>", "number of days in a month", 2, 2, [](const std::vector<std::string>& a) {
        long y = needLong(a[0], "year"), m = needLong(a[1], "month"); if (m < 1 || m > 12) mfail("month must be 1..12");
        return std::string(monthName(static_cast<int>(m))) + " " + std::to_string(y) + " has " + std::to_string(monthDays(y, static_cast<int>(m))) + " days";
    });
    addDate(r, "addays", "<date> <days>", "add (or subtract, with -) days to a date", 2, 2, [](const std::vector<std::string>& a) { return dateLine(needDate(a, 0) + needLong(a[1], "days")); });
    addDate(r, "addmonths", "<date> <months>", "add (or subtract) months; the day is clamped to the month length", 2, 2, [](const std::vector<std::string>& a) {
        Ymd c = civilFromDays(needDate(a, 0)); long n = needLong(a[1], "months");
        long t = c.y * 12 + (c.m - 1) + n; long y = t >= 0 ? t / 12 : -((-t + 11) / 12); int m = static_cast<int>(t - y * 12) + 1;
        return dateLine(daysFromCivil(y, m, std::min(c.d, monthDays(y, m))));
    });
    addDate(r, "age", "<birth-date> [on-date]", "age in years, months and days, plus total days lived", 1, 2, [](const std::vector<std::string>& a) {
        long b = needDate(a, 0), t = needDate(a, 1); if (b > t) mfail("birth date is after the reference date");
        Ymd x = civilFromDays(b), y = civilFromDays(t);
        long yy = y.y - x.y; int mm = y.m - x.m, dd = y.d - x.d;
        if (dd < 0) { --mm; int pm = y.m == 1 ? 12 : y.m - 1; dd += monthDays(pm == 12 ? y.y - 1 : y.y, pm); }
        if (mm < 0) { --yy; mm += 12; }
        return std::to_string(yy) + " years, " + std::to_string(mm) + " months, " + std::to_string(dd) + " days   (" + std::to_string(t - b) + " days)";
    });
    addDate(r, "countdown", "<date>", "days from today to a date", 1, 1, [](const std::vector<std::string>& a) {
        long n = needDate(a, 0) - todayDays();
        if (n == 0) return std::string("that is today");
        return std::to_string(std::labs(n)) + (std::labs(n) == 1 ? " day " : " days ") + (n > 0 ? "from now" : "ago") + "   (" + std::to_string(std::labs(n) / 7) + " weeks, " + std::to_string(std::labs(n) % 7) + " days)";
    });
    addDate(r, "workdays", "<from> <to>", "Monday-Friday days between two dates (inclusive of start, exclusive of end)", 2, 2, [](const std::vector<std::string>& a) {
        long x = needDate(a, 0), y = needDate(a, 1); bool neg = x > y; if (neg) std::swap(x, y);
        long n = 0; for (long d = x; d < y; ++d) { int w = weekdayOf(d); if (w != 0 && w != 6) ++n; }
        return (neg ? "-" : "") + std::to_string(n) + " working days   (" + std::to_string(y - x) + " calendar days)";
    });
    addDate(r, "easter", "<year…>", "Gregorian Easter Sunday", 1, 32, [](const std::vector<std::string>& a) {
        std::string o;
        for (auto& s : a) {
            long y = needLong(s, "year"); if (y < 1583) mfail("year must be >= 1583 (Gregorian calendar)");
            long A = y % 19, B = y / 100, C = y % 100, D = B / 4, E = B % 4, F = (B + 8) / 25, G = (B - F + 1) / 3, H = (19 * A + B - D - G + 15) % 30, I = C / 4, K = C % 4, L = (32 + 2 * E + 2 * I - H - K) % 7, M = (A + 11 * H + 22 * L) / 451;
            long mo = (H + L - 7 * M + 114) / 31, da = (H + L - 7 * M + 114) % 31 + 1;
            if (!o.empty()) o += '\n';
            o += dateLine(daysFromCivil(y, static_cast<int>(mo), static_cast<int>(da)));
        }
        return o;
    });
    addDate(r, "hijri", "[date]", "Gregorian date to the Islamic civil (tabular) calendar", 0, 1, [](const std::vector<std::string>& a) {
        Ymd h = jdToIslamic(daysToJd(needDate(a, 0)));
        char b[96]; std::snprintf(b, sizeof(b), "%ld-%02d-%02d AH   %d %s %ld", h.y, h.m, h.d, h.d, hijriMonth(h.m), h.y); return std::string(b);
    });
    addDate(r, "fromhijri", "<YYYY-MM-DD hijri>", "Islamic civil (tabular) date to Gregorian", 1, 1, [](const std::vector<std::string>& a) {
        long y; int m, d; char x = 0;
        if (std::sscanf(a[0].c_str(), "%ld-%d-%d%c", &y, &m, &d, &x) != 3 || m < 1 || m > 12 || d < 1 || d > 30 || y < 1) mfail("bad hijri date '" + a[0] + "'   (use YYYY-MM-DD, e.g. 1445-09-01)");
        return dateLine(static_cast<long>(std::floor(islamicToJd(y, m, d) - 2440587.5 + 0.5)));
    });
    addDate(r, "quarter", "[date]", "calendar quarter and its first and last day", 0, 1, [](const std::vector<std::string>& a) {
        Ymd c = civilFromDays(needDate(a, 0)); int q = (c.m - 1) / 3 + 1; int fm = (q - 1) * 3 + 1;
        return "Q" + std::to_string(q) + " " + std::to_string(c.y) + "   " + ymdStr(daysFromCivil(c.y, fm, 1)) + " .. " + ymdStr(daysFromCivil(c.y, fm + 2, monthDays(c.y, fm + 2)));
    });
    addDate(r, "daysleft", "[date]", "days remaining in the year and in the month", 0, 1, [](const std::vector<std::string>& a) {
        long d = needDate(a, 0); Ymd c = civilFromDays(d);
        return std::to_string(daysFromCivil(c.y, 12, 31) - d) + " days left in " + std::to_string(c.y) + ", " + std::to_string(monthDays(c.y, c.m) - c.d) + " in " + monthName(c.m);
    });
    addDate(r, "datefmt", "<date> <format>", "format a date with strftime codes, e.g. $ datefmt today '%A %d %B %Y'", 2, 2, [](const std::vector<std::string>& a) {
        long d = needDate(a, 0); Ymd c = civilFromDays(d);
        std::tm tm{}; tm.tm_year = static_cast<int>(c.y - 1900); tm.tm_mon = c.m - 1; tm.tm_mday = c.d; tm.tm_wday = weekdayOf(d); tm.tm_yday = static_cast<int>(d - daysFromCivil(c.y, 1, 1));
        char b[256]; size_t n = std::strftime(b, sizeof(b), a[1].c_str(), &tm);
        if (!n) mfail("empty result (bad format or too long)");
        return std::string(b, n);
    });
    addDate(r, "zodiac", "<date>", "Western zodiac sign of a birth date", 1, 1, [](const std::vector<std::string>& a) {
        Ymd c = civilFromDays(needDate(a, 0)); int k = c.m * 100 + c.d;
        static const struct { int from; const char* n; } Z[] = {{120, "Aquarius"}, {219, "Pisces"}, {321, "Aries"}, {420, "Taurus"}, {521, "Gemini"}, {621, "Cancer"}, {723, "Leo"}, {823, "Virgo"}, {923, "Libra"}, {1023, "Scorpio"}, {1122, "Sagittarius"}, {1222, "Capricorn"}};
        const char* s = "Capricorn"; for (auto& z : Z) if (k >= z.from) s = z.n;
        return std::string(s);
    });
    addDate(r, "duration", "<seconds…>", "seconds as days/hours/minutes/seconds", 1, 64, [](const std::vector<std::string>& a) {
        std::string o; for (auto& s : a) { if (!o.empty()) o += '\n'; o += humanDur(needLong(s, "seconds")); } return o;
    });
    addDate(r, "parsedur", "<text>", "human duration to seconds:  $ parsedur 1d 2h 30m 5s", 1, 32, [](const std::vector<std::string>& a) {
        long long total = 0;
        for (auto& w : a) {
            size_t i = 0; bool any = false;
            while (i < w.size()) {
                size_t j = i; while (j < w.size() && (std::isdigit(static_cast<unsigned char>(w[j])) || w[j] == '.')) ++j;
                if (j == i || j == w.size()) mfail("cannot read '" + w + "'   (examples: 90m, 1h30m, 2d 4h)");
                double v = std::strtod(w.substr(i, j - i).c_str(), nullptr);
                char u = w[j++]; double mul = u == 'w' ? 604800 : u == 'd' ? 86400 : u == 'h' ? 3600 : u == 'm' ? 60 : u == 's' ? 1 : -1;
                if (mul < 0) mfail(std::string("unknown unit '") + u + "' (use w d h m s)");
                total += static_cast<long long>(std::llround(v * mul)); i = j; any = true;
            }
            if (!any) mfail("empty duration");
        }
        return std::to_string(total) + "  (" + humanDur(total) + ")";
    });
    addDate(r, "moonphase", "[date]", "moon phase and illumination (approximate, mean synodic month)", 0, 1, [](const std::vector<std::string>& a) {
        double jd = daysToJd(needDate(a, 0)) + 0.5;                               // noon UTC
        const double P = 29.530588853;
        double age = std::fmod(jd - 2451550.1, P); if (age < 0) age += P;
        double ill = (1 - std::cos(2 * M_PI * age / P)) / 2 * 100;
        const char* nm = age < 1.85 ? "New Moon" : age < 5.54 ? "Waxing Crescent" : age < 9.23 ? "First Quarter" : age < 12.92 ? "Waxing Gibbous" : age < 16.61 ? "Full Moon" : age < 20.30 ? "Waning Gibbous" : age < 23.99 ? "Last Quarter" : age < 27.68 ? "Waning Crescent" : "New Moon";
        char b[96]; std::snprintf(b, sizeof(b), "%s   age %.1f days   illumination %.0f%%", nm, age, ill); return std::string(b);
    });
}

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin
