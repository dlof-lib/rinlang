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

// ---------------------------------------------------------------------
// Loomtime *session*: unlike rin_loom_render_json (a stateless one-shot render), a session keeps
// its Fabric + Warp state alive across calls, so a tap can actually mutate state in place (via
// Needle -- see rin_loom_needle.h) and only the affected Strands are re-resolved, instead of the
// whole preview being rebuilt from scratch. This is what lets an `onTap` handler (including one
// backed by a real `fun` with a `while` loop) actually do something in the live preview.

// Creates a session from `source`, laid out at `rootWidth` px. Always release with
// rin_loom_session_free, even if creation failed to parse (check via rin_loom_session_render_json).
RIN_API void* rin_loom_session_create(const char* source, int rootWidth);

// Current Fabric snapshot, same JSON shape as rin_loom_render_json. Free with rin_free_string().
RIN_API char* rin_loom_session_render_json(void* session);

// Dispatches a tap at (x, y) — same pixel space as the session's rootWidth. Finds the topmost
// Strand under the point that declared onTap=...;, runs it for real (a matching top-level `fun`
// if one exists, else a small set of built-in Warp ops: increment/decrement/toggle/set), applies
// any resulting Warp changes to the Fabric, and re-lays-out. Returns:
//   {"ok":true,"handled":bool,"targetId":N,"changed":["name",...],"error"?:"...","fabric":{...}}
// `error` is only present if a handler was found but failed at runtime (e.g. wrong arg count).
// Free with rin_free_string().
RIN_API char* rin_loom_session_tap(void* session, double x, double y);

// Re-parses `newSource` and diffs it against the session's existing Fabric in place (Shuttle),
// keeping all current Warp values (so state like a tapped counter survives editing elsewhere in
// the file). On a parse error the previous Fabric is left completely untouched (Snag containment)
// and the returned JSON still carries "fabric" (the last-good frame) alongside an "error"/"line".
// Free with rin_free_string().
RIN_API char* rin_loom_session_update_source(void* session, const char* newSource);

// Releases a session created by rin_loom_session_create.
RIN_API void rin_loom_session_free(void* session);

#ifdef __cplusplus
}
#endif
