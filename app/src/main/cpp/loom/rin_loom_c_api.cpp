// loom/rin_loom_c_api.cpp
#include "rin_loom_c_api.h"
#include "rin_loom_pipeline.h"
#include <cstring>
#include <cstdlib>
#include <sstream>

namespace {
char* dupToC(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}
}

extern "C" {

RIN_API char* rin_loom_render_json(const char* source, int rootWidth) {
    std::string src = source ? source : "";
    if (rootWidth <= 0) rootWidth = 390;

    loom::PipelineResult r = loom::runColdPipeline(src);
    if (!r.ok) {
        std::ostringstream os;
        os << "{\"error\":\"" << loom::jsonEscape(r.errorMessage) << "\",\"line\":" << r.errorLine << "}";
        return dupToC(os.str());
    }

    loom::Loom loomEngine;
    loomEngine.layout(r.fabric, loom::Constraints{0, (double)rootWidth, 0, 1e9}, 0, 0);

    std::ostringstream os;
    os << "{\"ok\":true,\"strandsMeasured\":" << loomEngine.stats.strandsMeasured
       << ",\"cacheHits\":" << loomEngine.stats.cacheHits
       << ",\"fabric\":" << loom::fabricToJsonString(r.fabric) << "}";
    return dupToC(os.str());
}

} // extern "C"
