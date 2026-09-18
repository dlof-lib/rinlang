// indsin/rin_indsin_c_api.h — Flat C ABI for the Indsintime rendering engine, extending rin_c_api.h's
// existing conventions (malloc'd char* results freed via rin_free_string, extern "C").
#pragma once
#include "../rin_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runs the cold Indsintime pipeline (Lexer -> Parser -> Warp seeding -> Fabric -> Indsin -> Dye)
// on `source`, laying the root out at `rootWidth` px, and returns a JSON dump of the Fabric
// (kind/name/line/geometry/resolved attrs, recursively) — or a JSON error object of the form
// {"error":"...", "line":N} if the source has no @view root or fails to parse.
// Free the result with rin_free_string().
RIN_API char* rin_indsin_render_json(const char* source, int rootWidth);

// Same as rin_indsin_render_json, but builds the Fabric from the @view root that lives *inside* a
// named @container (rather than the top-level program) — the container-scoped counterpart that
// makes Indsintime actually tied to `container`: every @container carrying its own @view/warp/
// @theme becomes an independently renderable screen/component, scoped to that container's own
// warp state. Returns a JSON error object if no container/group/volume with that name exists, or
// it has no @view root inside it. Free the result with rin_free_string().
RIN_API char* rin_indsin_render_container_json(const char* source, const char* containerName, int rootWidth);

// ---------------------------------------------------------------------
// Indsintime *session*: unlike rin_indsin_render_json (a stateless one-shot render), a session keeps
// its Fabric + Warp state alive across calls, so a tap can actually mutate state in place (via
// Needle -- see rin_indsin_needle.h) and only the affected Strands are re-resolved, instead of the
// whole preview being rebuilt from scratch. This is what lets an `onTap` handler (including one
// backed by a real `fun` with a `while` loop) actually do something in the live preview.

// Creates a session from `source`, laid out at `rootWidth` px. Always release with
// rin_indsin_session_free, even if creation failed to parse (check via rin_indsin_session_render_json).
RIN_API void* rin_indsin_session_create(const char* source, int rootWidth);

// Container-scoped counterpart of rin_indsin_session_create: the live session's Fabric/Warp state
// is seeded from the named @container's own body instead of the top-level program. Always
// release with rin_indsin_session_free, even if creation failed to parse.
RIN_API void* rin_indsin_session_create_for_container(const char* source, const char* containerName, int rootWidth);

// Overlay Engine (rin_indsin_overlay.h): sets the *viewport* height (the visible screen) used to
// center an open Dialog / anchor a Tooltip -- a different quantity from rootWidth's paired height,
// which is left unbounded (1e9) everywhere else in this engine because the rest of the Indsin lays
// out a scrollable design-time canvas, not a fixed screen. Defaults to 844 (a common phone
// viewport height) if never called. Triggers an immediate re-layout + overlay pass; call again
// whenever the host's visible area actually changes (e.g. rotation).
RIN_API void rin_indsin_session_set_viewport(void* session, int viewportHeight);

// Current Fabric snapshot plus the exact native Dye paint plan (`paint` array), same JSON shape as rin_indsin_render_json. Free with rin_free_string().
RIN_API char* rin_indsin_session_render_json(void* session);

// Dispatches a tap at (x, y) — same pixel space as the session's rootWidth. Finds the topmost
// Strand under the point that declared onTap=...;, runs it for real (a matching top-level `fun`
// if one exists, else a small set of built-in Warp ops: increment/decrement/toggle/set), applies
// any resulting Warp changes to the Fabric, and re-lays-out. Returns:
//   {"ok":true,"handled":bool,"targetId":N,"changed":["name",...],"error"?:"...","fabric":{...}}
// `error` is only present if a handler was found but failed at runtime (e.g. wrong arg count).
// Free with rin_free_string().
RIN_API char* rin_indsin_session_tap(void* session, double x, double y);

// ---- Events: long-press / double-tap / hover (rin_indsin_needle.h's dispatchLongPress/
// dispatchDoubleTap/dispatchHover, Overlay-aware) — same shape/semantics as rin_indsin_session_tap
// above, just against a different attribute (`onLongPress=`/`onDoubleTap=`/`onHoverEnter=` or
// `onHoverExit=`) instead of `onTap=`. A hit with no matching handler on that attribute simply
// reports `"handled":false` (not an error) — see dispatchGestureAttr's own doc comment. Free every
// result with rin_free_string().

// Dispatches a long-press at (x, y). The host is responsible for actually detecting a long-press
// gesture (hold duration) — this only resolves *what* to run once told one landed here.
RIN_API char* rin_indsin_session_long_press(void* session, double x, double y);

// Dispatches a double-tap at (x, y). Same caveat as long-press: gesture *detection* (two taps
// within a time/distance window) is the host's job.
RIN_API char* rin_indsin_session_double_tap(void* session, double x, double y);

// Dispatches a hover enter (entering=1) or exit (entering=0) at (x, y). Meant for a pointer/mouse/
// stylus host (Mirror Indsin desktop preview, or a mouse-driven Android device) — the host tracks
// which Strand it last considered hovered and calls this with entering=0 on it before calling it
// with entering=1 on whatever's under the pointer now, mirroring a real UI toolkit's enter/exit pair.
RIN_API char* rin_indsin_session_hover(void* session, double x, double y, int entering);

// ---- Effects: advances the session's animation clock (rin_indsin_effects.h's EffectRuntime) to
// "now" and re-applies every animating Strand's current opacity/translate/scale frame, WITHOUT
// dispatching any gesture — this is what a host's per-frame callback (Choreographer on Android,
// requestAnimationFrame-equivalent elsewhere) calls in a loop while an enter transition (effect=
// on some Strand) is still short of its `duration=`. Same JSON envelope shape as
// rin_indsin_session_tap (with "handled":false, "changed":[]), plus one more top-level field:
// `"animating":bool` — true if at least one Strand is still short of its full duration, i.e. the
// host should schedule another tick; false means it's safe to stop the per-frame loop until the
// next real interaction (tap/long-press/.../update_source) starts a fresh animation. Free with
// rin_free_string().
RIN_API char* rin_indsin_session_tick(void* session);

// Re-parses `newSource` and diffs it against the session's existing Fabric in place (Shuttle),
// keeping all current Warp values (so state like a tapped counter survives editing elsewhere in
// the file). On a parse error the previous Fabric is left completely untouched (Snag containment)
// and the returned JSON still carries "fabric" (the last-good frame) alongside an "error"/"line".
// Free with rin_free_string().
RIN_API char* rin_indsin_session_update_source(void* session, const char* newSource);

// Releases a session created by rin_indsin_session_create.
RIN_API void rin_indsin_session_free(void* session);

// ---------------------------------------------------------------------
// Native raster access (Desktop client — see tools/rin_indsin_desktop.cpp): unlike
// rin_indsin_session_render_json's "paint" array (a DrawCommand list a *host* renderer walks and
// draws itself, e.g. IndsinFabricView.kt's Canvas), these two return/consume the actual flat pixel
// buffer produced by the engine's own rasterizer (rin_indsin_paint.h's rasterizeToBuffer/writePNG —
// the ONE rasterizer in the engine; nothing here re-implements paint logic). This is what lets a
// dependency-free desktop window (Xlib, no Skia/Cairo/GL) blit a genuinely-rendered frame with
// XPutImage, and what lets the same tool dump a headless screenshot with no display at all.

// Rasterizes the session's current Fabric+Overlay (same paintWithOverlay() pass
// rin_indsin_session_render_json uses) into a malloc'd RGB888 buffer (row-major, top-to-bottom,
// 3 bytes/pixel). *outW receives rootWidth; *outH receives the Fabric's measured content height.
// Returns nullptr (and 0/0 in outW/outH) if the session has no valid Fabric. Free the result with
// rin_indsin_free_buffer().
RIN_API unsigned char* rin_indsin_session_render_rgb(void* session, int* outW, int* outH);

// Releases a buffer returned by rin_indsin_session_render_rgb.
RIN_API void rin_indsin_free_buffer(unsigned char* buf);

// Rasterizes the session's current Fabric+Overlay and writes it straight to a real PNG file at
// `path` (spec-valid PNG, same encoder as the Container-export path) — a headless equivalent of
// the Desktop client's window, for CI/screenshot use with no X server involved. Returns 1 on
// success, 0 on failure (no Fabric, or the file could not be written).
RIN_API int rin_indsin_session_export_png(void* session, const char* path);

#ifdef __cplusplus
}
#endif
