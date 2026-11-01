#include "clc_rin_opt.h"
#include "clc_io.h"
#include <algorithm>
#include <unordered_map>
#include <map>
#include <cctype>
#include <cstring>

namespace clc {

bool hasRinExtension(const std::string& path) {
    static const char* exts[] = {".rin", ".og.rin", ".txt", ".md", ".json", ".rincfg", ".cfg", ".gitignore"};
    for (auto e : exts) {
        size_t elen = std::strlen(e);
        if (path.size() >= elen && path.compare(path.size()-elen, elen, e) == 0) return true;
    }
    return false;
}

bool looksLikeTextOrRin(const std::string& path, const std::vector<uint8_t>& content) {
    if (hasRinExtension(path)) return true;
    if (content.empty()) return true;
    // فحص سريع: لا NUL bytes، وأغلب البايتات إما ASCII مطبوع/مسافة أو جزء من UTF-8
    // متعدد البايت (>=0x80). عيّنة أول 4KB كافية لملفات كبيرة (سرعة).
    size_t n = std::min<size_t>(content.size(), 4096);
    size_t suspicious = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = content[i];
        if (c == 0x00) return false; // NUL => ثنائي شبه مؤكد
        if (c < 0x09 || (c > 0x0D && c < 0x20)) suspicious++; // محارف تحكم غير معتادة
    }
    return suspicious < (n / 20 + 1); // تسامح بسيط
}

namespace {
inline bool isWordChar(uint8_t c) {
    return std::isalnum(c) || c == '_' || c == '.';
}
}

void accumulateTokenFrequency(const std::vector<uint8_t>& content, std::map<std::string,uint64_t>& freq) {
    const auto& d = content;
    size_t i = 0, n = d.size();
    while (i < n) {
        if (!isWordChar(d[i])) { ++i; continue; }
        size_t start = i;
        while (i < n && isWordChar(d[i])) ++i;
        size_t len = i - start;
        if (len >= RIN_OPT_MIN_TOKEN_LEN && len < 64) {
            std::string tok(reinterpret_cast<const char*>(d.data()+start), len);
            freq[tok]++;
        }
    }
}

std::vector<std::string> finalizeDictionary(const std::map<std::string,uint64_t>& freq) {
    std::vector<std::pair<std::string,uint64_t>> candidates;
    for (auto& kv : freq) {
        if (kv.second >= RIN_OPT_MIN_OCCURRENCES) {
            // التوفير التقريبي = occurrences * (len - 2 بايت للمرجع) — تجاهل ما لا يفيد.
            int64_t savings = int64_t(kv.second) * (int64_t(kv.first.size()) - 2);
            if (savings > 0) candidates.push_back(kv);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b){
        int64_t sa = int64_t(a.second) * (int64_t(a.first.size()) - 2);
        int64_t sb = int64_t(b.second) * (int64_t(b.first.size()) - 2);
        if (sa != sb) return sa > sb;
        return a.first < b.first; // ترتيب حتمي عند تعادل التوفير (نتائج قابلة لإعادة الإنتاج)
    });
    std::vector<std::string> dict;
    for (auto& kv : candidates) {
        if (dict.size() >= RIN_OPT_MAX_DICT_SIZE) break;
        dict.push_back(kv.first);
    }
    return dict;
}

std::vector<uint8_t> encodeWithDictionary(const std::vector<uint8_t>& data, const std::vector<std::string>& dict) {
    if (dict.empty()) {
        // لا يزال يجب تهريب 0x01 الحرفي حتى لو لا يوجد قاموس، كي يبقى decode متسقاً
        // مع نفس الدالة العكسية دائماً (سلوك واحد بلا حالات خاصة).
    }
    // فهرس بحسب أول حرف: لكل حرف بداية، قائمة (طول تنازلي) من (token, id) — longest match أولاً.
    std::unordered_map<uint8_t, std::vector<std::pair<std::string,uint8_t>>> byFirst;
    for (size_t i = 0; i < dict.size(); ++i) {
        byFirst[uint8_t(dict[i][0])].push_back({dict[i], uint8_t(i)});
    }
    for (auto& kv : byFirst) {
        std::sort(kv.second.begin(), kv.second.end(), [](auto&a, auto&b){ return a.first.size() > b.first.size(); });
    }

    std::vector<uint8_t> out;
    out.reserve(data.size());
    size_t i = 0, n = data.size();
    while (i < n) {
        uint8_t c = data[i];
        if (c == RIN_OPT_ESCAPE) {
            out.push_back(RIN_OPT_ESCAPE);
            out.push_back(RIN_OPT_LITERAL_ESCAPE_MARKER);
            ++i;
            continue;
        }
        auto it = byFirst.find(c);
        bool matched = false;
        if (it != byFirst.end()) {
            for (auto& tk : it->second) {
                size_t len = tk.first.size();
                if (i + len <= n && std::memcmp(data.data()+i, tk.first.data(), len) == 0) {
                    out.push_back(RIN_OPT_ESCAPE);
                    out.push_back(tk.second);
                    i += len;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) { out.push_back(c); ++i; }
    }
    return out;
}

std::vector<uint8_t> decodeWithDictionary(const std::vector<uint8_t>& data, const std::vector<std::string>& dict) {
    std::vector<uint8_t> out;
    out.reserve(data.size());
    size_t i = 0, n = data.size();
    while (i < n) {
        uint8_t c = data[i];
        if (c == RIN_OPT_ESCAPE) {
            if (i + 1 >= n) throw ClcFormatError("truncated dictionary escape sequence (corrupted block)");
            uint8_t marker = data[i+1];
            if (marker == RIN_OPT_LITERAL_ESCAPE_MARKER) {
                out.push_back(RIN_OPT_ESCAPE);
            } else {
                if (marker >= dict.size()) throw ClcFormatError("dictionary id out of range (corrupted block)");
                const std::string& tok = dict[marker];
                out.insert(out.end(), tok.begin(), tok.end());
            }
            i += 2;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

} // namespace clc
