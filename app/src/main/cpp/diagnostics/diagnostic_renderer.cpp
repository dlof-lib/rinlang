#include "diagnostic_renderer.h"
#include <sstream>
#include <algorithm>

namespace rin::diag {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jsonArray(const std::vector<std::string>& items) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) os << ",";
        os << "\"" << jsonEscape(items[i]) << "\"";
    }
    os << "]";
    return os.str();
}

} // namespace

std::string renderPlain(const Diagnostic& d, const SourceManager& sm) {
    std::ostringstream os;
    const std::string code = codeString(d.code);
    const std::string& loc = d.location.file;

    // العنوان: error[E0005]: type mismatch
    os << severityName(d.severity) << "[" << code << "]: " << d.message << "\n";
    os << "  --> " << loc << ":" << d.location.startLine << ":" << d.location.startCol << "\n";

    std::string lineText = sm.getLine(loc, d.location.startLine);
    if (!lineText.empty() || sm.hasFile(loc)) {
        std::string numStr = std::to_string(d.location.startLine);
        size_t gutter = std::max<size_t>(numStr.size(), 1);
        std::string pad(gutter, ' ');

        os << "\n";
        os << " " << numStr << " | " << lineText << "\n";
        os << " " << pad << " | ";
        int col = std::max(1, d.location.startCol);
        for (int i = 1; i < col; ++i) os << ' ';
        int width = d.location.caretWidth();
        for (int i = 0; i < width; ++i) os << '^';
        os << "\n";
    }

    if (d.reason) {
        os << "\nreason:\n  " << *d.reason << "\n";
    }
    if (d.found) {
        os << "\nfound:\n  " << *d.found << "\n";
    }
    if (d.expected) {
        os << "\nexpected:\n  " << *d.expected << "\n";
    }
    for (const auto& cause : d.causedBy) {
        os << "\nCaused by:\n  " << cause << "\n";
    }
    for (const auto& note : d.notes) {
        os << "\nnote:\n  " << note << "\n";
    }
    if (!d.suggestions.empty()) {
        os << "\n" << (d.suggestions.size() > 1 ? "possible matches:\n" : "did you mean:\n");
        for (const auto& s : d.suggestions) os << "  `" << s << "`\n";
    }
    for (const auto& hint : d.hints) {
        os << "\nhelp:\n  " << hint << "\n";
    }
    return os.str();
}

std::string renderShort(const Diagnostic& d) {
    std::ostringstream os;
    os << d.location.file << ":" << d.location.startLine << ":" << d.location.startCol
       << ": " << severityName(d.severity) << "[" << codeString(d.code) << "]: " << d.message;
    return os.str();
}

std::string renderJson(const Diagnostic& d) {
    std::ostringstream os;
    os << "{"
       << "\"severity\":\"" << severityName(d.severity) << "\","
       << "\"code\":\"" << codeString(d.code) << "\","
       << "\"codeName\":\"" << codeName(d.code) << "\","
       << "\"message\":\"" << jsonEscape(d.message) << "\","
       << "\"file\":\"" << jsonEscape(d.location.file) << "\","
       << "\"line\":" << d.location.startLine << ","
       << "\"column\":" << d.location.startCol << ","
       << "\"endLine\":" << d.location.endLine << ","
       << "\"endColumn\":" << d.location.endCol;
    if (d.reason)   os << ",\"reason\":\""   << jsonEscape(*d.reason)   << "\"";
    if (d.expected) os << ",\"expected\":\"" << jsonEscape(*d.expected) << "\"";
    if (d.found)    os << ",\"found\":\""    << jsonEscape(*d.found)    << "\"";
    os << ",\"notes\":" << jsonArray(d.notes);
    os << ",\"help\":" << jsonArray(d.hints);
    os << ",\"suggestions\":" << jsonArray(d.suggestions);
    os << ",\"causedBy\":" << jsonArray(d.causedBy);
    os << "}";
    return os.str();
}

std::string renderLsp(const Diagnostic& d) {
    // LSP: severity 1=Error,2=Warning,3=Information,4=Hint
    int sev = 1;
    switch (d.severity) {
        case Severity::Error:   sev = 1; break;
        case Severity::Warning: sev = 2; break;
        case Severity::Note:    sev = 3; break;
        case Severity::Help:    sev = 4; break;
    }
    std::ostringstream os;
    os << "{"
       << "\"range\":{"
       <<   "\"start\":{\"line\":" << (d.location.startLine - 1) << ",\"character\":" << (d.location.startCol - 1) << "},"
       <<   "\"end\":{\"line\":" << (d.location.endLine - 1) << ",\"character\":" << (d.location.endCol - 1) << "}"
       << "},"
       << "\"severity\":" << sev << ","
       << "\"code\":\"" << codeString(d.code) << "\","
       << "\"source\":\"rin\","
       << "\"message\":\"" << jsonEscape(d.message + (d.reason ? (" - " + *d.reason) : "")) << "\"";
    if (!d.hints.empty()) {
        os << ",\"relatedInformation\":[";
        for (size_t i = 0; i < d.hints.size(); ++i) {
            if (i) os << ",";
            os << "{\"message\":\"" << jsonEscape(d.hints[i]) << "\"}";
        }
        os << "]";
    }
    os << "}";
    return os.str();
}

std::string renderAll(const DiagnosticEngine& engine, const SourceManager& sm, OutputFormat fmt) {
    std::ostringstream os;
    const auto& items = engine.all();

    if (fmt == OutputFormat::Json || fmt == OutputFormat::Lsp) {
        os << "[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) os << ",";
            os << (fmt == OutputFormat::Json ? renderJson(items[i]) : renderLsp(items[i]));
        }
        os << "]";
        return os.str();
    }

    for (size_t i = 0; i < items.size(); ++i) {
        if (fmt == OutputFormat::Short) os << renderShort(items[i]) << "\n";
        else os << renderPlain(items[i], sm) << "\n";
    }

    if (fmt == OutputFormat::Plain) {
        int errs = engine.errorCount();
        int warns = engine.warningCount();
        if (errs > 0 && warns > 0) {
            os << errs << (errs == 1 ? " error" : " errors") << ", "
               << warns << (warns == 1 ? " warning" : " warnings") << " emitted\n";
        } else if (errs > 0) {
            os << errs << (errs == 1 ? " error" : " errors") << " emitted\n";
        } else if (warns > 0) {
            os << warns << (warns == 1 ? " warning" : " warnings") << " emitted\n";
        }
    }
    return os.str();
}

} // namespace rin::diag
