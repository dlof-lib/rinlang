// cli/linux/src/pkg/semver.cpp
#include "semver.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace rinpm {

std::string Version::toString() const {
    std::ostringstream ss;
    ss << major << '.' << minor << '.' << patch;
    if (!preRelease.empty()) ss << '-' << preRelease;
    return ss.str();
}

int Version::compare(const Version& o) const {
    if (major != o.major) return major < o.major ? -1 : 1;
    if (minor != o.minor) return minor < o.minor ? -1 : 1;
    if (patch != o.patch) return patch < o.patch ? -1 : 1;
    // SemVer: نسخة بلا pre-release أعلى من نفس الرقم مع pre-release.
    if (preRelease.empty() && o.preRelease.empty()) return 0;
    if (preRelease.empty()) return 1;
    if (o.preRelease.empty()) return -1;
    if (preRelease == o.preRelease) return 0;
    return preRelease < o.preRelease ? -1 : 1;
}

static bool isDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool tryParseVersion(const std::string& textIn, Version& out) {
    std::string text = textIn;
    // إزالة بادئة 'v' الشائعة (v1.2.3) تساهلاً، غير إلزامية في المواصفة لكنها شائعة الاستخدام.
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) text = text.substr(1);

    std::string core = text;
    std::string pre;
    auto dash = text.find('-');
    if (dash != std::string::npos) {
        core = text.substr(0, dash);
        pre = text.substr(dash + 1);
    }
    // دعم أيضاً "+build" في الذيل (يُتجاهل بصمت، غير مؤثر في المقارنة، توافقاً مع SemVer العام).
    auto plus = core.find('+');
    if (plus != std::string::npos) core = core.substr(0, plus);

    std::vector<std::string> parts;
    std::stringstream ss(core);
    std::string seg;
    while (std::getline(ss, seg, '.')) parts.push_back(seg);
    if (parts.size() != 3) return false;
    for (auto& p : parts) if (!isDigits(p)) return false;

    try {
        out.major = std::stoi(parts[0]);
        out.minor = std::stoi(parts[1]);
        out.patch = std::stoi(parts[2]);
    } catch (...) {
        return false;
    }
    out.preRelease = pre;
    return true;
}

Version parseVersion(const std::string& text) {
    Version v;
    if (!tryParseVersion(text, v)) {
        throw std::invalid_argument("invalid SemVer version: \"" + text + "\" (expected MAJOR.MINOR.PATCH)");
    }
    return v;
}

bool VersionConstraint::matches(const Version& v) const {
    if (matchesAny) return true;
    if (min) {
        int c = v.compare(*min);
        if (minInclusive ? (c < 0) : (c <= 0)) return false;
    }
    if (max) {
        int c = v.compare(*max);
        if (maxInclusive ? (c > 0) : (c >= 0)) return false;
    }
    return true;
}

std::string VersionConstraint::toString() const { return raw; }

static std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool tryParseConstraint(const std::string& textIn, VersionConstraint& out) {
    std::string text = trimStr(textIn);
    out = VersionConstraint{};
    out.raw = textIn;

    if (text.empty() || text == "*") {
        out.matchesAny = true;
        return true;
    }

    // 1.x / 1.2.x  (يسمح بحرف x أو X)
    {
        std::string lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lowered.find('x') != std::string::npos) {
            std::vector<std::string> parts;
            std::stringstream ss(lowered);
            std::string seg;
            while (std::getline(ss, seg, '.')) parts.push_back(seg);
            if (parts.empty() || parts.size() > 3) return false;
            // آخر عنصر يجب أن يكون 'x'؛ ما قبله أرقام صريحة.
            if (parts.back() != "x") return false;
            for (size_t i = 0; i + 1 < parts.size(); ++i) if (!isDigits(parts[i])) return false;
            int major = 0, minor = 0;
            try {
                if (parts.size() >= 1 && parts.size() - 1 >= 1) major = std::stoi(parts[0]);
                if (parts.size() == 3) minor = std::stoi(parts[1]);
            } catch (...) { return false; }
            Version lo{major, (parts.size() == 3 ? minor : 0), 0, ""};
            Version hi;
            if (parts.size() == 3) { hi = Version{major, minor + 1, 0, ""}; }
            else { hi = Version{major + 1, 0, 0, ""}; }
            out.min = lo; out.minInclusive = true;
            out.max = hi; out.maxInclusive = false;
            return true;
        }
    }

    char op0 = text[0];
    if (op0 == '^') {
        // يقبل ^1  و ^1.2  و ^1.2.3 (أجزاء ناقصة تُفهَم كأصفار، بأسلوب npm/cargo caret الشائع).
        std::string rest = text.substr(1);
        std::vector<std::string> parts;
        std::stringstream ss(rest);
        std::string seg;
        while (std::getline(ss, seg, '.')) parts.push_back(seg);
        if (parts.empty() || parts.size() > 3) return false;
        for (auto& p : parts) if (!isDigits(p)) return false;
        int major = std::stoi(parts[0]);
        int minor = parts.size() >= 2 ? std::stoi(parts[1]) : 0;
        int patch = parts.size() == 3 ? std::stoi(parts[2]) : 0;
        Version v{major, minor, patch, ""};
        out.min = v; out.minInclusive = true;
        Version hi;
        if (v.major > 0) hi = Version{v.major + 1, 0, 0, ""};
        else if (v.minor > 0) hi = Version{0, v.minor + 1, 0, ""};
        else hi = Version{0, 0, v.patch + 1, ""};
        out.max = hi; out.maxInclusive = false;
        return true;
    }
    if (op0 == '~') {
        std::string rest = text.substr(1);
        // يسمح بـ ~1  و ~1.2  و ~1.2.3
        std::vector<std::string> parts;
        std::stringstream ss(rest);
        std::string seg;
        while (std::getline(ss, seg, '.')) parts.push_back(seg);
        if (parts.empty() || parts.size() > 3) return false;
        for (auto& p : parts) if (!isDigits(p)) return false;
        int major = std::stoi(parts[0]);
        int minor = parts.size() >= 2 ? std::stoi(parts[1]) : 0;
        int patch = parts.size() == 3 ? std::stoi(parts[2]) : 0;
        Version lo{major, minor, patch, ""};
        Version hi = (parts.size() == 1) ? Version{major + 1, 0, 0, ""} : Version{major, minor + 1, 0, ""};
        out.min = lo; out.minInclusive = true;
        out.max = hi; out.maxInclusive = false;
        return true;
    }
    if (text.rfind(">=", 0) == 0) {
        Version v;
        if (!tryParseVersion(trimStr(text.substr(2)), v)) return false;
        out.min = v; out.minInclusive = true;
        return true;
    }
    if (text.rfind("<=", 0) == 0) {
        Version v;
        if (!tryParseVersion(trimStr(text.substr(2)), v)) return false;
        out.max = v; out.maxInclusive = true;
        return true;
    }
    if (text[0] == '>') {
        Version v;
        if (!tryParseVersion(trimStr(text.substr(1)), v)) return false;
        out.min = v; out.minInclusive = false;
        return true;
    }
    if (text[0] == '<') {
        Version v;
        if (!tryParseVersion(trimStr(text.substr(1)), v)) return false;
        out.max = v; out.maxInclusive = false;
        return true;
    }

    // إصدار محدد بدقة: يُطابق فقط نفس الإصدار (مساوٍ لِـ "=1.2.3" ضمنياً).
    Version v;
    if (!tryParseVersion(text, v)) return false;
    out.min = v; out.minInclusive = true;
    out.max = v; out.maxInclusive = true;
    return true;
}

VersionConstraint parseConstraint(const std::string& text) {
    VersionConstraint c;
    if (!tryParseConstraint(text, c)) {
        throw std::invalid_argument("invalid version constraint: \"" + text + "\"");
    }
    return c;
}

std::optional<Version> selectBestVersion(const VersionConstraint& c, const std::vector<Version>& available) {
    std::optional<Version> best;
    for (const auto& v : available) {
        if (!c.matches(v)) continue;
        if (!best || best->compare(v) < 0) best = v;
    }
    return best;
}

} // namespace rinpm
