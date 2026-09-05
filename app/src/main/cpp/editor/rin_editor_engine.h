// rin_editor_engine.h
//
// محرك محرر أكواد كامل بلغة C++ (بلا أي اعتماد على Android EditText) خاص بلغة Rin.
// يحتفظ بالنص كسطور UTF-8، ويوفر: إدراج/حذف، تراجع/إعادة (undo/redo) حقيقي مبني على
// سجلّ تعديلات دقيق (وليس نسخ نص كاملة)، مسافة بادئة تلقائية + إغلاق أقواس/علامات تنصيص
// تلقائي + إزاحة ذكية عند `}`، أوامر مستوى-السطر (تكرار/حذف/نقل/تعليق/إزاحة)، فحص توازن
// الأقواس، بحث عن كل التطابقات، وتلوين نحوي حقيقي مبني على rin::Lexer الفعلي (وليس Regex).
//
// كل الإحداثيات (line, col) هي: line = فهرس سطر صفري (0-based)، col = إزاحة بايت UTF-8
// صفرية داخل السطر (0-based) — نفس اصطلاح Token.col/endCol في rin_lexer.h (بعد طرح 1).
#pragma once

#include <string>
#include <vector>
#include <deque>
#include <cstdint>

namespace rinedit {

enum class HighlightKind {
    Default = 0,
    Keyword = 1,
    String = 2,
    Number = 3,
    Ident = 4,
    Operator = 5,
    Bracket = 6,
    At = 7,
    Error = 8,
    Call = 9, // معرِّف متبوع مباشرة بـ '(' — نمط استدعاء دالة (لتلوين مميّز كما في المحررات الاحترافية)
    // = 11 عمداً (تخطّي 10 المحجوزة لـ Comment في HighlightKind.kt؛ الماسح الحقيقي لا يُصدر
    // رموز تعليقات أصلاً لأنها تُستبعَد أثناء المسح). القيمة يجب أن تطابق HighlightKind.TYPE في
    // Kotlin حرفياً لأن nativeGetHighlightSpansFlat يمرّر ترتيب enum الخام كعدد صحيح مباشرة.
    Type = 11, // كلمات لغة الحاويات/الأنواع (container/table/Section/Group/link/save/file/...)
};

struct HighlightSpan {
    int line;
    int startCol;
    int endCol;
    HighlightKind kind;
};

struct Position {
    int line = 0;
    int col = 0;
};

struct Match {
    int line;
    int startCol;
    int endCol;
};

// سجلّ تعديل واحد قابل للتراجع/الإعادة. النطاق [startLine,startCol) هو النقطة المشتركة؛
// (origEndLine,origEndCol) هو نهاية النص *قبل* التعديل (حيث كان removedText)،
// (newEndLine,newEndCol) هو نهاية النص *بعد* التعديل (حيث أصبح insertedText).
struct EditRecord {
    int startLine = 0, startCol = 0;
    int origEndLine = 0, origEndCol = 0;
    int newEndLine = 0, newEndCol = 0;
    std::string removedText;
    std::string insertedText;
    Position cursorBefore;
    Position cursorAfter;
    int64_t timestampMs = 0;
    bool coalescable = false; // true لكتابة حرف واحد/حذف حرف واحد فقط (تُجمَّع مع سابقتها)
};

class EditorEngine {
public:
    EditorEngine();

    // --- نص كامل ---
    void setText(const std::string& text);
    std::string getText() const;
    int lineCount() const { return static_cast<int>(lines_.size()); }
    std::string getLine(int line) const;

    // --- مؤشر/تحديد ---
    Position getCursor() const { return cursor_; }
    bool hasSelection() const { return hasSel_; }
    // يُرجع دومًا مُطبَّعة (start <= end)
    void getSelection(int* sl, int* sc, int* el, int* ec) const;
    void setCursor(int line, int col, bool extendSelection);
    void setSelection(int aLine, int aCol, int bLine, int bCol);
    void collapseSelectionToCursor();

    // --- تحرير أساسي ---
    // يُدرج نصًا عند المؤشر (يستبدل التحديد إن وُجد). smart=true يفعّل الإزاحة التلقائية
    // بعد Enter، وإغلاق الأقواس/التنصيص التلقائي، والتخطي فوق القوس المغلق، للحرف الواحد فقط.
    void insertText(const std::string& utf8Text, bool smart);
    // مكافئ لزر Backspace: يحذف حرفًا واحدًا (أو زوج قوس/تنصيص تلقائي بالكامل، أو التحديد إن وُجد)
    void deleteBackward();
    // مكافئ لزر Delete: يحذف حرفًا واحدًا للأمام (أو التحديد إن وُجد)
    void deleteForward();
    void deleteSelectionIfAny();
    void replaceRange(int sl, int sc, int el, int ec, const std::string& text);

    // --- تراجع/إعادة ---
    bool undo();
    bool redo();

    // --- أوامر مستوى-السطر ---
    void duplicateCurrentLine();
    void deleteCurrentLine();
    void moveLineUp();
    void moveLineDown();
    void toggleLineComment();
    void indentSelection();
    void unindentSelection();

    // --- فحص/بحث ---
    // -1 = متوازن، وإلا رقم أول سطر فيه خلل (1-based)
    int checkBracketBalance() const;
    std::vector<Match> findAll(const std::string& query, bool caseSensitive) const;
    // بداية السطر رقم oneBasedLine (مُقيَّد ضمن الحدود)
    Position lineStartPosition(int oneBasedLine) const;

    // --- تلوين نحوي حقيقي (rin::Lexer) ---
    std::vector<HighlightSpan> computeHighlights() const;

    // --- إكمال تلقائي (autocomplete) حقيقي ---
    // يُرجع حتى maxResults اقتراحًا يبدأ بـ prefix (غير حسّاس لحالة الأحرف)، مبنية على: كل
    // الكلمات المحجوزة الفعلية في لغة Rin (rin::keywordList()) + كل المعرِّفات (IDENT) الفريدة
    // الظاهرة فعلاً في المستند الحالي (متغيرات/دوال عرَّفها المستخدم) — الكلمات المحجوزة أولاً
    // ثم المعرِّفات، وكلٌّ منها مُرتَّب أبجديًا؛ لا يقترح prefix نفسه إن كان مطابقًا تمامًا لوحده.
    std::vector<std::string> collectSuggestions(const std::string& prefix, int maxResults) const;

private:
    std::vector<std::string> lines_;
    Position cursor_;
    Position selAnchor_;
    bool hasSel_ = false;

    std::deque<EditRecord> undoStack_;
    std::deque<EditRecord> redoStack_;
    static constexpr size_t kMaxHistory = 500;
    static constexpr int64_t kCoalesceWindowMs = 800;

    int clampLine(int line) const;
    int clampCol(int line, int col) const;
    void clampCursor();
    std::string textOfRange(int sl, int sc, int el, int ec) const;
    // ينفّذ الاستبدال الخام فعليًا على lines_، ويكتب نهاية النص الجديد في outEnd إن لم تكن null.
    void applyRawReplace(int sl, int sc, int el, int ec, const std::string& text, Position* outEnd);
    void pushUndo(EditRecord rec);
    void applyReplaceTracked(int sl, int sc, int el, int ec, const std::string& text, bool coalescable);

    void applySmartDedentForClosingBrace();
    static std::string leadingWhitespace(const std::string& s);
    static std::string rtrim(const std::string& s);
};

} // namespace rinedit
