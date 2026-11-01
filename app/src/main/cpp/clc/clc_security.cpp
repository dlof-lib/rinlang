#include "clc_security.h"
#include "clc_io.h"
#include <vector>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace clc {

std::string sanitizeEntryPath(const std::string& rawPath) {
    if (rawPath.empty()) throw ClcFormatError("empty entry path in container (rejected)");
    if (rawPath.find('\0') != std::string::npos)
        throw ClcFormatError("entry path contains NUL byte (rejected — possible attack)");
    if (rawPath.front() == '/' || rawPath.front() == '\\')
        throw ClcFormatError("absolute entry path rejected: " + rawPath);
    if (rawPath.size() >= 2 && rawPath[1] == ':')
        throw ClcFormatError("windows-style absolute path rejected: " + rawPath);

    // طبّع الفواصل ثم افحص كل مقطع.
    std::string norm = rawPath;
    for (auto& c : norm) if (c == '\\') c = '/';

    std::vector<std::string> parts;
    std::stringstream ss(norm);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..")
            throw ClcFormatError("path traversal ('..') rejected in entry: " + rawPath);
        parts.push_back(seg);
    }
    if (parts.empty()) throw ClcFormatError("entry path resolves to empty after normalization: " + rawPath);

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return out;
}

std::string resolveUnderRoot(const std::string& outDir, const std::string& sanitizedRelPath) {
    fs::path root = fs::absolute(fs::path(outDir)).lexically_normal();
    fs::path full = fs::absolute(root / sanitizedRelPath).lexically_normal();
    auto rootStr = root.string();
    auto fullStr = full.string();
    // full يجب أن يبدأ بـ root بالضبط (defense in depth حتى لو sanitizeEntryPath مرّر شيئاً).
    if (fullStr.compare(0, rootStr.size(), rootStr) != 0) {
        throw ClcFormatError("resolved path escapes output directory (rejected): " + sanitizedRelPath);
    }
    return fullStr;
}

} // namespace clc
