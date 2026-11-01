// ============================================================================
//  rin_extra_natives10.cpp — مفهوم `linalg`: الجبر الخطي للمهندسين (mat* / vec*)
// ----------------------------------------------------------------------------
//  دفعة عاشرة فوق rin_extra_natives*.cpp دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra10() وتُضمَّن (#include) في نهاية rin_interpreter.cpp
//  (كالدفعتين الثامنة والتاسعة) فتدخل كل أهداف البناء (APK / CLI / WASM) بلا تعديل في ملفات البناء.
//  الشرح الكامل والأمثلة: docs/LINALG.md
//
//  الدمج لا الإنشاء من الصفر:
//    • التمثيل هو المصفوفة العادية في Rin (مصفوفة صفوف) — نفس ما تستعمله lib/matrix.og.rin
//      (mat_*) وlib/math.og.rin (matrixFrom/solveLinear) وlib/rinml.og.rin (nd_*)؛ فلا تحويل ولا نوع جديد.
//    • الجمع المُعوَّض (Neumaier) والتساوي التقريبي (rel=1e-9, abs=1e-12) والتنسيق هي نفسها
//      numberSum / numberApprox / numberFormat من مفهوم number (rin_extra_natives9.cpp):
//      vecSum/matTrace يستعملان neumaier9، matApprox يطابق دلالة numberApprox، matFormat يستدعي numberFormat.
//    • الأخطاء بنفس نظام التشخيص (E0004/E0007/E0035) قابلة للالتقاط عبر try/catch.
//
//  الإنشاء والفحص:
//    matZeros(r[,c]) matOnes(r[,c]) matEye(n[,m]) matDiag(v|M) matBuild(r,c,fn(i,j))
//    matRandom(r,c[,{seed,min,max,normal,int}]) matHilbert(n) matVandermonde(v[,cols])
//    matShape(M) matCheck(M) matIs(M, "symmetric"|"orthogonal"|...[,tol])
//  العمليات:
//    matAdd matSub matScale matHadamard matMul(...) matTranspose matTrace matPow(M,k) matKron matMap
//    matNorm(M[,kind]) matApprox(A,B[,rel[,abs]]) matFormat(M[,opts])
//  التحليل والحل:
//    matLU matQR matCholesky matEigSym matEigvals matSVD matSolve matInv matDet matRank matCond
//    matPinv matLstsq matNull matRref matSolveIter matPowerIter
//  المتجهات:
//    vecDot vecNorm vecCross vecNormalize vecAngle vecProject vecAdd vecSub vecScale vecDist vecLinspace vecSum
//
//  القواعد المشتركة: كلها نقية (لا تعدّل مدخلاتها)؛ المصفوفة صفوفٌ مستطيلة من أرقام؛ الفهارس تبدأ من 0؛
//  المتجه مصفوفة أرقام عادية؛ العمليات الحدية (مصفوفة منفردة، عدم تطابق الأبعاد، عدم التقارب) تُرمى
//  بخطأ واضح يذكر الدالة والبُعد/الصفّ بدل نتيجة خاطئة صامتة.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>

namespace rin {
namespace extra10 {

using Args = std::vector<Value>;
static const double kEps10 = std::numeric_limits<double>::epsilon();

// ------------------------------------------------------------------ أخطاء ومساعدات القيم
[[noreturn]] static void failType10(const std::string& fn, const std::string& msg, int line) {
    throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "': " + msg);
}
[[noreturn]] static void failArg10(const std::string& fn, const std::string& msg, int line) {
    throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "': " + msg);
}
[[noreturn]] static void failRun10(const std::string& fn, const std::string& msg, int line) {
    throw diagErr(diag::Code::E0035_RuntimeError, line, "'" + fn + "': " + msg);
}

static Value S10(const std::string& s) { return Value::string(s); }
static Value N10(double n) { return Value::num(n); }
static Value B10(bool b) { return Value::boolean_(b); }
static void put10(const std::shared_ptr<MapData>& m, const char* k, Value v) { m->push_back({S10(k), std::move(v)}); }
static const Value* get10(const Value& m, const char* key) {
    if (m.type != Value::Type::MAP || !m.map) return nullptr;
    return m.map->findValue(Value::string(key));
}
static double optNum10(const Value* opts, const char* key, double def, const char* fn, int line) {
    if (!opts) return def;
    const Value* v = get10(*opts, key);
    if (!v || v->type == Value::Type::NIL) return def;
    if (v->type != Value::Type::NUMBER) failType10(fn, std::string("option '") + key + "' expects a number but got " + v->typeName(), line);
    return v->number;
}
static bool optBool10(const Value* opts, const char* key, bool def) {
    if (!opts) return def;
    const Value* v = get10(*opts, key);
    return (v && v->type != Value::Type::NIL) ? v->isTruthy() : def;
}
static std::string optStr10(const Value* opts, const char* key, const std::string& def) {
    if (!opts) return def;
    const Value* v = get10(*opts, key);
    return (v && v->type == Value::Type::STRING) ? v->str : def;
}
static const Value* optMap10(const Args& a, size_t idx, const char* fn, int line) {
    if (a.size() <= idx || a[idx].type == Value::Type::NIL) return nullptr;
    if (a[idx].type != Value::Type::MAP) failType10(fn, "options must be a map but got " + a[idx].typeName(), line);
    return &a[idx];
}

// ------------------------------------------------------------------ المصفوفة الداخلية
struct Mat {
    size_t r = 0, c = 0;
    std::vector<double> d;
    Mat() = default;
    Mat(size_t rows, size_t cols, double v = 0.0) : r(rows), c(cols), d(rows * cols, v) {}
    double& operator()(size_t i, size_t j) { return d[i * c + j]; }
    double operator()(size_t i, size_t j) const { return d[i * c + j]; }
};

static Value vecVal(const std::vector<double>& v) {
    auto arr = std::make_shared<ArrayData>();
    arr->reserve(v.size());
    for (double x : v) arr->push_back(N10(x));
    return Value::makeArray(arr);
}
static Value matVal(const Mat& m) {
    auto rows = std::make_shared<ArrayData>();
    rows->reserve(m.r);
    for (size_t i = 0; i < m.r; ++i) {
        auto row = std::make_shared<ArrayData>();
        row->reserve(m.c);
        for (size_t j = 0; j < m.c; ++j) row->push_back(N10(m(i, j)));
        rows->push_back(Value::makeArray(row));
    }
    return Value::makeArray(rows);
}

static bool looksMatrix10(const Value& v) {
    return v.type == Value::Type::ARRAY && v.array && !v.array->empty() &&
           (*v.array)[0].type == Value::Type::ARRAY;
}

// يقرأ مصفوفة مستطيلة غير فارغة من أرقام
static Mat readMat10(const Value& v, const char* fn, const char* what, int line, bool finite = false) {
    if (v.type != Value::Type::ARRAY || !v.array) failType10(fn, std::string("'") + what + "' expects a matrix (array of rows) but got " + v.typeName(), line);
    if (v.array->empty()) failArg10(fn, std::string("'") + what + "' is an empty matrix", line);
    const size_t R = v.array->size();
    size_t C = 0;
    Mat m;
    for (size_t i = 0; i < R; ++i) {
        const Value& row = (*v.array)[i];
        if (row.type != Value::Type::ARRAY || !row.array)
            failType10(fn, std::string("'") + what + "' row " + std::to_string(i) + " must be an array but got " + row.typeName() + " (a matrix is an array of rows)", line);
        if (i == 0) {
            C = row.array->size();
            if (C == 0) failArg10(fn, std::string("'") + what + "' has an empty row", line);
            m = Mat(R, C);
        } else if (row.array->size() != C) {
            failArg10(fn, std::string("'") + what + "' is not rectangular: row " + std::to_string(i) + " has " + std::to_string(row.array->size()) +
                          " columns but row 0 has " + std::to_string(C), line);
        }
        for (size_t j = 0; j < C; ++j) {
            const Value& e = (*row.array)[j];
            if (e.type != Value::Type::NUMBER)
                failType10(fn, std::string("'") + what + "'[" + std::to_string(i) + "][" + std::to_string(j) + "] must be a number but got " + e.typeName(), line);
            if (finite && !std::isfinite(e.number))
                failArg10(fn, std::string("'") + what + "'[" + std::to_string(i) + "][" + std::to_string(j) + "] is not finite", line);
            m(i, j) = e.number;
        }
    }
    return m;
}

static std::vector<double> readVec10(const Value& v, const char* fn, const char* what, int line, bool finite = false, bool allowEmpty = false) {
    if (v.type != Value::Type::ARRAY || !v.array) failType10(fn, std::string("'") + what + "' expects a vector (array of numbers) but got " + v.typeName(), line);
    if (v.array->empty() && !allowEmpty) failArg10(fn, std::string("'") + what + "' is an empty vector", line);
    std::vector<double> out;
    out.reserve(v.array->size());
    for (size_t i = 0; i < v.array->size(); ++i) {
        const Value& e = (*v.array)[i];
        if (e.type != Value::Type::NUMBER)
            failType10(fn, std::string("'") + what + "'[" + std::to_string(i) + "] must be a number but got " + e.typeName() + (e.type == Value::Type::ARRAY ? " (expected a vector, got a matrix)" : ""), line);
        if (finite && !std::isfinite(e.number)) failArg10(fn, std::string("'") + what + "'[" + std::to_string(i) + "] is not finite", line);
        out.push_back(e.number);
    }
    return out;
}

static size_t needDim10(const Value& v, const char* fn, const char* what, int line, size_t lo = 1) {
    if (v.type != Value::Type::NUMBER || !std::isfinite(v.number) || v.number != std::floor(v.number) || v.number < static_cast<double>(lo) || v.number > 100000000.0)
        failArg10(fn, std::string("'") + what + "' must be an integer >= " + std::to_string(lo) + " but got " + (v.type == Value::Type::NUMBER ? std::to_string(v.number) : v.typeName()), line);
    return static_cast<size_t>(v.number);
}
static double needNum10(const Value& v, const char* fn, const char* what, int line) {
    if (v.type != Value::Type::NUMBER) failType10(fn, std::string("'") + what + "' expects a number but got " + v.typeName(), line);
    return v.number;
}

static std::string dims10(const Mat& m) { return std::to_string(m.r) + "x" + std::to_string(m.c); }

// ------------------------------------------------------------------ حساب دقيق
// جمع Neumaier (نفس خوارزمية numberSum في مفهوم number)
static double neumaier10(const std::vector<double>& v) {
    double sum = 0.0, c = 0.0;
    for (double x : v) {
        const double t = sum + x;
        if (std::fabs(sum) >= std::fabs(x)) c += (sum - t) + x; else c += (x - t) + sum;
        sum = t;
    }
    return sum + c;
}
// جداء داخلي مُعوَّض (Ogita–Rump–Oishi Dot2): دقته كأنّه محسوب بضعف الدقة
static double dot2_10(const double* x, size_t sx, const double* y, size_t sy, size_t n) {
    double p = 0.0, s = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = x[i * sx], b = y[i * sy];
        const double h = a * b, r = std::fma(a, b, -h);
        const double t = p + h, z = t - p;
        const double q = (p - (t - z)) + (h - z);
        p = t; s += q + r;
    }
    return p + s;
}
// نظيم إقليدي بلا فيض/انسياب (تحجيم)
static double norm2_10(const double* x, size_t n, size_t stride = 1) {
    double scale = 0.0;
    for (size_t i = 0; i < n; ++i) scale = std::max(scale, std::fabs(x[i * stride]));
    if (scale == 0.0 || !std::isfinite(scale)) return scale;
    double acc = 0.0, c = 0.0;
    for (size_t i = 0; i < n; ++i) { const double t = x[i * stride] / scale; const double y = t * t - c; const double z = acc + y; c = (z - acc) - y; acc = z; }
    return scale * std::sqrt(acc);
}

static Mat transpose10(const Mat& a) {
    Mat t(a.c, a.r);
    for (size_t i = 0; i < a.r; ++i) for (size_t j = 0; j < a.c; ++j) t(j, i) = a(i, j);
    return t;
}
static Mat identity10(size_t n) { Mat m(n, n); for (size_t i = 0; i < n; ++i) m(i, i) = 1.0; return m; }
static Mat mul10(const Mat& a, const Mat& b) { // a: m×k, b: k×n
    Mat out(a.r, b.c);
    for (size_t i = 0; i < a.r; ++i)
        for (size_t k = 0; k < a.c; ++k) {
            const double aik = a(i, k);
            if (aik == 0.0) continue;
            for (size_t j = 0; j < b.c; ++j) out(i, j) += aik * b(k, j);
        }
    return out;
}
static double maxAbs10(const Mat& a) { double m = 0; for (double x : a.d) m = std::max(m, std::fabs(x)); return m; }

// ------------------------------------------------------------------ LU (تدوير جزئي)
struct LU10 { Mat lu; std::vector<size_t> perm; int sign = 1; bool singular = false; size_t badCol = 0; };

static LU10 luDecompose10(const Mat& A) {
    LU10 f; f.lu = A; const size_t n = A.r;
    f.perm.resize(n); std::iota(f.perm.begin(), f.perm.end(), 0);
    const double scale = maxAbs10(A);
    const double tiny = scale * static_cast<double>(n) * kEps10;
    for (size_t k = 0; k < n; ++k) {
        size_t p = k; double best = std::fabs(f.lu(k, k));
        for (size_t i = k + 1; i < n; ++i) if (std::fabs(f.lu(i, k)) > best) { best = std::fabs(f.lu(i, k)); p = i; }
        if (best <= tiny) { if (!f.singular) { f.singular = true; f.badCol = k; } if (best == 0.0) continue; }
        if (p != k) {
            for (size_t j = 0; j < n; ++j) std::swap(f.lu(k, j), f.lu(p, j));
            std::swap(f.perm[k], f.perm[p]); f.sign = -f.sign;
        }
        const double piv = f.lu(k, k);
        if (piv == 0.0) continue;
        for (size_t i = k + 1; i < n; ++i) {
            const double m = f.lu(i, k) / piv; f.lu(i, k) = m;
            if (m != 0.0) for (size_t j = k + 1; j < n; ++j) f.lu(i, j) -= m * f.lu(k, j);
        }
    }
    return f;
}
// يحلّ A X = B بعد LU؛ B بعدد n صفوف
static Mat luSolve10(const LU10& f, const Mat& B) {
    const size_t n = f.lu.r, k = B.c;
    Mat X(n, k);
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < k; ++j) X(i, j) = B(f.perm[i], j);
    for (size_t c = 0; c < k; ++c) {
        for (size_t i = 0; i < n; ++i) { double s = X(i, c); for (size_t j = 0; j < i; ++j) s -= f.lu(i, j) * X(j, c); X(i, c) = s; }
        for (size_t ii = n; ii-- > 0;) { double s = X(ii, c); for (size_t j = ii + 1; j < n; ++j) s -= f.lu(ii, j) * X(j, c); X(ii, c) = s / f.lu(ii, ii); }
    }
    return X;
}
static double det10(const Mat& A) {
    LU10 f = luDecompose10(A);
    double d = f.sign;
    for (size_t i = 0; i < A.r; ++i) d *= f.lu(i, i);
    return d;
}

// ------------------------------------------------------------------ QR (Householder)
struct QR10 { Mat Q, R; };
static QR10 qrHouseholder10(const Mat& A) {
    const size_t m = A.r, n = A.c;
    QR10 out; out.R = A; out.Q = identity10(m);
    const size_t steps = std::min(m > 0 ? m - 1 : 0, n);
    std::vector<double> v(m);
    for (size_t k = 0; k < steps; ++k) {
        double nx = norm2_10(&out.R(k, k), m - k, n);
        if (nx == 0.0) continue;
        const double alpha = out.R(k, k) > 0 ? -nx : nx;
        for (size_t i = k; i < m; ++i) v[i] = out.R(i, k);
        v[k] -= alpha;
        double vn = norm2_10(&v[k], m - k);
        if (vn == 0.0) continue;
        for (size_t i = k; i < m; ++i) v[i] /= vn;
        for (size_t j = 0; j < n; ++j) {
            double s = 0; for (size_t i = k; i < m; ++i) s += v[i] * out.R(i, j);
            for (size_t i = k; i < m; ++i) out.R(i, j) -= 2.0 * v[i] * s;
        }
        for (size_t i = 0; i < m; ++i) {
            double s = 0; for (size_t j = k; j < m; ++j) s += out.Q(i, j) * v[j];
            for (size_t j = k; j < m; ++j) out.Q(i, j) -= 2.0 * s * v[j];
        }
        for (size_t i = k + 1; i < m; ++i) out.R(i, k) = 0.0;
    }
    return out;
}

// ------------------------------------------------------------------ Jacobi للمتناظرة
struct Eig10 { std::vector<double> w; Mat V; int sweeps = 0; bool converged = true; };
static Eig10 jacobiEig10(const Mat& S) {
    const size_t n = S.r; Eig10 e; Mat A = S; e.V = identity10(n);
    const double scale = std::max(maxAbs10(S), std::numeric_limits<double>::min());
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0; for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) off += A(i, j) * A(i, j);
        e.sweeps = sweep;
        if (std::sqrt(off) <= kEps10 * scale * 1e-2 || off == 0.0) { e.converged = true; break; }
        if (sweep == 99) e.converged = false;
        for (size_t p = 0; p < n; ++p) for (size_t q = p + 1; q < n; ++q) {
            const double apq = A(p, q);
            if (std::fabs(apq) <= std::numeric_limits<double>::min()) continue;
            const double theta = (A(q, q) - A(p, p)) / (2.0 * apq);
            const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
            const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
            for (size_t k = 0; k < n; ++k) { const double akp = A(k, p), akq = A(k, q); A(k, p) = c * akp - s * akq; A(k, q) = s * akp + c * akq; }
            for (size_t k = 0; k < n; ++k) { const double apk = A(p, k), aqk = A(q, k); A(p, k) = c * apk - s * aqk; A(q, k) = s * apk + c * aqk; }
            for (size_t k = 0; k < n; ++k) { const double vkp = e.V(k, p), vkq = e.V(k, q); e.V(k, p) = c * vkp - s * vkq; e.V(k, q) = s * vkp + c * vkq; }
        }
    }
    std::vector<size_t> idx(n); std::iota(idx.begin(), idx.end(), 0);
    std::vector<double> diag(n); for (size_t i = 0; i < n; ++i) diag[i] = A(i, i);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return diag[a] > diag[b]; });
    Mat V2(n, n); e.w.resize(n);
    for (size_t k = 0; k < n; ++k) {
        e.w[k] = diag[idx[k]];
        size_t big = 0; double bm = -1;
        for (size_t i = 0; i < n; ++i) if (std::fabs(e.V(i, idx[k])) > bm + 1e-12) { bm = std::fabs(e.V(i, idx[k])); big = i; }
        const double sg = e.V(big, idx[k]) < 0 ? -1.0 : 1.0;   // اتفاقية الإشارة: أكبر مركّبة موجبة
        for (size_t i = 0; i < n; ++i) V2(i, k) = sg * e.V(i, idx[k]);
    }
    e.V = V2;
    return e;
}

// ------------------------------------------------------------------ SVD (Jacobi أحادي الجانب — Hestenes)
struct SVD10 { Mat U; std::vector<double> s; Mat V; Mat Vfull; };
// يفترض m >= n
static void svdCore10(Mat U, size_t n, Mat& Uo, std::vector<double>& s, Mat& V) {
    const size_t m = U.r;
    V = identity10(n);
    for (int sweep = 0; sweep < 60; ++sweep) {
        bool rotated = false;
        for (size_t p = 0; p + 1 < n; ++p) for (size_t q = p + 1; q < n; ++q) {
            double alpha = 0, beta = 0, gamma = 0;
            for (size_t i = 0; i < m; ++i) { alpha += U(i, p) * U(i, p); beta += U(i, q) * U(i, q); gamma += U(i, p) * U(i, q); }
            if (gamma == 0.0 || std::fabs(gamma) <= kEps10 * std::sqrt(alpha * beta)) continue;
            rotated = true;
            const double zeta = (beta - alpha) / (2.0 * gamma);
            const double t = (zeta >= 0 ? 1.0 : -1.0) / (std::fabs(zeta) + std::sqrt(1.0 + zeta * zeta));
            const double c = 1.0 / std::sqrt(1.0 + t * t), sn = c * t;
            for (size_t i = 0; i < m; ++i) { const double up = U(i, p), uq = U(i, q); U(i, p) = c * up - sn * uq; U(i, q) = sn * up + c * uq; }
            for (size_t i = 0; i < n; ++i) { const double vp = V(i, p), vq = V(i, q); V(i, p) = c * vp - sn * vq; V(i, q) = sn * vp + c * vq; }
        }
        if (!rotated) break;
    }
    s.assign(n, 0.0);
    for (size_t j = 0; j < n; ++j) s[j] = norm2_10(&U(0, j), m, n);
    std::vector<size_t> idx(n); std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return s[a] > s[b]; });
    Uo = Mat(m, n); Mat V2(n, n); std::vector<double> s2(n);
    for (size_t k = 0; k < n; ++k) {
        const size_t j = idx[k]; s2[k] = s[j];
        for (size_t i = 0; i < m; ++i) Uo(i, k) = s[j] > 0 ? U(i, j) / s[j] : 0.0;
        for (size_t i = 0; i < n; ++i) V2(i, k) = V(i, j);
    }
    s = s2; V = V2;
}
static SVD10 svd10(const Mat& A) {
    SVD10 r;
    if (A.r >= A.c) {
        svdCore10(A, A.c, r.U, r.s, r.V);
        r.Vfull = r.V;
    } else { // نُتمّم بصفوف صفرية لتصبح مربعة فنحصل على V كاملة (لازمة لفضاء الصفر)
        Mat P(A.c, A.c);
        for (size_t i = 0; i < A.r; ++i) for (size_t j = 0; j < A.c; ++j) P(i, j) = A(i, j);
        Mat Up, Vp; std::vector<double> sp;
        svdCore10(P, A.c, Up, sp, Vp);
        const size_t k = A.r;
        r.U = Mat(A.r, k); r.V = Mat(A.c, k); r.s.assign(sp.begin(), sp.begin() + static_cast<long>(k));
        for (size_t i = 0; i < A.r; ++i) for (size_t j = 0; j < k; ++j) r.U(i, j) = Up(i, j);
        for (size_t i = 0; i < A.c; ++i) for (size_t j = 0; j < k; ++j) r.V(i, j) = Vp(i, j);
        r.Vfull = Vp;
    }
    return r;
}
static double svdTol10(const SVD10& f, size_t m, size_t n, double userTol) {
    if (userTol > 0) return userTol;
    return static_cast<double>(std::max(m, n)) * (f.s.empty() ? 0.0 : f.s[0]) * kEps10;
}
static size_t rankFromSvd10(const SVD10& f, double tol) { size_t r = 0; for (double x : f.s) if (x > tol) ++r; return r; }
static Mat pinvFromSvd10(const SVD10& f, size_t m, size_t n, double tol) {
    Mat P(n, m);
    for (size_t k = 0; k < f.s.size(); ++k) {
        if (!(f.s[k] > tol)) continue;
        const double inv = 1.0 / f.s[k];
        for (size_t i = 0; i < n; ++i) { const double vik = f.V(i, k) * inv; if (vik == 0.0) continue; for (size_t j = 0; j < m; ++j) P(i, j) += vik * f.U(j, k); }
    }
    return P;
}

// ------------------------------------------------------------------ القيم الذاتية لمصفوفة حقيقية عامة (توازن + Hessenberg + QR مُزاح)
struct Cx10 { double re, im; };
static bool eigvals10(const Mat& M, std::vector<Cx10>& out) {
    const size_t n = M.r;
    std::vector<std::vector<double>> a(n + 2, std::vector<double>(n + 2, 0.0));
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) a[i + 1][j + 1] = M(i, j);
    // موازنة
    {
        const double RADIX = 2.0, sq = RADIX * RADIX; bool last = false;
        while (!last) {
            last = true;
            for (size_t i = 1; i <= n; ++i) {
                double r = 0, c = 0;
                for (size_t j = 1; j <= n; ++j) if (j != i) { c += std::fabs(a[j][i]); r += std::fabs(a[i][j]); }
                if (c != 0.0 && r != 0.0) {
                    double g = r / RADIX, f = 1.0; const double s = c + r;
                    while (c < g) { f *= RADIX; c *= sq; }
                    g = r * RADIX;
                    while (c > g) { f /= RADIX; c /= sq; }
                    if ((c + r) / f < 0.95 * s) {
                        last = false; g = 1.0 / f;
                        for (size_t j = 1; j <= n; ++j) a[i][j] *= g;
                        for (size_t j = 1; j <= n; ++j) a[j][i] *= f;
                    }
                }
            }
        }
    }
    // اختزال إلى Hessenberg بالحذف مع التدوير
    for (size_t m = 2; m < n; ++m) {
        double x = 0.0; size_t i = m;
        for (size_t j = m; j <= n; ++j) if (std::fabs(a[j][m - 1]) > std::fabs(x)) { x = a[j][m - 1]; i = j; }
        if (i != m) {
            for (size_t j = m - 1; j <= n; ++j) std::swap(a[i][j], a[m][j]);
            for (size_t j = 1; j <= n; ++j) std::swap(a[j][i], a[j][m]);
        }
        if (x != 0.0) {
            for (i = m + 1; i <= n; ++i) {
                double y = a[i][m - 1];
                if (y != 0.0) {
                    y /= x; a[i][m - 1] = y;
                    for (size_t j = m; j <= n; ++j) a[i][j] -= y * a[m][j];
                    for (size_t j = 1; j <= n; ++j) a[j][m] += y * a[j][i];
                }
            }
        }
    }
    for (size_t i = 3; i <= n; ++i) for (size_t j = 1; j + 1 < i; ++j) a[i][j] = 0.0;
    // QR (hqr)
    std::vector<double> wr(n + 2, 0.0), wi(n + 2, 0.0);
    double anorm = 0.0;
    for (size_t i = 1; i <= n; ++i) for (size_t j = (i > 1 ? i - 1 : 1); j <= n; ++j) anorm += std::fabs(a[i][j]);
    long nn = static_cast<long>(n); double t = 0.0;
    auto A = [&](long i, long j) -> double& { return a[static_cast<size_t>(i)][static_cast<size_t>(j)]; };
    auto sign = [](double x, double y) { return y >= 0.0 ? std::fabs(x) : -std::fabs(x); };
    double p = 0, q = 0, r = 0, s = 0, w = 0, x = 0, y = 0, z = 0;
    while (nn >= 1) {
        int its = 0; long l;
        do {
            for (l = nn; l >= 2; --l) {
                s = std::fabs(A(l - 1, l - 1)) + std::fabs(A(l, l));
                if (s == 0.0) s = anorm;
                if (std::fabs(A(l, l - 1)) + s == s) { A(l, l - 1) = 0.0; break; }
            }
            x = A(nn, nn);
            if (l == nn) { wr[nn] = x + t; wi[nn] = 0.0; --nn; }
            else {
                y = A(nn - 1, nn - 1); w = A(nn, nn - 1) * A(nn - 1, nn);
                if (l == nn - 1) {
                    p = 0.5 * (y - x); q = p * p + w; z = std::sqrt(std::fabs(q)); x += t;
                    if (q >= 0.0) {
                        z = p + sign(z, p);
                        wr[nn - 1] = wr[nn] = x + z;
                        if (z != 0.0) wr[nn] = x - w / z;
                        wi[nn - 1] = wi[nn] = 0.0;
                    } else {
                        wr[nn - 1] = wr[nn] = x + p;
                        wi[nn] = z; wi[nn - 1] = -z;
                    }
                    nn -= 2;
                } else {
                    if (its == 60) return false;
                    if (its == 10 || its == 20) {
                        t += x;
                        for (long i = 1; i <= nn; ++i) A(i, i) -= x;
                        s = std::fabs(A(nn, nn - 1)) + std::fabs(A(nn - 1, nn - 2));
                        y = x = 0.75 * s; w = -0.4375 * s * s;
                    }
                    ++its;
                    long m;
                    for (m = nn - 2; m >= l; --m) {
                        z = A(m, m); r = x - z; s = y - z;
                        p = (r * s - w) / A(m + 1, m) + A(m, m + 1);
                        q = A(m + 1, m + 1) - z - r - s; r = A(m + 2, m + 1);
                        s = std::fabs(p) + std::fabs(q) + std::fabs(r);
                        p /= s; q /= s; r /= s;
                        if (m == l) break;
                        const double u = std::fabs(A(m, m - 1)) * (std::fabs(q) + std::fabs(r));
                        const double v = std::fabs(p) * (std::fabs(A(m - 1, m - 1)) + std::fabs(z) + std::fabs(A(m + 1, m + 1)));
                        if (u + v == v) break;
                    }
                    for (long i = m + 2; i <= nn; ++i) { A(i, i - 2) = 0.0; if (i != m + 2) A(i, i - 3) = 0.0; }
                    for (long k = m; k <= nn - 1; ++k) {
                        if (k != m) {
                            p = A(k, k - 1); q = A(k + 1, k - 1); r = 0.0;
                            if (k != nn - 1) r = A(k + 2, k - 1);
                            if ((x = std::fabs(p) + std::fabs(q) + std::fabs(r)) != 0.0) { p /= x; q /= x; r /= x; }
                        }
                        if ((s = sign(std::sqrt(p * p + q * q + r * r), p)) != 0.0) {
                            if (k == m) { if (l != m) A(k, k - 1) = -A(k, k - 1); }
                            else A(k, k - 1) = -s * x;
                            p += s; x = p / s; y = q / s; z = r / s; q /= p; r /= p;
                            for (long j = k; j <= nn; ++j) {
                                p = A(k, j) + q * A(k + 1, j);
                                if (k != nn - 1) { p += r * A(k + 2, j); A(k + 2, j) -= p * z; }
                                A(k + 1, j) -= p * y; A(k, j) -= p * x;
                            }
                            const long mmin = nn < k + 3 ? nn : k + 3;
                            for (long i = l; i <= mmin; ++i) {
                                p = x * A(i, k) + y * A(i, k + 1);
                                if (k != nn - 1) { p += z * A(i, k + 2); A(i, k + 2) -= p * r; }
                                A(i, k + 1) -= p * q; A(i, k) -= p;
                            }
                        }
                    }
                }
            }
        } while (l < nn - 1);
    }
    out.clear();
    for (size_t i = 1; i <= n; ++i) out.push_back({wr[i], wi[i]});
    return true;
}

// ------------------------------------------------------------------ تطبيع الوسائط: مصفوفة أو متجه
struct Operand { Mat m; bool isVec = false; };
static Operand readOperand10(const Value& v, const char* fn, const char* what, int line) {
    Operand o;
    if (looksMatrix10(v)) { o.m = readMat10(v, fn, what, line); return o; }
    std::vector<double> x = readVec10(v, fn, what, line);
    o.m = Mat(x.size(), 1); for (size_t i = 0; i < x.size(); ++i) o.m(i, 0) = x[i]; o.isVec = true;
    return o;
}
static Value operandVal10(const Mat& m, bool asVec) {
    if (!asVec) return matVal(m);
    std::vector<double> v(m.r); for (size_t i = 0; i < m.r; ++i) v[i] = m(i, 0);
    return vecVal(v);
}
static double matNormKind10(const Mat& M, const std::string& kind, const char* fn, int line) {
    if (kind == "fro" || kind == "f" || kind == "frobenius") return norm2_10(M.d.data(), M.d.size());
    if (kind == "1") { double best = 0; for (size_t j = 0; j < M.c; ++j) { double s = 0; for (size_t i = 0; i < M.r; ++i) s += std::fabs(M(i, j)); best = std::max(best, s); } return best; }
    if (kind == "inf") { double best = 0; for (size_t i = 0; i < M.r; ++i) { double s = 0; for (size_t j = 0; j < M.c; ++j) s += std::fabs(M(i, j)); best = std::max(best, s); } return best; }
    if (kind == "max") return maxAbs10(M);
    if (kind == "2" || kind == "spectral") { SVD10 f = svd10(M); return f.s.empty() ? 0.0 : f.s[0]; }
    if (kind == "nuc" || kind == "nuclear") { SVD10 f = svd10(M); std::vector<double> s = f.s; return neumaier10(s); }
    failArg10(fn, "unknown norm '" + kind + "' (use 1, 2, \"inf\", \"fro\", \"max\" or \"nuc\")", line);
}
static std::string normKindArg10(const Value& v, const char* fn, int line) {
    if (v.type == Value::Type::NUMBER) { if (v.number == 1) return "1"; if (v.number == 2) return "2"; if (std::isinf(v.number) && v.number > 0) return "inf"; failArg10(fn, "unknown norm (use 1, 2, \"inf\", \"fro\", \"max\" or \"nuc\")", line); }
    if (v.type == Value::Type::STRING) { std::string s = v.str; std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); }); return s; }
    failType10(fn, "norm kind must be a number or a string", line);
}
static void requireSquare10(const Mat& m, const char* fn, const char* what, int line) {
    if (m.r != m.c) failArg10(fn, std::string("'") + what + "' must be square but is " + dims10(m), line);
}
static bool isSymmetric10(const Mat& m, double tol) {
    if (m.r != m.c) return false;
    const double sc = std::max(maxAbs10(m), std::numeric_limits<double>::min());
    for (size_t i = 0; i < m.r; ++i) for (size_t j = i + 1; j < m.c; ++j) if (std::fabs(m(i, j) - m(j, i)) > tol * sc) return false;
    return true;
}
static bool tryCholesky10(const Mat& A, Mat& L, double tolSym) {
    if (!isSymmetric10(A, tolSym)) return false;
    const size_t n = A.r; L = Mat(n, n);
    for (size_t j = 0; j < n; ++j) {
        double d = A(j, j); for (size_t k = 0; k < j; ++k) d -= L(j, k) * L(j, k);
        if (!(d > 0.0)) return false;
        L(j, j) = std::sqrt(d);
        for (size_t i = j + 1; i < n; ++i) { double s = A(i, j); for (size_t k = 0; k < j; ++k) s -= L(i, k) * L(j, k); L(i, j) = s / L(j, j); }
    }
    return true;
}
static void singularFail10(const char* fn, const LU10& f, int line) {
    failRun10(fn, "matrix is singular or numerically singular (zero pivot at column " + std::to_string(f.badCol) + "); use matLstsq/matPinv for a least-squares answer", line);
}
static uint64_t seedMix10(uint64_t s) { s += 0x9E3779B97F4A7C15ULL; s = (s ^ (s >> 30)) * 0xBF58476D1CE4E5B9ULL; s = (s ^ (s >> 27)) * 0x94D049BB133111EBULL; return s ^ (s >> 31); }

} // namespace extra10

// ============================================================================
void Interpreter::registerNativesExtra10() {
    using namespace extra10;

    // ======================================================== الإنشاء
    natives["matZeros"] = [](Args& a, int line) -> Value {
        expectArgsRange("matZeros", a, 1, 2, line);
        const size_t r = needDim10(a[0], "matZeros", "rows", line), c = a.size() == 2 ? needDim10(a[1], "matZeros", "cols", line) : r;
        if (r * c > 50000000) failArg10("matZeros", "matrix too large", line);
        return matVal(Mat(r, c, 0.0));
    };
    natives["matOnes"] = [](Args& a, int line) -> Value {
        expectArgsRange("matOnes", a, 1, 2, line);
        const size_t r = needDim10(a[0], "matOnes", "rows", line), c = a.size() == 2 ? needDim10(a[1], "matOnes", "cols", line) : r;
        if (r * c > 50000000) failArg10("matOnes", "matrix too large", line);
        return matVal(Mat(r, c, 1.0));
    };
    natives["matEye"] = [](Args& a, int line) -> Value {
        expectArgsRange("matEye", a, 1, 2, line);
        const size_t r = needDim10(a[0], "matEye", "n", line), c = a.size() == 2 ? needDim10(a[1], "matEye", "m", line) : r;
        if (r * c > 50000000) failArg10("matEye", "matrix too large", line);
        Mat m(r, c); for (size_t i = 0; i < std::min(r, c); ++i) m(i, i) = 1.0;
        return matVal(m);
    };
    // matDiag(vector) -> مصفوفة قطرية | matDiag(matrix) -> متجه القطر
    natives["matDiag"] = [](Args& a, int line) -> Value {
        expectArgs("matDiag", a, 1, line);
        if (looksMatrix10(a[0])) {
            Mat m = readMat10(a[0], "matDiag", "M", line);
            std::vector<double> d; for (size_t i = 0; i < std::min(m.r, m.c); ++i) d.push_back(m(i, i));
            return vecVal(d);
        }
        std::vector<double> v = readVec10(a[0], "matDiag", "v", line);
        Mat m(v.size(), v.size()); for (size_t i = 0; i < v.size(); ++i) m(i, i) = v[i];
        return matVal(m);
    };
    // matBuild(rows, cols, fn(i, j)) — يملأ بدالة (الفهارس من 0)
    natives["matBuild"] = [this](Args& a, int line) -> Value {
        expectArgs("matBuild", a, 3, line);
        const size_t r = needDim10(a[0], "matBuild", "rows", line), c = needDim10(a[1], "matBuild", "cols", line);
        if (r * c > 20000000) failArg10("matBuild", "matrix too large", line);
        if (a[2].type != Value::Type::FUNCTION && a[2].type != Value::Type::INSTANCE) failType10("matBuild", "third argument must be a function(i, j)", line);
        Mat m(r, c);
        for (size_t i = 0; i < r; ++i) for (size_t j = 0; j < c; ++j) {
            std::vector<Value> args{N10(static_cast<double>(i)), N10(static_cast<double>(j))};
            Value v = callValue(a[2], args, line);
            if (v.type != Value::Type::NUMBER) failType10("matBuild", "fn(" + std::to_string(i) + ", " + std::to_string(j) + ") must return a number but returned " + v.typeName(), line);
            m(i, j) = v.number;
        }
        return matVal(m);
    };
    // matRandom(r, c[, {seed, min, max, normal, mean, std, int}]) — حتمية عند تحديد seed (بين المنصّات)
    natives["matRandom"] = [](Args& a, int line) -> Value {
        expectArgsRange("matRandom", a, 2, 3, line);
        const size_t r = needDim10(a[0], "matRandom", "rows", line), c = needDim10(a[1], "matRandom", "cols", line);
        if (r * c > 20000000) failArg10("matRandom", "matrix too large", line);
        const Value* o = optMap10(a, 2, "matRandom", line);
        uint64_t seed;
        if (o && get10(*o, "seed")) seed = seedMix10(static_cast<uint64_t>(static_cast<int64_t>(optNum10(o, "seed", 0, "matRandom", line))));
        else seed = (static_cast<uint64_t>(std::random_device{}()) << 32) ^ std::random_device{}();
        std::mt19937_64 g(seed);
        auto uni = [&]() { return static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0); };
        const bool normal = optBool10(o, "normal", false), asInt = optBool10(o, "int", false);
        const double lo = optNum10(o, "min", 0.0, "matRandom", line), hi = optNum10(o, "max", 1.0, "matRandom", line);
        const double mean = optNum10(o, "mean", 0.0, "matRandom", line), sd = optNum10(o, "std", 1.0, "matRandom", line);
        if (!normal && hi < lo) failArg10("matRandom", "'max' must be >= 'min'", line);
        Mat m(r, c);
        for (size_t i = 0; i < r; ++i) for (size_t j = 0; j < c; ++j) {
            double x;
            if (normal) { double u1 = uni(), u2 = uni(); if (u1 < 1e-300) u1 = 1e-300; x = mean + sd * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2); }
            else if (asInt) x = std::floor(lo + uni() * (std::floor(hi) - std::ceil(lo) + 1.0));
            else x = lo + uni() * (hi - lo);
            m(i, j) = x;
        }
        return matVal(m);
    };
    natives["matHilbert"] = [](Args& a, int line) -> Value {
        expectArgs("matHilbert", a, 1, line);
        const size_t n = needDim10(a[0], "matHilbert", "n", line);
        if (n > 5000) failArg10("matHilbert", "n too large", line);
        Mat m(n, n); for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) m(i, j) = 1.0 / static_cast<double>(i + j + 1);
        return matVal(m);
    };
    // matVandermonde(v[, cols]) — الأعمدة x^0, x^1, ... (ترتيب تصاعدي)
    natives["matVandermonde"] = [](Args& a, int line) -> Value {
        expectArgsRange("matVandermonde", a, 1, 2, line);
        std::vector<double> v = readVec10(a[0], "matVandermonde", "v", line);
        const size_t cols = a.size() == 2 ? needDim10(a[1], "matVandermonde", "cols", line) : v.size();
        Mat m(v.size(), cols);
        for (size_t i = 0; i < v.size(); ++i) { double p = 1.0; for (size_t j = 0; j < cols; ++j) { m(i, j) = p; p *= v[i]; } }
        return matVal(m);
    };

    // ======================================================== الفحص
    natives["matShape"] = [](Args& a, int line) -> Value {
        expectArgs("matShape", a, 1, line);
        Operand o = readOperand10(a[0], "matShape", "M", line);
        auto m = std::make_shared<MapData>();
        put10(m, "rows", N10(static_cast<double>(o.m.r))); put10(m, "cols", N10(static_cast<double>(o.m.c)));
        put10(m, "size", N10(static_cast<double>(o.m.r * o.m.c))); put10(m, "square", B10(o.m.r == o.m.c));
        put10(m, "kind", S10(o.isVec ? "vector" : "matrix"));
        return Value::makeMap(m);
    };
    // matCheck(M) -> {ok, rows, cols} | {ok:false, error} — بلا استثناء
    natives["matCheck"] = [](Args& a, int line) -> Value {
        expectArgs("matCheck", a, 1, line);
        auto m = std::make_shared<MapData>();
        try {
            Mat x = readMat10(a[0], "matCheck", "M", line);
            put10(m, "ok", B10(true)); put10(m, "rows", N10(static_cast<double>(x.r))); put10(m, "cols", N10(static_cast<double>(x.c)));
            bool fin = true; for (double v : x.d) if (!std::isfinite(v)) { fin = false; break; }
            put10(m, "finite", B10(fin));
        } catch (const RinError& e) {
            std::string why = e.message;
            const size_t k = why.find("'M' "); // نُبقي الجزء المفيد بعد اسم الدالة
            if (k != std::string::npos) why = why.substr(k + 4);
            m = std::make_shared<MapData>(); put10(m, "ok", B10(false)); put10(m, "error", S10(why));
        }
        return Value::makeMap(m);
    };
    // matIs(M, what[, tol]) — symmetric | orthogonal | diagonal | identity | upper | lower | positive-definite | singular | square | zero
    natives["matIs"] = [](Args& a, int line) -> Value {
        expectArgsRange("matIs", a, 2, 3, line);
        Mat M = readMat10(a[0], "matIs", "M", line);
        if (a[1].type != Value::Type::STRING) failType10("matIs", "property must be a string", line);
        std::string w = a[1].str; std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) { return std::tolower(c); });
        const double tol = a.size() == 3 ? needNum10(a[2], "matIs", "tol", line) : 1e-10;
        const double sc = std::max(maxAbs10(M), std::numeric_limits<double>::min());
        if (w == "square") return B10(M.r == M.c);
        if (w == "zero") return B10(maxAbs10(M) == 0.0);
        if (w == "symmetric") return B10(isSymmetric10(M, tol));
        if (w == "diagonal" || w == "upper" || w == "lower" || w == "identity") {
            for (size_t i = 0; i < M.r; ++i) for (size_t j = 0; j < M.c; ++j) {
                const bool off = (w == "diagonal" || w == "identity") ? i != j : (w == "upper" ? i > j : i < j);
                if (off && std::fabs(M(i, j)) > tol * sc) return B10(false);
                if (w == "identity" && i == j && std::fabs(M(i, j) - 1.0) > tol) return B10(false);
            }
            return B10(w != "identity" || M.r == M.c);
        }
        if (w == "orthogonal") {
            if (M.r != M.c) return B10(false);
            Mat P = mul10(transpose10(M), M);
            for (size_t i = 0; i < P.r; ++i) for (size_t j = 0; j < P.c; ++j) if (std::fabs(P(i, j) - (i == j ? 1.0 : 0.0)) > tol * std::max(1.0, static_cast<double>(M.r))) return B10(false);
            return B10(true);
        }
        if (w == "positive-definite" || w == "pd" || w == "spd") { Mat L; return B10(M.r == M.c && tryCholesky10(M, L, tol)); }
        if (w == "singular") { if (M.r != M.c) return B10(true); LU10 f = luDecompose10(M); return B10(f.singular); }
        failArg10("matIs", "unknown property '" + a[1].str + "' (symmetric, orthogonal, diagonal, identity, upper, lower, positive-definite, singular, square, zero)", line);
    };

    // ======================================================== العمليات الأساسية
    auto binaryOp = [](const char* name, int mode) {
        return [name, mode](Args& a, int line) -> Value {
            expectArgs(name, a, 2, line);
            Operand x = readOperand10(a[0], name, "A", line), y = readOperand10(a[1], name, "B", line);
            if (x.isVec != y.isVec) failType10(name, "cannot combine a vector with a matrix", line);
            if (x.m.r != y.m.r || x.m.c != y.m.c) failArg10(name, "shape mismatch: " + dims10(x.m) + " vs " + dims10(y.m), line);
            Mat o(x.m.r, x.m.c);
            for (size_t i = 0; i < o.d.size(); ++i) o.d[i] = mode == 0 ? x.m.d[i] + y.m.d[i] : (mode == 1 ? x.m.d[i] - y.m.d[i] : x.m.d[i] * y.m.d[i]);
            return operandVal10(o, x.isVec);
        };
    };
    natives["matAdd"] = binaryOp("matAdd", 0);
    natives["matSub"] = binaryOp("matSub", 1);
    natives["matHadamard"] = binaryOp("matHadamard", 2);
    natives["vecAdd"] = [](Args& a, int line) -> Value {
        expectArgs("vecAdd", a, 2, line);
        auto x = readVec10(a[0], "vecAdd", "a", line), y = readVec10(a[1], "vecAdd", "b", line);
        if (x.size() != y.size()) failArg10("vecAdd", "length mismatch: " + std::to_string(x.size()) + " vs " + std::to_string(y.size()), line);
        for (size_t i = 0; i < x.size(); ++i) x[i] += y[i];
        return vecVal(x);
    };
    natives["vecSub"] = [](Args& a, int line) -> Value {
        expectArgs("vecSub", a, 2, line);
        auto x = readVec10(a[0], "vecSub", "a", line), y = readVec10(a[1], "vecSub", "b", line);
        if (x.size() != y.size()) failArg10("vecSub", "length mismatch: " + std::to_string(x.size()) + " vs " + std::to_string(y.size()), line);
        for (size_t i = 0; i < x.size(); ++i) x[i] -= y[i];
        return vecVal(x);
    };
    natives["vecScale"] = [](Args& a, int line) -> Value {
        expectArgs("vecScale", a, 2, line);
        auto x = readVec10(a[0], "vecScale", "v", line); const double k = needNum10(a[1], "vecScale", "k", line);
        for (double& e : x) e *= k;
        return vecVal(x);
    };
    natives["matScale"] = [](Args& a, int line) -> Value {
        expectArgs("matScale", a, 2, line);
        Operand x = readOperand10(a[0], "matScale", "M", line); const double k = needNum10(a[1], "matScale", "k", line);
        for (double& e : x.m.d) e *= k;
        return operandVal10(x.m, x.isVec);
    };
    natives["matTranspose"] = [](Args& a, int line) -> Value {
        expectArgs("matTranspose", a, 1, line);
        return matVal(transpose10(readMat10(a[0], "matTranspose", "M", line)));
    };
    natives["matTrace"] = [](Args& a, int line) -> Value {
        expectArgs("matTrace", a, 1, line);
        Mat m = readMat10(a[0], "matTrace", "M", line); requireSquare10(m, "matTrace", "M", line);
        std::vector<double> d; for (size_t i = 0; i < m.r; ++i) d.push_back(m(i, i));
        return N10(neumaier10(d));
    };
    // matMul(A, B, ...) — مصفوفة×مصفوفة | مصفوفة×متجه (عمود) | متجه×مصفوفة (صف)
    natives["matMul"] = [](Args& a, int line) -> Value {
        if (a.size() < 2) failArg10("matMul", "expects at least 2 arguments but got " + std::to_string(a.size()), line);
        Operand acc = readOperand10(a[0], "matMul", "arg 0", line);
        const bool firstVec = acc.isVec; bool accRow = false;
        if (firstVec) { // متجه في أول الموضع = صف 1×n
            acc.m = transpose10(acc.m); accRow = true;
        }
        bool lastVecCol = false;
        for (size_t k = 1; k < a.size(); ++k) {
            Operand y = readOperand10(a[k], "matMul", ("arg " + std::to_string(k)).c_str(), line);
            if (acc.m.c != y.m.r) failArg10("matMul", "shape mismatch at argument " + std::to_string(k) + ": " + dims10(acc.m) + " × " + dims10(y.m) + " (inner dimensions " + std::to_string(acc.m.c) + " vs " + std::to_string(y.m.r) + ")", line);
            if (y.isVec && accRow && acc.m.r == 1 && k == 1 && acc.m.c == y.m.r && a.size() == 2)
                failArg10("matMul", "vector × vector is ambiguous — use vecDot(a, b) for the inner product", line);
            acc.m = mul10(acc.m, y.m); lastVecCol = y.isVec; accRow = false;
        }
        if (lastVecCol) return operandVal10(acc.m, true);
        if (firstVec && acc.m.r == 1) { std::vector<double> row(acc.m.c); for (size_t j = 0; j < acc.m.c; ++j) row[j] = acc.m(0, j); return vecVal(row); } // متجه × مصفوفة = متجه
        return matVal(acc.m);
    };
    // matPow(M, k) — قوة صحيحة (سالبة عبر المعكوس)، تربيع متكرر
    natives["matPow"] = [](Args& a, int line) -> Value {
        expectArgs("matPow", a, 2, line);
        Mat m = readMat10(a[0], "matPow", "M", line); requireSquare10(m, "matPow", "M", line);
        const double kd = needNum10(a[1], "matPow", "k", line);
        if (!std::isfinite(kd) || kd != std::floor(kd) || std::fabs(kd) > 1e9) failArg10("matPow", "'k' must be an integer", line);
        long long k = static_cast<long long>(kd);
        if (k < 0) {
            LU10 f = luDecompose10(m); if (f.singular) singularFail10("matPow", f, line);
            m = luSolve10(f, identity10(m.r)); k = -k;
        }
        Mat res = identity10(m.r);
        while (k > 0) { if (k & 1) res = mul10(res, m); k >>= 1; if (k) m = mul10(m, m); }
        return matVal(res);
    };
    natives["matKron"] = [](Args& a, int line) -> Value {
        expectArgs("matKron", a, 2, line);
        Mat x = readMat10(a[0], "matKron", "A", line), y = readMat10(a[1], "matKron", "B", line);
        if (x.r * y.r * x.c * y.c > 20000000) failArg10("matKron", "result too large", line);
        Mat o(x.r * y.r, x.c * y.c);
        for (size_t i = 0; i < x.r; ++i) for (size_t j = 0; j < x.c; ++j) for (size_t k = 0; k < y.r; ++k) for (size_t l = 0; l < y.c; ++l) o(i * y.r + k, j * y.c + l) = x(i, j) * y(k, l);
        return matVal(o);
    };
    // matMap(M, fn(x, i, j))
    natives["matMap"] = [this](Args& a, int line) -> Value {
        expectArgs("matMap", a, 2, line);
        Operand x = readOperand10(a[0], "matMap", "M", line);
        if (a[1].type != Value::Type::FUNCTION && a[1].type != Value::Type::INSTANCE) failType10("matMap", "second argument must be a function(x, i, j)", line);
        for (size_t i = 0; i < x.m.r; ++i) for (size_t j = 0; j < x.m.c; ++j) {
            std::vector<Value> args{N10(x.m(i, j)), N10(static_cast<double>(x.isVec ? 0 : i)), N10(static_cast<double>(x.isVec ? i : j))};
            if (x.isVec) args = {N10(x.m(i, j)), N10(static_cast<double>(i))};
            Value v = callValue(a[1], args, line);
            if (v.type != Value::Type::NUMBER) failType10("matMap", "fn must return a number but returned " + v.typeName(), line);
            x.m(i, j) = v.number;
        }
        return operandVal10(x.m, x.isVec);
    };
    natives["matNorm"] = [](Args& a, int line) -> Value {
        expectArgsRange("matNorm", a, 1, 2, line);
        Mat m = readMat10(a[0], "matNorm", "M", line, true);
        return N10(matNormKind10(m, a.size() == 2 ? normKindArg10(a[1], "matNorm", line) : "fro", "matNorm", line));
    };
    // matApprox(A, B[, rel=1e-9[, abs=1e-12]]) — نفس دلالة numberApprox على كل عنصر
    natives["matApprox"] = [](Args& a, int line) -> Value {
        expectArgsRange("matApprox", a, 2, 4, line);
        Operand x = readOperand10(a[0], "matApprox", "A", line), y = readOperand10(a[1], "matApprox", "B", line);
        const double rel = a.size() >= 3 ? needNum10(a[2], "matApprox", "rel", line) : 1e-9;
        const double ab = a.size() == 4 ? needNum10(a[3], "matApprox", "abs", line) : 1e-12;
        if (x.m.r != y.m.r || x.m.c != y.m.c) return B10(false);
        for (size_t i = 0; i < x.m.d.size(); ++i) {
            const double p = x.m.d[i], q = y.m.d[i];
            if (std::isnan(p) || std::isnan(q)) return B10(false);
            if (p == q) continue;
            if (std::isinf(p) || std::isinf(q)) return B10(false);
            if (!(std::fabs(p - q) <= std::max(rel * std::max(std::fabs(p), std::fabs(q)), ab))) return B10(false);
        }
        return B10(true);
    };
    // matFormat(M[, opts]) — نص منسّق ومحاذى؛ opts تُمرَّر إلى numberFormat (decimals, group, digits, ...)
    natives["matFormat"] = [this](Args& a, int line) -> Value {
        expectArgsRange("matFormat", a, 1, 2, line);
        Operand x = readOperand10(a[0], "matFormat", "M", line);
        const Value* o = optMap10(a, 1, "matFormat", line);
        auto nf = natives.find("numberFormat");
        if (nf == natives.end()) failRun10("matFormat", "numberFormat is not available", line);
        Value cellOpts;
        if (o) cellOpts = *o; else { auto mm = std::make_shared<MapData>(); put10(mm, "maxDecimals", N10(6)); cellOpts = Value::makeMap(mm); }
        if (o && !get10(*o, "decimals") && !get10(*o, "maxDecimals")) { auto mm = std::make_shared<MapData>(*o->map); put10(mm, "maxDecimals", N10(6)); cellOpts = Value::makeMap(mm); }
        std::vector<std::vector<std::string>> cells(x.m.r, std::vector<std::string>(x.m.c));
        std::vector<size_t> w(x.m.c, 0);
        auto cp = [](const std::string& s) { size_t n = 0; for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n; return n; };
        for (size_t i = 0; i < x.m.r; ++i) for (size_t j = 0; j < x.m.c; ++j) {
            std::vector<Value> args{N10(x.m(i, j)), cellOpts};
            Value s = nf->second(args, line);
            cells[i][j] = s.type == Value::Type::STRING ? s.str : std::to_string(x.m(i, j));
            w[j] = std::max(w[j], cp(cells[i][j]));
        }
        const bool brackets = optBool10(o, "brackets", true);
        std::string out;
        for (size_t i = 0; i < x.m.r; ++i) {
            if (i) out += "\n";
            if (brackets) out += "[ ";
            for (size_t j = 0; j < x.m.c; ++j) {
                if (j) out += "  ";
                out += std::string(w[j] - cp(cells[i][j]), ' ') + cells[i][j];
            }
            if (brackets) out += " ]";
        }
        return S10(out);
    };

    // ======================================================== المتجهات
    natives["vecDot"] = [](Args& a, int line) -> Value {
        expectArgs("vecDot", a, 2, line);
        auto x = readVec10(a[0], "vecDot", "a", line), y = readVec10(a[1], "vecDot", "b", line);
        if (x.size() != y.size()) failArg10("vecDot", "length mismatch: " + std::to_string(x.size()) + " vs " + std::to_string(y.size()), line);
        return N10(dot2_10(x.data(), 1, y.data(), 1, x.size()));
    };
    natives["vecSum"] = [](Args& a, int line) -> Value {
        expectArgs("vecSum", a, 1, line);
        return N10(neumaier10(readVec10(a[0], "vecSum", "v", line)));
    };
    // vecNorm(v[, p=2]) — p: 1 | 2 | عدد >= 1 | "inf"
    natives["vecNorm"] = [](Args& a, int line) -> Value {
        expectArgsRange("vecNorm", a, 1, 2, line);
        auto x = readVec10(a[0], "vecNorm", "v", line);
        double p = 2.0;
        if (a.size() == 2) {
            if (a[1].type == Value::Type::STRING && a[1].str == "inf") p = INFINITY;
            else p = needNum10(a[1], "vecNorm", "p", line);
            if (!(p >= 1.0)) failArg10("vecNorm", "'p' must be >= 1 or \"inf\"", line);
        }
        if (p == 2.0) return N10(norm2_10(x.data(), x.size()));
        if (std::isinf(p)) { double m = 0; for (double e : x) m = std::max(m, std::fabs(e)); return N10(m); }
        double m = 0; for (double e : x) m = std::max(m, std::fabs(e));
        if (m == 0.0 || !std::isfinite(m)) return N10(m);
        std::vector<double> t; for (double e : x) t.push_back(std::pow(std::fabs(e) / m, p));
        return N10(m * std::pow(neumaier10(t), 1.0 / p));
    };
    natives["vecDist"] = [](Args& a, int line) -> Value {
        expectArgsRange("vecDist", a, 2, 3, line);
        auto x = readVec10(a[0], "vecDist", "a", line), y = readVec10(a[1], "vecDist", "b", line);
        if (x.size() != y.size()) failArg10("vecDist", "length mismatch: " + std::to_string(x.size()) + " vs " + std::to_string(y.size()), line);
        for (size_t i = 0; i < x.size(); ++i) x[i] -= y[i];
        double p = 2.0;
        if (a.size() == 3) { if (a[2].type == Value::Type::STRING && a[2].str == "inf") p = INFINITY; else p = needNum10(a[2], "vecDist", "p", line); if (!(p >= 1.0)) failArg10("vecDist", "'p' must be >= 1 or \"inf\"", line); }
        if (p == 2.0) return N10(norm2_10(x.data(), x.size()));
        if (std::isinf(p)) { double m = 0; for (double e : x) m = std::max(m, std::fabs(e)); return N10(m); }
        double m = 0; for (double e : x) m = std::max(m, std::fabs(e));
        if (m == 0.0) return N10(0);
        std::vector<double> t; for (double e : x) t.push_back(std::pow(std::fabs(e) / m, p));
        return N10(m * std::pow(neumaier10(t), 1.0 / p));
    };
    natives["vecCross"] = [](Args& a, int line) -> Value {
        expectArgs("vecCross", a, 2, line);
        auto x = readVec10(a[0], "vecCross", "a", line), y = readVec10(a[1], "vecCross", "b", line);
        if (x.size() != 3 || y.size() != 3) failArg10("vecCross", "both vectors must have 3 components", line);
        auto d2 = [](double p, double q, double r, double s) { return dot2_10(std::vector<double>{p, -q}.data(), 1, std::vector<double>{s, r}.data(), 1, 2); };
        return vecVal({d2(x[1], x[2], y[1], y[2]), d2(x[2], x[0], y[2], y[0]), d2(x[0], x[1], y[0], y[1])});
    };
    natives["vecNormalize"] = [](Args& a, int line) -> Value {
        expectArgs("vecNormalize", a, 1, line);
        auto x = readVec10(a[0], "vecNormalize", "v", line, true);
        const double n = norm2_10(x.data(), x.size());
        if (n == 0.0) failRun10("vecNormalize", "cannot normalize the zero vector", line);
        for (double& e : x) e /= n;
        return vecVal(x);
    };
    // vecAngle(a, b) — بالراديان، مستقرّ عددياً (atan2 على |a×b| و a·b عبر الفرق)
    natives["vecAngle"] = [](Args& a, int line) -> Value {
        expectArgs("vecAngle", a, 2, line);
        auto x = readVec10(a[0], "vecAngle", "a", line, true), y = readVec10(a[1], "vecAngle", "b", line, true);
        if (x.size() != y.size()) failArg10("vecAngle", "length mismatch: " + std::to_string(x.size()) + " vs " + std::to_string(y.size()), line);
        const double nx = norm2_10(x.data(), x.size()), ny = norm2_10(y.data(), y.size());
        if (nx == 0.0 || ny == 0.0) failRun10("vecAngle", "angle with the zero vector is undefined", line);
        std::vector<double> u(x.size()), v(x.size());
        for (size_t i = 0; i < x.size(); ++i) { u[i] = x[i] / nx; v[i] = y[i] / ny; }
        std::vector<double> s(x.size()), d(x.size());
        for (size_t i = 0; i < x.size(); ++i) { s[i] = u[i] + v[i]; d[i] = u[i] - v[i]; }
        return N10(2.0 * std::atan2(norm2_10(d.data(), d.size()), norm2_10(s.data(), s.size())));
    };
    natives["vecProject"] = [](Args& a, int line) -> Value {
        expectArgs("vecProject", a, 2, line);
        auto x = readVec10(a[0], "vecProject", "a", line), y = readVec10(a[1], "vecProject", "onto", line);
        if (x.size() != y.size()) failArg10("vecProject", "length mismatch: " + std::to_string(x.size()) + " vs " + std::to_string(y.size()), line);
        const double yy = dot2_10(y.data(), 1, y.data(), 1, y.size());
        if (yy == 0.0) failRun10("vecProject", "cannot project onto the zero vector", line);
        const double k = dot2_10(x.data(), 1, y.data(), 1, x.size()) / yy;
        for (double& e : y) e *= k;
        return vecVal(y);
    };
    natives["vecLinspace"] = [](Args& a, int line) -> Value {
        expectArgsRange("vecLinspace", a, 3, 4, line);
        const double lo = needNum10(a[0], "vecLinspace", "start", line), hi = needNum10(a[1], "vecLinspace", "stop", line);
        const size_t n = needDim10(a[2], "vecLinspace", "n", line);
        if (n > 50000000) failArg10("vecLinspace", "n too large", line);
        const bool endpoint = a.size() == 4 ? a[3].isTruthy() : true;
        std::vector<double> v(n);
        for (size_t i = 0; i < n; ++i) {
            if (n == 1) { v[i] = lo; continue; }
            const double t = static_cast<double>(i) / static_cast<double>(endpoint ? n - 1 : n);
            v[i] = (endpoint && i == n - 1) ? hi : lo + (hi - lo) * t;
        }
        return vecVal(v);
    };

    // ======================================================== التحليلات
    // matLU(M) -> {L, U, P, perm, sign, singular}   حيث P·M = L·U (تدوير جزئي)
    natives["matLU"] = [](Args& a, int line) -> Value {
        expectArgs("matLU", a, 1, line);
        Mat M = readMat10(a[0], "matLU", "M", line, true); requireSquare10(M, "matLU", "M", line);
        LU10 f = luDecompose10(M);
        const size_t n = M.r; Mat L(n, n), U(n, n), P(n, n); std::vector<double> perm(n);
        for (size_t i = 0; i < n; ++i) { for (size_t j = 0; j < n; ++j) { if (j < i) L(i, j) = f.lu(i, j); else U(i, j) = f.lu(i, j); } L(i, i) = 1.0; P(i, f.perm[i]) = 1.0; perm[i] = static_cast<double>(f.perm[i]); }
        auto m = std::make_shared<MapData>();
        put10(m, "L", matVal(L)); put10(m, "U", matVal(U)); put10(m, "P", matVal(P)); put10(m, "perm", vecVal(perm));
        put10(m, "sign", N10(f.sign)); put10(m, "singular", B10(f.singular));
        return Value::makeMap(m);
    };
    // matQR(M[, {reduced:true}]) -> {Q, R}
    natives["matQR"] = [](Args& a, int line) -> Value {
        expectArgsRange("matQR", a, 1, 2, line);
        Mat M = readMat10(a[0], "matQR", "M", line, true);
        const Value* o = optMap10(a, 1, "matQR", line);
        QR10 f = qrHouseholder10(M);
        if (optBool10(o, "reduced", false)) {
            const size_t k = std::min(M.r, M.c);
            Mat Q(M.r, k), R(k, M.c);
            for (size_t i = 0; i < M.r; ++i) for (size_t j = 0; j < k; ++j) Q(i, j) = f.Q(i, j);
            for (size_t i = 0; i < k; ++i) for (size_t j = 0; j < M.c; ++j) R(i, j) = f.R(i, j);
            f.Q = Q; f.R = R;
        }
        auto m = std::make_shared<MapData>(); put10(m, "Q", matVal(f.Q)); put10(m, "R", matVal(f.R));
        return Value::makeMap(m);
    };
    // matCholesky(M) -> L (سفلية) حيث M = L·Lᵀ ؛ خطأ إن لم تكن متناظرة موجبة التحديد
    natives["matCholesky"] = [](Args& a, int line) -> Value {
        expectArgs("matCholesky", a, 1, line);
        Mat M = readMat10(a[0], "matCholesky", "M", line, true); requireSquare10(M, "matCholesky", "M", line);
        Mat L;
        if (!isSymmetric10(M, 1e-10)) failRun10("matCholesky", "matrix is not symmetric", line);
        if (!tryCholesky10(M, L, 1e-10)) failRun10("matCholesky", "matrix is not positive definite", line);
        return matVal(L);
    };
    // matEigSym(M) -> {values (تنازلياً), vectors (الأعمدة)}
    natives["matEigSym"] = [](Args& a, int line) -> Value {
        expectArgs("matEigSym", a, 1, line);
        Mat M = readMat10(a[0], "matEigSym", "M", line, true); requireSquare10(M, "matEigSym", "M", line);
        if (!isSymmetric10(M, 1e-9)) failRun10("matEigSym", "matrix is not symmetric — use matEigvals for general matrices", line);
        Eig10 e = jacobiEig10(M);
        auto m = std::make_shared<MapData>();
        put10(m, "values", vecVal(e.w)); put10(m, "vectors", matVal(e.V)); put10(m, "sweeps", N10(e.sweeps));
        return Value::makeMap(m);
    };
    // matEigvals(M[, {real:true}]) -> [{re,im}, ...] مرتّبة بالقيمة المطلقة تنازلياً
    natives["matEigvals"] = [](Args& a, int line) -> Value {
        expectArgsRange("matEigvals", a, 1, 2, line);
        Mat M = readMat10(a[0], "matEigvals", "M", line, true); requireSquare10(M, "matEigvals", "M", line);
        const Value* o = optMap10(a, 1, "matEigvals", line);
        std::vector<Cx10> ev;
        if (!eigvals10(M, ev)) failRun10("matEigvals", "QR iteration did not converge", line);
        std::stable_sort(ev.begin(), ev.end(), [](const Cx10& p, const Cx10& q) {
            const double mp = std::hypot(p.re, p.im), mq = std::hypot(q.re, q.im);
            if (std::fabs(mp - mq) > 1e-12 * std::max(mp, mq)) return mp > mq;
            if (std::fabs(p.re - q.re) > 1e-12 * std::max(1.0, std::fabs(p.re))) return p.re > q.re;
            return p.im > q.im;
        });
        const double tolIm = 1e-12;
        if (optBool10(o, "real", false)) {
            std::vector<double> r;
            for (const auto& c : ev) { if (std::fabs(c.im) > tolIm * std::max(1.0, std::fabs(c.re))) failRun10("matEigvals", "matrix has complex eigenvalues (drop {real:true})", line); r.push_back(c.re); }
            return vecVal(r);
        }
        auto arr = std::make_shared<ArrayData>();
        for (const auto& c : ev) { auto m = std::make_shared<MapData>(); put10(m, "re", N10(c.re)); put10(m, "im", N10(std::fabs(c.im) <= tolIm * std::max(1.0, std::fabs(c.re)) ? 0.0 : c.im)); arr->push_back(Value::makeMap(m)); }
        return Value::makeArray(arr);
    };
    // matEig(M) -> {values:[{re,im}], vectors:[متجه | nil]} — قيم ومتجهات ذاتية لمصفوفة حقيقية عامة.
    // المتجه الذاتي (طوله 1، أكبر مركّبة موجبة) يُحسب للقيمة الحقيقية من فضاء صفر (M-λI) عبر SVD؛ nil للقيم المركّبة.
    natives["matEig"] = [](Args& a, int line) -> Value {
        expectArgs("matEig", a, 1, line);
        Mat M = readMat10(a[0], "matEig", "M", line, true); requireSquare10(M, "matEig", "M", line);
        std::vector<Cx10> ev;
        if (!eigvals10(M, ev)) failRun10("matEig", "QR iteration did not converge", line);
        std::stable_sort(ev.begin(), ev.end(), [](const Cx10& p, const Cx10& q) {
            const double mp = std::hypot(p.re, p.im), mq = std::hypot(q.re, q.im);
            if (std::fabs(mp - mq) > 1e-12 * std::max(mp, mq)) return mp > mq;
            if (std::fabs(p.re - q.re) > 1e-12 * std::max(1.0, std::fabs(p.re))) return p.re > q.re;
            return p.im > q.im;
        });
        const size_t n = M.r;
        auto vals = std::make_shared<ArrayData>(), vecs = std::make_shared<ArrayData>();
        for (const auto& c : ev) {
            const bool real = std::fabs(c.im) <= 1e-12 * std::max(1.0, std::fabs(c.re));
            auto mv = std::make_shared<MapData>(); put10(mv, "re", N10(c.re)); put10(mv, "im", N10(real ? 0.0 : c.im)); vals->push_back(Value::makeMap(mv));
            if (!real) { vecs->push_back(Value::nil()); continue; }
            Mat B = M; for (size_t i = 0; i < n; ++i) B(i, i) -= c.re;
            SVD10 f = svd10(B);
            std::vector<double> v(n); size_t big = 0;
            for (size_t i = 0; i < n; ++i) { v[i] = f.Vfull(i, n - 1); if (std::fabs(v[i]) > std::fabs(v[big]) + 1e-12) big = i; }
            if (v[big] < 0) for (double& e : v) e = -e;
            vecs->push_back(vecVal(v));
        }
        auto m = std::make_shared<MapData>(); put10(m, "values", Value::makeArray(vals)); put10(m, "vectors", Value::makeArray(vecs));
        return Value::makeMap(m);
    };
    // matSVD(M) -> {U, S (متجه)، V, rank}  M = U·diag(S)·Vᵀ
    natives["matSVD"] = [](Args& a, int line) -> Value {
        expectArgs("matSVD", a, 1, line);
        Mat M = readMat10(a[0], "matSVD", "M", line, true);
        SVD10 f = svd10(M);
        auto m = std::make_shared<MapData>();
        put10(m, "U", matVal(f.U)); put10(m, "S", vecVal(f.s)); put10(m, "V", matVal(f.V));
        put10(m, "rank", N10(static_cast<double>(rankFromSvd10(f, svdTol10(f, M.r, M.c, 0)))));
        return Value::makeMap(m);
    };

    // ======================================================== الحل
    // matSolve(A, b[, {info:true}]) — A مربعة: LU بتدوير جزئي (خطأ واضح إن كانت منفردة)؛ غير مربعة: أقل المربعات
    natives["matSolve"] = [](Args& a, int line) -> Value {
        expectArgsRange("matSolve", a, 2, 3, line);
        Mat A = readMat10(a[0], "matSolve", "A", line, true);
        Operand B = readOperand10(a[1], "matSolve", "b", line);
        const Value* o = optMap10(a, 2, "matSolve", line);
        if (B.m.r != A.r) failArg10("matSolve", "shape mismatch: A is " + dims10(A) + " but b has " + std::to_string(B.m.r) + " rows", line);
        Mat X; double rank = static_cast<double>(std::min(A.r, A.c));
        if (A.r == A.c) {
            LU10 f = luDecompose10(A); if (f.singular) singularFail10("matSolve", f, line);
            X = luSolve10(f, B.m);
        } else {
            SVD10 f = svd10(A); const double tol = svdTol10(f, A.r, A.c, 0);
            X = mul10(pinvFromSvd10(f, A.r, A.c, tol), B.m); rank = static_cast<double>(rankFromSvd10(f, tol));
        }
        if (!optBool10(o, "info", false)) return operandVal10(X, B.isVec);
        Mat R = mul10(A, X); for (size_t i = 0; i < R.d.size(); ++i) R.d[i] -= B.m.d[i];
        auto m = std::make_shared<MapData>();
        put10(m, "x", operandVal10(X, B.isVec)); put10(m, "residual", N10(norm2_10(R.d.data(), R.d.size()))); put10(m, "rank", N10(rank));
        put10(m, "method", S10(A.r == A.c ? "lu" : "svd-lstsq"));
        return Value::makeMap(m);
    };
    natives["matInv"] = [](Args& a, int line) -> Value {
        expectArgs("matInv", a, 1, line);
        Mat M = readMat10(a[0], "matInv", "M", line, true); requireSquare10(M, "matInv", "M", line);
        LU10 f = luDecompose10(M); if (f.singular) singularFail10("matInv", f, line);
        return matVal(luSolve10(f, identity10(M.r)));
    };
    natives["matDet"] = [](Args& a, int line) -> Value {
        expectArgs("matDet", a, 1, line);
        Mat M = readMat10(a[0], "matDet", "M", line, true); requireSquare10(M, "matDet", "M", line);
        return N10(det10(M));
    };
    // matRank(M[, tol])
    natives["matRank"] = [](Args& a, int line) -> Value {
        expectArgsRange("matRank", a, 1, 2, line);
        Mat M = readMat10(a[0], "matRank", "M", line, true);
        SVD10 f = svd10(M);
        return N10(static_cast<double>(rankFromSvd10(f, svdTol10(f, M.r, M.c, a.size() == 2 ? needNum10(a[1], "matRank", "tol", line) : 0))));
    };
    // matCond(M[, p=2]) — رقم الشرطية (Infinity للمنفردة)
    natives["matCond"] = [](Args& a, int line) -> Value {
        expectArgsRange("matCond", a, 1, 2, line);
        Mat M = readMat10(a[0], "matCond", "M", line, true);
        std::string kind = a.size() == 2 ? normKindArg10(a[1], "matCond", line) : "2";
        if (kind == "2") {
            SVD10 f = svd10(M); const size_t k = std::min(M.r, M.c);
            if (f.s.empty() || f.s[k - 1] <= 0.0) return N10(INFINITY);
            return N10(f.s[0] / f.s[k - 1]);
        }
        requireSquare10(M, "matCond", "M", line);
        LU10 f = luDecompose10(M); if (f.singular) return N10(INFINITY);
        Mat inv = luSolve10(f, identity10(M.r));
        return N10(matNormKind10(M, kind, "matCond", line) * matNormKind10(inv, kind, "matCond", line));
    };
    // matPinv(M[, tol]) — شبه معكوس مور–بنروز
    natives["matPinv"] = [](Args& a, int line) -> Value {
        expectArgsRange("matPinv", a, 1, 2, line);
        Mat M = readMat10(a[0], "matPinv", "M", line, true);
        SVD10 f = svd10(M);
        return matVal(pinvFromSvd10(f, M.r, M.c, svdTol10(f, M.r, M.c, a.size() == 2 ? needNum10(a[1], "matPinv", "tol", line) : 0)));
    };
    // matLstsq(A, b) -> {x, residual, rank}  — أقل المربعات (أصغر نظيم عند نقص الرتبة)
    natives["matLstsq"] = [](Args& a, int line) -> Value {
        expectArgs("matLstsq", a, 2, line);
        Mat A = readMat10(a[0], "matLstsq", "A", line, true);
        Operand B = readOperand10(a[1], "matLstsq", "b", line);
        if (B.m.r != A.r) failArg10("matLstsq", "shape mismatch: A is " + dims10(A) + " but b has " + std::to_string(B.m.r) + " rows", line);
        SVD10 f = svd10(A); const double tol = svdTol10(f, A.r, A.c, 0);
        Mat X = mul10(pinvFromSvd10(f, A.r, A.c, tol), B.m);
        Mat R = mul10(A, X); for (size_t i = 0; i < R.d.size(); ++i) R.d[i] -= B.m.d[i];
        auto m = std::make_shared<MapData>();
        put10(m, "x", operandVal10(X, B.isVec)); put10(m, "residual", N10(norm2_10(R.d.data(), R.d.size())));
        put10(m, "rank", N10(static_cast<double>(rankFromSvd10(f, tol))));
        return Value::makeMap(m);
    };
    // matNull(M[, tol]) -> أعمدة أساس فضاء الصفر (n × (n-rank))، أو [] إن كان تافهاً
    natives["matNull"] = [](Args& a, int line) -> Value {
        expectArgsRange("matNull", a, 1, 2, line);
        Mat M = readMat10(a[0], "matNull", "M", line, true);
        SVD10 f = svd10(M); const double tol = svdTol10(f, M.r, M.c, a.size() == 2 ? needNum10(a[1], "matNull", "tol", line) : 0);
        const size_t rk = rankFromSvd10(f, tol), n = M.c;
        if (rk >= n) return Value::makeArray(std::make_shared<ArrayData>());
        Mat N(n, n - rk);
        for (size_t j = rk; j < n; ++j) for (size_t i = 0; i < n; ++i) N(i, j - rk) = f.Vfull(i, j);
        return matVal(N);
    };
    // matRref(M[, tol]) -> {rref, pivots, rank}
    natives["matRref"] = [](Args& a, int line) -> Value {
        expectArgsRange("matRref", a, 1, 2, line);
        Mat M = readMat10(a[0], "matRref", "M", line, true);
        const double tol = a.size() == 2 ? needNum10(a[1], "matRref", "tol", line) : static_cast<double>(std::max(M.r, M.c)) * kEps10 * std::max(maxAbs10(M), 1e-300);
        std::vector<double> piv; size_t row = 0;
        for (size_t col = 0; col < M.c && row < M.r; ++col) {
            size_t p = row; for (size_t i = row + 1; i < M.r; ++i) if (std::fabs(M(i, col)) > std::fabs(M(p, col))) p = i;
            if (std::fabs(M(p, col)) <= tol) { for (size_t i = row; i < M.r; ++i) M(i, col) = 0.0; continue; }
            for (size_t j = 0; j < M.c; ++j) std::swap(M(row, j), M(p, j));
            const double pv = M(row, col); for (size_t j = 0; j < M.c; ++j) M(row, j) /= pv;
            for (size_t i = 0; i < M.r; ++i) if (i != row) { const double f = M(i, col); if (f != 0.0) for (size_t j = 0; j < M.c; ++j) M(i, j) -= f * M(row, j); M(i, col) = 0.0; }
            piv.push_back(static_cast<double>(col)); ++row;
        }
        auto m = std::make_shared<MapData>();
        put10(m, "rref", matVal(M)); put10(m, "pivots", vecVal(piv)); put10(m, "rank", N10(static_cast<double>(piv.size())));
        return Value::makeMap(m);
    };
    // matSolveIter(A, b[, {method:"cg"|"jacobi"|"gauss-seidel"|"sor", tol, maxIter, omega, x0}])
    natives["matSolveIter"] = [](Args& a, int line) -> Value {
        expectArgsRange("matSolveIter", a, 2, 3, line);
        Mat A = readMat10(a[0], "matSolveIter", "A", line, true); requireSquare10(A, "matSolveIter", "A", line);
        std::vector<double> b = readVec10(a[1], "matSolveIter", "b", line, true);
        if (b.size() != A.r) failArg10("matSolveIter", "shape mismatch: A is " + dims10(A) + " but b has length " + std::to_string(b.size()), line);
        const Value* o = optMap10(a, 2, "matSolveIter", line);
        std::string method = optStr10(o, "method", "cg");
        const double tol = optNum10(o, "tol", 1e-10, "matSolveIter", line), omega = optNum10(o, "omega", 1.5, "matSolveIter", line);
        const size_t n = A.r; const long maxIter = static_cast<long>(optNum10(o, "maxIter", std::max<double>(1000, 10.0 * n), "matSolveIter", line));
        std::vector<double> x(n, 0.0);
        if (o) if (const Value* x0 = get10(*o, "x0")) { x = readVec10(*x0, "matSolveIter", "x0", line, true); if (x.size() != n) failArg10("matSolveIter", "x0 has the wrong length", line); }
        const double bn = std::max(norm2_10(b.data(), n), std::numeric_limits<double>::min());
        auto residual = [&](const std::vector<double>& xx) { std::vector<double> r(n); for (size_t i = 0; i < n; ++i) r[i] = b[i] - dot2_10(&A(i, 0), 1, xx.data(), 1, n); return r; };
        long it = 0; bool conv = false; double res = 0;
        if (method == "cg") {
            for (size_t i = 0; i < n; ++i) if (std::fabs(A(i, i)) <= 0) failRun10("matSolveIter", "method 'cg' needs a symmetric positive-definite matrix (zero diagonal found)", line);
            if (!isSymmetric10(A, 1e-9)) failRun10("matSolveIter", "method 'cg' needs a symmetric matrix", line);
            std::vector<double> r = residual(x), p = r; double rs = dot2_10(r.data(), 1, r.data(), 1, n);
            res = std::sqrt(rs) / bn;
            for (; it < maxIter && res > tol; ++it) {
                std::vector<double> Ap(n); for (size_t i = 0; i < n; ++i) Ap[i] = dot2_10(&A(i, 0), 1, p.data(), 1, n);
                const double pAp = dot2_10(p.data(), 1, Ap.data(), 1, n);
                if (!(pAp > 0)) failRun10("matSolveIter", "method 'cg' broke down: matrix is not positive definite", line);
                const double alpha = rs / pAp;
                for (size_t i = 0; i < n; ++i) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
                const double rs2 = dot2_10(r.data(), 1, r.data(), 1, n);
                for (size_t i = 0; i < n; ++i) p[i] = r[i] + (rs2 / rs) * p[i];
                rs = rs2; res = std::sqrt(rs) / bn;
            }
        } else if (method == "jacobi" || method == "gauss-seidel" || method == "sor") {
            for (size_t i = 0; i < n; ++i) if (A(i, i) == 0.0) failRun10("matSolveIter", "zero on the diagonal at row " + std::to_string(i) + " — reorder the equations", line);
            const double w = method == "sor" ? omega : 1.0;
            if (method == "sor" && !(w > 0 && w < 2)) failArg10("matSolveIter", "'omega' must be in (0, 2)", line);
            res = norm2_10(residual(x).data(), n) / bn;
            for (; it < maxIter && res > tol; ++it) {
                if (method == "jacobi") {
                    std::vector<double> nx(n);
                    for (size_t i = 0; i < n; ++i) { double s = b[i]; for (size_t j = 0; j < n; ++j) if (j != i) s -= A(i, j) * x[j]; nx[i] = s / A(i, i); }
                    x = nx;
                } else {
                    for (size_t i = 0; i < n; ++i) { double s = b[i]; for (size_t j = 0; j < n; ++j) if (j != i) s -= A(i, j) * x[j]; x[i] = (1.0 - w) * x[i] + w * s / A(i, i); }
                }
                std::vector<double> r = residual(x); res = norm2_10(r.data(), n) / bn;
                if (!std::isfinite(res)) failRun10("matSolveIter", "method '" + method + "' diverged (try 'cg' for SPD systems, or make A diagonally dominant)", line);
            }
        } else failArg10("matSolveIter", "unknown method '" + method + "' (cg, jacobi, gauss-seidel, sor)", line);
        conv = res <= tol;
        auto m = std::make_shared<MapData>();
        put10(m, "x", vecVal(x)); put10(m, "iterations", N10(static_cast<double>(it))); put10(m, "residual", N10(res)); put10(m, "converged", B10(conv)); put10(m, "method", S10(method));
        return Value::makeMap(m);
    };
    // matPowerIter(M[, {tol, maxIter, seed}]) -> {value, vector, iterations, converged} — القيمة الذاتية المهيمنة
    natives["matPowerIter"] = [](Args& a, int line) -> Value {
        expectArgsRange("matPowerIter", a, 1, 2, line);
        Mat M = readMat10(a[0], "matPowerIter", "M", line, true); requireSquare10(M, "matPowerIter", "M", line);
        const Value* o = optMap10(a, 1, "matPowerIter", line);
        const double tol = optNum10(o, "tol", 1e-12, "matPowerIter", line); const long maxIter = static_cast<long>(optNum10(o, "maxIter", 10000, "matPowerIter", line));
        const size_t n = M.r; std::vector<double> x(n);
        std::mt19937_64 g(seedMix10(static_cast<uint64_t>(optNum10(o, "seed", 1, "matPowerIter", line))));
        for (size_t i = 0; i < n; ++i) x[i] = 0.5 + static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0);
        double nx = norm2_10(x.data(), n); for (double& e : x) e /= nx;
        double lam = 0, prev = 0; long it = 0; bool conv = false;
        for (; it < maxIter; ++it) {
            std::vector<double> y(n); for (size_t i = 0; i < n; ++i) y[i] = dot2_10(&M(i, 0), 1, x.data(), 1, n);
            lam = dot2_10(x.data(), 1, y.data(), 1, n);
            const double ny = norm2_10(y.data(), n);
            if (ny == 0.0) { lam = 0; conv = true; break; }
            for (size_t i = 0; i < n; ++i) y[i] /= ny;
            double dmin = 0, dmax = 0; for (size_t i = 0; i < n; ++i) { dmin += std::fabs(y[i] - x[i]); dmax += std::fabs(y[i] + x[i]); }
            x = y;
            if (std::min(dmin, dmax) < tol && std::fabs(lam - prev) <= tol * std::max(1.0, std::fabs(lam))) { conv = true; break; }
            prev = lam;
        }
        size_t big = 0; for (size_t i = 0; i < n; ++i) if (std::fabs(x[i]) > std::fabs(x[big])) big = i;
        if (x[big] < 0) for (double& e : x) e = -e;
        auto m = std::make_shared<MapData>();
        put10(m, "value", N10(lam)); put10(m, "vector", vecVal(x)); put10(m, "iterations", N10(static_cast<double>(it))); put10(m, "converged", B10(conv));
        return Value::makeMap(m);
    };
}

} // namespace rin
