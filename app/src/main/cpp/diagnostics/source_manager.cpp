#include "source_manager.h"

namespace rin::diag {

std::vector<std::pair<size_t, size_t>> SourceManager::splitLines(const std::string& text) {
    std::vector<std::pair<size_t, size_t>> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            size_t len = i - start;
            // يدعم CRLF: يستبعد \r الزائد من نهاية السطر
            if (len > 0 && text[start + len - 1] == '\r') len--;
            lines.emplace_back(start, len);
            start = i + 1;
        }
    }
    // آخر سطر بلا \n في نهاية الملف
    if (start <= text.size()) {
        size_t len = text.size() - start;
        if (len > 0 && text[start + len - 1] == '\r') len--;
        lines.emplace_back(start, len);
    }
    return lines;
}

void SourceManager::addFile(const std::string& filename, const std::string& source) {
    FileEntry entry;
    entry.source = source;
    entry.lineOffsets = splitLines(source);
    files[filename] = std::move(entry);
}

bool SourceManager::hasFile(const std::string& filename) const {
    return files.find(filename) != files.end();
}

std::string SourceManager::getLine(const std::string& filename, int lineNumber) const {
    auto it = files.find(filename);
    if (it == files.end()) return "";
    if (lineNumber < 1 || static_cast<size_t>(lineNumber) > it->second.lineOffsets.size()) return "";
    auto [start, len] = it->second.lineOffsets[lineNumber - 1];
    return it->second.source.substr(start, len);
}

int SourceManager::lineCount(const std::string& filename) const {
    auto it = files.find(filename);
    if (it == files.end()) return 0;
    return static_cast<int>(it->second.lineOffsets.size());
}

SourceManager& globalSourceManager() {
    static SourceManager instance;
    return instance;
}

} // namespace rin::diag
