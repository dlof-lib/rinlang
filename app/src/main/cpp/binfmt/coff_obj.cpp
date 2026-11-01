#include "coff_obj.h"
#include <stdexcept>
#include <cstdint>

namespace binfmt::coff {

std::string buildObject(const std::string& symbolName, int32_t returnValue) {
    // mov eax, returnValue ; ret — نفس جسم الدالة المُصدَّرة المستخدَم في ELF .so وMach-O dylib
    ByteBuf code;
    code.u8(0xB8); code.u32le(uint32_t(returnValue));
    code.u8(0xC3);
    const uint32_t codeSize = uint32_t(code.pos());

    const uint32_t coffHdrSize = 20;
    const uint32_t sectHdrSize = 40;
    const uint32_t codeOff = coffHdrSize + sectHdrSize;
    const uint32_t symtabOff = codeOff + codeSize;
    const uint32_t symtabSize = 18; // رمز واحد فقط
    const uint32_t strtabOff = symtabOff + symtabSize;

    // جدول السلاسل: حجم إجمالي (4 بايت، يشمل هذه الأربعة بايتات نفسها) ثم الاسم منتهياً بصفر.
    // (اسم الرمز يُشار إليه دوماً عبر جدول السلاسل هنا للتبسيط، حتى لو كان قصيراً بما يكفي
    // ليُخزَّن مباشرة — كلا الأسلوبين صالح رسمياً في صيغة COFF).
    std::string strtab;
    { ByteBuf t; t.u32le(uint32_t(4 + symbolName.size() + 1)); strtab = t.toString(); }
    strtab += symbolName; strtab.push_back('\0');

    ByteBuf b;
    // ---- IMAGE_FILE_HEADER (بلا أي غلاف MZ/PE — ملف .obj خام) ----
    b.u16le(0x8664);        // Machine: IMAGE_FILE_MACHINE_AMD64
    b.u16le(1);              // NumberOfSections
    b.u32le(0);               // TimeDateStamp
    b.u32le(symtabOff);       // PointerToSymbolTable
    b.u32le(1);               // NumberOfSymbols
    b.u16le(0);               // SizeOfOptionalHeader (لا يوجد في ملف كائن)
    b.u16le(0);               // Characteristics

    // ---- IMAGE_SECTION_HEADER: .text ----
    b.fixedStr(".text", 8, '\0');
    b.u32le(0);               // PhysicalAddress/VirtualSize (غير مستخدَم في ملف كائن)
    b.u32le(0);               // VirtualAddress
    b.u32le(codeSize);        // SizeOfRawData
    b.u32le(codeOff);         // PointerToRawData
    b.u32le(0); b.u32le(0);   // PointerToRelocations/Linenumbers (لا علاقات — الدالة مكتفية ذاتياً)
    b.u16le(0); b.u16le(0);   // NumberOfRelocations/Linenumbers
    b.u32le(0x60000020u);     // IMAGE_SCN_CNT_CODE | MEM_EXECUTE | MEM_READ

    b.bytes(code.data);

    // ---- جدول الرموز الكلاسيكي: رمز عام خارجي واحد (C_EXT) معرَّف داخل القسم رقم 1 ----
    b.u32le(0);                // Name (أول 4 بايت=0) => الاسم يُقرأ من جدول السلاسل، ليس مضمَّناً مباشرة
    b.u32le(4);                // إزاحة الاسم داخل جدول السلاسل (بعد حقل الحجم الأولي 4 بايت)
    b.u32le(0);                // Value: إزاحة الرمز داخل .text (صفر: أول شيء فيه)
    b.u16le(1);                // SectionNumber = 1 (.text)
    b.u16le(0x20);             // Type: DT_FUNCTION
    b.u8(2);                   // StorageClass = C_EXT (خارجي/عام)
    b.u8(0);                   // NumberOfAuxSymbols

    if (b.pos() != strtabOff) throw std::runtime_error("COFF obj: internal symtab-size mismatch");
    b.str(strtab);

    return b.toString();
}

} // namespace binfmt::coff
