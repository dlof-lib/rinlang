#include "rin_abi.h"
RIN_FN(greeter_sum) {
    double total = 1000;
    if (!args.arr.empty() && args.arr[0].type == rin::Json::Arr)
        for (auto& v : args.arr[0].arr) total += v.num;
    return rin::Json::number(total);
}
