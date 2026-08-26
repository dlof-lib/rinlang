#pragma once
// Rin Diagnostics - source_location.h
//
// موقع دقيق داخل ملف مصدر Rin: سطر/عمود البداية والنهاية، بالإضافة إلى
// إزاحتين (offsets) اختياريتين ضمن نص الملف الكامل. يُستخدم من قِبَل كل
// Diagnostic لعرض السهم (^^^^) تحت المقطع الصحيح من السطر بالضبط، بدل
// معرفة رقم السطر فقط.
//
// ملاحظة: الأعمدة 1-indexed (أول حرف بالسطر = عمود 1) بما يطابق تعارف
// المحررات وLSP (بعد تحويل بسيط) ورسائل compilers مثل rustc/clang.
#include <string>
#include <algorithm>

namespace rin::diag {

struct SourceLocation {
    std::string file;      // اسم/مسار الملف كما مُرِّر إلى Lexer (أو "<stdin>"/"<inline>")
    int startLine = 0;     // 1-indexed
    int startCol = 0;      // 1-indexed
    int endLine = 0;       // 1-indexed (عادة == startLine لمعظم الرموز البسيطة)
    int endCol = 0;        // 1-indexed، حصري النهاية (أي يشير إلى ما بعد آخر حرف)

    SourceLocation() = default;

    SourceLocation(std::string f, int sLine, int sCol, int eLine, int eCol)
        : file(std::move(f)), startLine(sLine), startCol(sCol), endLine(eLine), endCol(eCol) {}

    // موقع نقطة واحدة (بدون مدى)؛ يُستخدم عندما لا يُعرف طول الرمز المخالف
    static SourceLocation point(std::string f, int line, int col) {
        return SourceLocation(std::move(f), line, col, line, col + 1);
    }

    bool isValid() const { return startLine > 0 && startCol > 0; }

    // عدد الأحرف التي يغطيها المدى على نفس السطر (لرسم ^^^^ بالطول الصحيح).
    // لا يقل عن 1 حتى لو كانت البيانات ناقصة.
    int caretWidth() const {
        if (endLine != startLine) return 1; // مدى متعدد الأسطر: نكتفي بسهم عند نقطة البداية
        return std::max(1, endCol - startCol);
    }
};

} // namespace rin::diag
