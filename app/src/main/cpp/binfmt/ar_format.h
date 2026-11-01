// ar_format.h — كاتب أرشيف ar حقيقي (System V/GNU ar format)، هو نفسه الصالح لصيغتي:
//   - .a  على لينكس/ماك (يحوي أعضاء ELF/Mach-O قابلة لإعادة الربط .o)
//   - .lib الساكنة على ويندوز (Microsoft lib.exe يستخدم نفس صيغة ar حرفياً للأعضاء COFF .obj)
// مرجع الصيغة: "!<arch>\n" ثم لكل عضو ترويسة ثابتة 60 بايت بتنسيق نصوص عشرية/أوكتال، تليها
// بايتات العضو الخام، مع حشوة بايت واحد '\n' إن كان طول العضو فردياً.
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace binfmt::ar {

struct Member {
    std::string name;                 // بلا مسار، كما سيظهر في فهرس الأرشيف (مثال: "func.o")
    std::string data;                 // محتوى العضو الخام (ملف .o أو .obj كاملاً)
    std::vector<std::string> symbols; // الرموز العامة (GLOBAL) التي يُصدِّرها هذا العضو —
                                       // تُستخدَم لبناء فهرس الأرشيف الحقيقي (armap/"/" الخاص)
                                       // الذي يحتاجه أي رابط (ld) ليقبل الأرشيف دون "no index".
};

// يبني أرشيف ar حقيقي يحوي كل الأعضاء المُعطاة بالترتيب.
std::string build(const std::vector<Member>& members);

} // namespace binfmt::ar
