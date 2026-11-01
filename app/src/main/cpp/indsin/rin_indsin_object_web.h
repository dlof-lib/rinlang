// rin_indsin_object_web.h — C++ bridge between the Indsin engine and the indsin-object web page
// (tools/indsin-object/indsin_object.html). Header-only, C++17, no dependencies.
//
//  * Emscripten (WebAssembly in a browser/WebView): pushFrame() calls IndsinObject.applyFrame() directly.
//  * Native builds: install a sink with setSink() (e.g. forward to a WebView's evaluateJavascript);
//    the helper builds the exact script to run and escapes the frame as a JS string literal.
//
//   rin::indsin_object::pushFrame(frameJson);                  // frame from IndsinSession::frameJson()
//   rin::indsin_object::setSink([](const std::string& js) { /* webView.evaluateJavascript(js) */ });
#pragma once

#include <cstdio>
#include <functional>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace rin {
namespace indsin_object {

/// Escapes `s` as a double-quoted JavaScript string literal (safe inside <script> and eval).
inline std::string jsStringLiteral(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '<': out += "\\u003c"; break;   // never emit "</script"
            case '>': out += "\\u003e"; break;
            case '&': out += "\\u0026"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    // U+2028 / U+2029 (E2 80 A8 / E2 80 A9) terminate JS string literals in older engines
    for (size_t pos = 0; (pos = out.find("\xE2\x80\xA8", pos)) != std::string::npos;) out.replace(pos, 3, "\\u2028");
    for (size_t pos = 0; (pos = out.find("\xE2\x80\xA9", pos)) != std::string::npos;) out.replace(pos, 3, "\\u2029");
    out.push_back('"');
    return out;
}

/// The JavaScript statement that renders `frameJson` in the page.
inline std::string applyFrameScript(const std::string& frameJson) {
    return "IndsinObject.applyFrame(" + jsStringLiteral(frameJson) + ")";
}

/// The JavaScript statement that switches the page theme.
inline std::string setThemeScript(const std::string& theme) {
    return "IndsinObject.setTheme(" + jsStringLiteral(theme) + ")";
}

using ScriptSink = std::function<void(const std::string&)>;
inline ScriptSink& sink() { static ScriptSink s; return s; }
inline void setSink(ScriptSink s) { sink() = std::move(s); }

/// Runs `js` in the page: directly under Emscripten, otherwise through the installed sink.
/// Returns false when no route to a page is available.
inline bool run(const std::string& js) {
#ifdef __EMSCRIPTEN__
    emscripten_run_script(js.c_str());
    return true;
#else
    if (!sink()) return false;
    sink()(js);
    return true;
#endif
}

inline bool pushFrame(const std::string& frameJson) { return run(applyFrameScript(frameJson)); }
inline bool setTheme(const std::string& theme) { return run(setThemeScript(theme)); }

}  // namespace indsin_object
}  // namespace rin
