// loom/rin_loom_c_api.h — Flat C ABI for the Loomtime rendering engine, extending rin_c_api.h's
// existing conventions (malloc'd char* results freed via rin_free_string, extern "C").
#pragma once
#include "../rin_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runs the cold Loomtime pipeline (Lexer -> Parser -> Warp seeding -> Fabric -> Loom -> Dye)
// on `source`, laying the root out at `rootWidth` px, and returns a JSON dump of the Fabric
// (kind/name/line/geometry/resolved attrs, recursively) — or a JSON error object of the form
// {"error":"...", "line":N} if the source has no @view root or fails to parse.
// Free the result with rin_free_string().
RIN_API char* rin_loom_render_json(const char* source, int rootWidth);

#ifdef __cplusplus
}
#endif
