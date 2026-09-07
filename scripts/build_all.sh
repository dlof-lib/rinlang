#!/usr/bin/env bash
# ============================================================================
# scripts/build_all.sh — Unified build system for RinLang
# ============================================================================
# One entry point that drives the three existing-but-separate build paths this
# repo already has (Gradle for the Android app, Emscripten for the browser
# WASM engine, g++ for the new Desktop client) and collects every output under
# a single dist/ directory. It does not replace or duplicate any of those
# build paths — each target below runs the exact same commands the project's
# own CI workflows / scripts already use (.github/workflows/build_apk.yml's
# `gradle assembleRelease`, web/build_rinhtml_wasm.sh's emcc invocation,
# tools/README_DESKTOP.md's g++ command) — it only adds: toolchain detection
# (skip a target with a clear, actionable message instead of a wall of
# unrelated errors when its compiler/SDK isn't installed), a consistent
# dist/<target>/ output layout, and one CLI for all three.
#
# Usage:
#   scripts/build_all.sh apk [debug|release]   # default: debug
#   scripts/build_all.sh web
#   scripts/build_all.sh desktop
#   scripts/build_all.sh all [debug|release]   # builds every target whose
#                                               # toolchain is available;
#                                               # missing toolchains are
#                                               # skipped with a warning, not
#                                               # a hard failure, so `all`
#                                               # still succeeds on a
#                                               # single-purpose dev machine.
#   scripts/build_all.sh clean                 # removes dist/ and each
#                                               # target's own build output
#                                               # (app/build, web/rin_engine.*)
#
# Exit status: for a single explicit target (apk/web/desktop), a missing
# toolchain is a real failure (exit 1). For `all`, a missing toolchain is a
# skip (warning printed, exit stays 0 as long as at least one target built);
# `all` only exits non-zero if every target failed/was skipped.
# ============================================================================
set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="$REPO_DIR/app/src/main/cpp"
DIST="$REPO_DIR/dist"

log()  { echo "[build_all] $*"; }
warn() { echo "[build_all] $*" >&2; }

# ----------------------------------------------------------------------------
# APK — same task Gradle/CI already runs (.github/workflows/build_apk.yml uses
# `gradle assembleRelease`; this adds assembleDebug as the local-dev default
# since debug needs no signing keystore).
# ----------------------------------------------------------------------------
build_apk() {
    local variant="${1:-debug}"
    log "== APK ($variant) =="

    local gradle_cmd=""
    if [ -x "$REPO_DIR/gradlew" ]; then
        gradle_cmd="$REPO_DIR/gradlew"
    elif command -v gradle >/dev/null 2>&1; then
        gradle_cmd="gradle"
    else
        warn "SKIP apk: no ./gradlew wrapper and no system 'gradle' on PATH."
        warn "          Install Gradle (or run 'gradle wrapper' once to add ./gradlew)."
        return 1
    fi
    if [ -z "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}" ]; then
        warn "SKIP apk: neither ANDROID_HOME nor ANDROID_SDK_ROOT is set."
        warn "          Install the Android SDK + NDK and point one of those at it"
        warn "          (see .github/workflows/build_apk.yml's 'Set up Android SDK' /"
        warn "          'Install NDK & CMake' steps for the exact packages the CI uses)."
        return 1
    fi

    local task="assembleDebug"
    [ "$variant" = "release" ] && task="assembleRelease"

    ( cd "$REPO_DIR" && "$gradle_cmd" "$task" --no-daemon --stacktrace )
    local rc=$?
    if [ $rc -ne 0 ]; then
        warn "FAILED apk: gradle $task exited $rc."
        return 1
    fi

    local apk
    apk="$(find "$REPO_DIR/app/build/outputs/apk/$variant" -maxdepth 1 -name '*.apk' 2>/dev/null | head -n1)"
    if [ -z "$apk" ]; then
        warn "FAILED apk: gradle reported success but no .apk was found under app/build/outputs/apk/$variant/."
        return 1
    fi
    mkdir -p "$DIST/apk"
    cp "$apk" "$DIST/apk/"
    log "APK -> $DIST/apk/$(basename "$apk")"
    return 0
}

# ----------------------------------------------------------------------------
# Web (WASM) — delegates straight to the project's own web/build_rinhtml_wasm.sh
# (same emcc invocation, same source list) and copies its output into dist/.
# ----------------------------------------------------------------------------
build_web() {
    log "== Web (WASM) =="

    if ! command -v emcc >/dev/null 2>&1; then
        warn "SKIP web: emcc (Emscripten) not found on PATH."
        warn "          Install: https://emscripten.org/docs/getting_started/downloads.html"
        warn "          then 'source /path/to/emsdk/emsdk_env.sh' before re-running."
        return 1
    fi

    ( cd "$REPO_DIR/web" && ./build_rinhtml_wasm.sh )
    local rc=$?
    if [ $rc -ne 0 ]; then
        warn "FAILED web: build_rinhtml_wasm.sh exited $rc."
        return 1
    fi

    mkdir -p "$DIST/web"
    cp "$REPO_DIR/web/rin_engine.js" "$REPO_DIR/web/rin_engine.wasm" "$DIST/web/" 2>/dev/null
    [ -d "$REPO_DIR/web/rinhtml" ] && cp -r "$REPO_DIR/web/rinhtml" "$DIST/web/"
    [ -f "$REPO_DIR/web/index.html" ] && cp "$REPO_DIR/web/index.html" "$DIST/web/"
    log "Web -> $DIST/web/ (rin_engine.js + rin_engine.wasm)"
    return 0
}

# ----------------------------------------------------------------------------
# Desktop — the real X11 Loomtime window (tools/rin_loom_desktop.cpp). Same
# source list and flags documented in tools/README_DESKTOP.md.
# ----------------------------------------------------------------------------
build_desktop() {
    log "== Desktop (X11) =="

    local cxx=""
    if command -v g++ >/dev/null 2>&1; then cxx="g++"
    elif command -v clang++ >/dev/null 2>&1; then cxx="clang++"
    else
        warn "SKIP desktop: no g++ or clang++ on PATH."
        return 1
    fi

    if ! printf '#include <X11/Xlib.h>\nint main(){return 0;}\n' \
            | "$cxx" -std=c++17 -x c++ -o /dev/null - -lX11 >/dev/null 2>&1; then
        warn "SKIP desktop: X11 dev headers/libs not found."
        warn "              Debian/Ubuntu: sudo apt install libx11-dev"
        warn "              Fedora:        sudo dnf install libX11-devel"
        return 1
    fi

    mkdir -p "$DIST/desktop"
    "$cxx" -std=c++17 -O2 -I "$CORE" -o "$DIST/desktop/rin_loom_desktop" \
        "$REPO_DIR/tools/rin_loom_desktop.cpp" \
        "$CORE/loom/rin_loom_c_api.cpp" \
        "$CORE/rin_c_api.cpp" \
        "$CORE/rin_lexer.cpp" \
        "$CORE/rin_parser.cpp" \
        "$CORE/rin_make.cpp" \
        "$CORE/rin_interpreter.cpp" \
        "$CORE/loader_ui/library_loader_ui.cpp" \
        "$CORE/rin_http.cpp" \
        "$CORE/diagnostics/diagnostic.cpp" \
        "$CORE/diagnostics/source_manager.cpp" \
        "$CORE/diagnostics/diagnostic_engine.cpp" \
        "$CORE/diagnostics/diagnostic_renderer.cpp" \
        "$CORE/clc/clc_container.cpp" \
        "$CORE/clc/clc_compress.cpp" \
        "$CORE/clc/clc_security.cpp" \
        "$CORE/clc/clc_rin_opt.cpp" \
        "$CORE/clc/clc_zip_import.cpp" \
        "$CORE/clc/sha256.cpp" \
        -lz -lX11
    local rc=$?
    if [ $rc -ne 0 ]; then
        warn "FAILED desktop: compile/link exited $rc."
        return 1
    fi
    log "Desktop -> $DIST/desktop/rin_loom_desktop"
    return 0
}

clean_all() {
    log "removing $DIST"
    rm -rf "$DIST"
    log "removing app/build (Gradle outputs)"
    rm -rf "$REPO_DIR/app/build"
    log "removing web/rin_engine.js / web/rin_engine.wasm"
    rm -f "$REPO_DIR/web/rin_engine.js" "$REPO_DIR/web/rin_engine.wasm"
    log "clean done"
}

usage() {
    cat >&2 <<EOF
usage:
  $0 apk [debug|release]
  $0 web
  $0 desktop
  $0 all [debug|release]
  $0 clean
EOF
}

main() {
    local target="${1:-}"
    case "$target" in
        apk)     build_apk "${2:-debug}"; exit $? ;;
        web)     build_web; exit $? ;;
        desktop) build_desktop; exit $? ;;
        clean)   clean_all; exit 0 ;;
        all)
            local variant="${2:-debug}"
            local ok=0
            build_apk "$variant" && ok=$((ok+1))
            build_web && ok=$((ok+1))
            build_desktop && ok=$((ok+1))
            echo
            log "== summary =="
            [ -d "$DIST/apk" ]      && log "apk:     OK -> $DIST/apk/"      || log "apk:     skipped/failed"
            [ -d "$DIST/web" ]      && log "web:     OK -> $DIST/web/"      || log "web:     skipped/failed"
            [ -d "$DIST/desktop" ]  && log "desktop: OK -> $DIST/desktop/"  || log "desktop: skipped/failed"
            if [ "$ok" -eq 0 ]; then
                warn "all targets skipped or failed — see messages above for what to install."
                exit 1
            fi
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
}

main "$@"
