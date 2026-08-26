#pragma once
// Rin Diagnostics - diagnostic_engine.h
//
// المُجمِّع المركزي: كل مرحلة (Lexer/Parser/Interpreter) تستدعي emit() بدل
// throw مباشرة عندما يكون آمناً الاستمرار بعد الخطأ (error recovery)، بحيث
// يمكن الإبلاغ عن عدة أخطاء دفعة واحدة بدل التوقف عند أول خطأ فقط.
//
// أخطاء لا يمكن معها الاستمنار بأمان (مثل فشل قراءة ملف، أو حالة parser لا
// يمكن معها إيجاد نقطة synchronization) تبقى تُرفَع كـ RinError (استثناء C++
// عادي يحمل Diagnostic كامل) — انظر rin_common.h.
#include <vector>
#include <string>
#include "diagnostic.h"

namespace rin::diag {

class DiagnosticEngine {
public:
    void emit(Diagnostic d) { entries.push_back(std::move(d)); }

    const std::vector<Diagnostic>& all() const { return entries; }
    void clear() { entries.clear(); }

    bool hasErrors() const {
        for (auto& d : entries) if (d.isError()) return true;
        return false;
    }

    int errorCount() const {
        int n = 0; for (auto& d : entries) if (d.isError()) n++; return n;
    }

    int warningCount() const {
        int n = 0; for (auto& d : entries) if (d.severity == Severity::Warning) n++; return n;
    }

    size_t size() const { return entries.size(); }

private:
    std::vector<Diagnostic> entries;
};

// ------------------------------------------------------------------------
// Suggestion engine: مسافة Levenshtein + إيجاد أقرب الأسماء المرشّحة
// (يُستخدم لـ "did you mean `x`?" في المتغيرات/الدوال/الخصائص غير المعرَّفة).
// ------------------------------------------------------------------------

// مسافة التحرير (عدد الإدراج/الحذف/الاستبدال) بين سلسلتين.
int levenshteinDistance(const std::string& a, const std::string& b);

// يعيد أفضل maxResults مرشّح من candidates بحيث تكون مسافة Levenshtein عن
// target أقل من أو تساوي maxDistance، مرتّبة من الأقرب فالأبعد. يتجاهل
// target نفسه إن ظهر ضمن candidates (لا فائدة من اقتراح الاسم بنفسه).
std::vector<std::string> nearestMatches(const std::string& target,
                                         const std::vector<std::string>& candidates,
                                         int maxDistance = 2,
                                         size_t maxResults = 3);

// اختصار شائع: أول مرشّح فقط (أو فارغ إن لم يوجد)، لبناء رسالة
// "did you mean `x`?" مباشرة.
std::string bestMatch(const std::string& target,
                       const std::vector<std::string>& candidates,
                       int maxDistance = 2);

} // namespace rin::diag
