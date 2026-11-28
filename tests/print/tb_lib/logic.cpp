// مصدر C++ لأداة @Container.TB — يُترجَم عند أول نداء ويُنادى كأنه عضو في الأداة: Tool.tb_add(2, 3)
// دوال رقمية extern "C" (كلها double أو كلها int) · ودوال JSON بـ RIN_FN (تقبل أي قيمة وتُرجع أي قيمة)
#include "rin_abi.h"

extern "C" double tb_add(double a, double b) { return a + b; }
extern "C" long long tb_answer() { return 42; }

RIN_FN(tb_sum) {                       // args = مصفوفة الوسائط كما مرّرها Rin
    double t = 0;
    for (auto& v : args.arr) t += v.num;
    return rin::Json::number(t);
}
