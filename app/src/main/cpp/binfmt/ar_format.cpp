#include "ar_format.h"
#include <cstdio>
#include <cstdint>

namespace binfmt::ar {

namespace {
// حقل نصي بعرض ثابت، محاذى لليسار ومملوء بمسافات (0x20) — تماماً كما تفرضه صيغة ar.
std::string field(const std::string& s, size_t width) {
    std::string t = s.substr(0, width);
    t.resize(width, ' ');
    return t;
}
}

namespace {
void putU32BE(std::string& s, uint32_t v) {
    s.push_back(char((v >> 24) & 0xFF));
    s.push_back(char((v >> 16) & 0xFF));
    s.push_back(char((v >> 8) & 0xFF));
    s.push_back(char(v & 0xFF));
}
}

std::string build(const std::vector<Member>& members) {
    std::string out = "!<arch>\n"; // ترويسة الأرشيف العامة الإلزامية (8 بايت بالضبط)

    // ---- فهرس الأرشيف الحقيقي (armap): عضو خاص أول باسم "/" يحتاجه أي رابط (ld/lld) ليقبل
    // الأرشيف مباشرة بلا شكوى "archive has no index; run ranlib". الصيغة (GNU/System V):
    //   عدد الرموز N (uint32 كبير النهاية) ثم N إزاحة (uint32 كبيرة النهاية) لكل رمز تشير إلى
    //   بداية ترويسة العضو الذي يُعرِّف ذلك الرمز، ثم أسماء الرموز نفسها متتالية بحرف NUL فاصل.
    size_t totalSymbols = 0, totalNameBytes = 0;
    for (const auto& m : members) { totalSymbols += m.symbols.size(); for (auto& s : m.symbols) totalNameBytes += s.size() + 1; }

    std::string symdefContent;
    putU32BE(symdefContent, uint32_t(totalSymbols));
    size_t offsetsPatchStart = symdefContent.size();
    symdefContent.append(4 * totalSymbols, '\0'); // مكان محجوز للإزاحات؛ يُملأ لاحقاً بعد حساب مواضع الأعضاء
    for (const auto& m : members) for (auto& s : m.symbols) { symdefContent += s; symdefContent.push_back('\0'); }

    std::string symdefHeader = field("/", 16) + field("0", 12) + field("0", 6) + field("0", 6) +
                                field("0", 8) + field(std::to_string(symdefContent.size()), 10) + "\x60\x0A";
    out += symdefHeader;
    size_t symdefBodyStart = out.size();
    out += symdefContent;
    if (symdefContent.size() % 2 != 0) out.push_back('\n');

    // الآن نضيف الأعضاء الفعليين، ونسجّل إزاحة ترويسة كل عضو لنعيد ملء جدول الإزاحات أعلاه.
    std::vector<uint32_t> memberHeaderOffsets;
    for (const auto& m : members) {
        memberHeaderOffsets.push_back(uint32_t(out.size()));
        // اسم العضو بأسلوب GNU ar: الاسم ثم '/' كفاصل نهاية-اسم، ثم حشوة مسافات حتى 16 بايت
        // (يفترض أسماء أعضاء لا تتجاوز 15 محرفاً؛ كافٍ لأسماء ملفات كائن مثل "func.o").
        std::string nameField = field(m.name + "/", 16);
        std::string mtimeField = field("0", 12);
        std::string uidField = field("0", 6);
        std::string gidField = field("0", 6);
        std::string modeField = field("100644", 8); // rw-r--r-- عادي لملف كائن
        std::string sizeField = field(std::to_string(m.data.size()), 10);

        out += nameField + mtimeField + uidField + gidField + modeField + sizeField;
        out += "\x60\x0A"; // نهاية الترويسة الإلزامية "`\n"
        out += m.data;
        if (m.data.size() % 2 != 0) out.push_back('\n'); // حشوة محاذاة زوجية بين الأعضاء
    }

    // الآن -وقد عرفنا إزاحة ترويسة كل عضو فعلياً- نعيد كتابة جدول الإزاحات المحجوز داخل
    // محتوى عضو armap (لا حاجة لإعادة بناء أي شيء آخر لأن حجم الجدول محجوز مسبقاً بنفس القياس).
    {
        std::string patched = symdefContent;
        size_t idx = 0;
        for (size_t mi = 0; mi < members.size(); ++mi) {
            for (size_t i = 0; i < members[mi].symbols.size(); ++i) {
                size_t p = offsetsPatchStart + idx * 4;
                uint32_t off = memberHeaderOffsets[mi];
                patched[p+0] = char((off >> 24) & 0xFF);
                patched[p+1] = char((off >> 16) & 0xFF);
                patched[p+2] = char((off >> 8) & 0xFF);
                patched[p+3] = char(off & 0xFF);
                ++idx;
            }
        }
        out.replace(symdefBodyStart, patched.size(), patched);
    }
    return out;
}

} // namespace binfmt::ar
