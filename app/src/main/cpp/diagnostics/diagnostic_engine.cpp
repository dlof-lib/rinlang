#include "diagnostic_engine.h"
#include <vector>
#include <algorithm>

namespace rin::diag {

int levenshteinDistance(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    if (n == 0) return static_cast<int>(m);
    if (m == 0) return static_cast<int>(n);

    // صفّان فقط بدل مصفوفة كاملة (O(min(n,m)) ذاكرة)
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);

    for (size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({
                prev[j] + 1,       // حذف
                cur[j - 1] + 1,    // إدراج
                prev[j - 1] + cost // استبدال
            });
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

std::vector<std::string> nearestMatches(const std::string& target,
                                         const std::vector<std::string>& candidates,
                                         int maxDistance,
                                         size_t maxResults) {
    std::vector<std::pair<int, std::string>> scored;
    scored.reserve(candidates.size());
    for (const auto& cand : candidates) {
        if (cand == target) continue;
        int d = levenshteinDistance(target, cand);
        // نسمح بمسافة أكبر قليلاً للأسماء الطويلة حتى لا نخسر اقتراحات معقولة
        int allowed = std::max(maxDistance, static_cast<int>(target.size()) / 3);
        if (d <= allowed) scored.emplace_back(d, cand);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) {
        if (x.first != y.first) return x.first < y.first;
        return x.second < y.second; // ترتيب أبجدي عند تعادل المسافة (نتيجة حتمية)
    });
    std::vector<std::string> out;
    for (size_t i = 0; i < scored.size() && i < maxResults; ++i) out.push_back(scored[i].second);
    return out;
}

std::string bestMatch(const std::string& target,
                       const std::vector<std::string>& candidates,
                       int maxDistance) {
    auto matches = nearestMatches(target, candidates, maxDistance, 1);
    return matches.empty() ? std::string() : matches.front();
}

} // namespace rin::diag
