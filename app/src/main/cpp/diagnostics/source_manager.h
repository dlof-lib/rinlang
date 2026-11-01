#pragma once
// Rin Diagnostics - source_manager.h
//
// يحتفظ بنص كل ملف مصدر (أو مقتطف) سُجِّل أثناء التصريف/التشغيل، بحيث يستطيع
// diagnostic_renderer عرض سطر الكود الفعلي وسهم (^^^^) تحته دون أن يحمل كل
// Diagnostic نسخة من الملف بأكمله.
#include <string>
#include <unordered_map>
#include <vector>

namespace rin::diag {

class SourceManager {
public:
    // يسجّل/يستبدل محتوى ملف باسم معيّن. يُستدعى مرة واحدة عادة عند بدء lexing.
    void addFile(const std::string& filename, const std::string& source);

    // هل الملف مسجَّل؟
    bool hasFile(const std::string& filename) const;

    // نص سطر واحد (1-indexed) من الملف؛ يعيد سلسلة فارغة إن كان الملف/السطر غير موجودين.
    std::string getLine(const std::string& filename, int lineNumber) const;

    // عدد أسطر الملف الكلي (لأغراض التحقق من صحة موقع الخطأ فقط).
    int lineCount(const std::string& filename) const;

private:
    struct FileEntry {
        std::string source;
        std::vector<std::pair<size_t, size_t>> lineOffsets; // (start, length) لكل سطر بدون \n
    };

    std::unordered_map<std::string, FileEntry> files;

    static std::vector<std::pair<size_t, size_t>> splitLines(const std::string& text);
};

// نسخة عامة واحدة يشترك فيها Lexer/Parser/Interpreter ضمن نفس عملية التشغيل.
// كل مكوّن يسجّل الملف الذي يعالجه عبر globalSourceManager().addFile(...)
// قبل إصدار أي Diagnostic يشير إليه.
SourceManager& globalSourceManager();

} // namespace rin::diag
