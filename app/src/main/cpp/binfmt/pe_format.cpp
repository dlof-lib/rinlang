#include "pe_format.h"
#include <stdexcept>

namespace binfmt::pe {

namespace {

constexpr uint32_t SECTION_ALIGN = 0x1000;
constexpr uint32_t FILE_ALIGN = 0x200;

uint32_t alignUp(uint32_t v, uint32_t a) { uint32_t r = v % a; return r ? v + (a - r) : v; }

void writeDosHeader(ByteBuf& b, uint32_t peHeaderOffset) {
    b.u8('M'); b.u8('Z');
    b.zeros(58);           // بقية حقول IMAGE_DOS_HEADER (لا يهتم بها محمّل PE إطلاقاً)
    b.u32le(peHeaderOffset); // e_lfanew عند الإزاحة 0x3C بالضبط (64 بايت إجمالي حتى هنا)
}

void writeCoffHeader(ByteBuf& b, uint16_t numberOfSections, uint16_t sizeOfOptionalHeader, uint16_t characteristics) {
    b.str("PE"); b.u8(0); b.u8(0);
    b.u16le(0x8664);           // IMAGE_FILE_MACHINE_AMD64
    b.u16le(numberOfSections);
    b.u32le(0);                 // TimeDateStamp
    b.u32le(0);                 // PointerToSymbolTable
    b.u32le(0);                 // NumberOfSymbols
    b.u16le(sizeOfOptionalHeader);
    b.u16le(characteristics);
}

// يكتب IMAGE_OPTIONAL_HEADER64 كاملاً (112 بايت ثابتة + 16 مدخل دليل بيانات * 8 بايت = 240 بايت)
void writeOptionalHeader64(ByteBuf& b, uint32_t entryRva, uint32_t baseOfCodeRva, uint64_t imageBase,
                            uint32_t sizeOfCode, uint32_t sizeOfImage, uint32_t sizeOfHeaders,
                            uint16_t subsystem, uint16_t dllCharacteristics,
                            uint32_t importDirRva, uint32_t importDirSize,
                            uint32_t exportDirRva, uint32_t exportDirSize) {
    b.u16le(0x20B);            // Magic: PE32+
    b.u8(14); b.u8(0);          // Linker version (قيمة تعريفية فقط)
    b.u32le(sizeOfCode);
    b.u32le(0);                 // SizeOfInitializedData
    b.u32le(0);                 // SizeOfUninitializedData
    b.u32le(entryRva);          // AddressOfEntryPoint (0 يعني: لا نقطة دخول — صالح لِـ DLL)
    b.u32le(baseOfCodeRva);
    b.u64le(imageBase);
    b.u32le(SECTION_ALIGN);
    b.u32le(FILE_ALIGN);
    b.u16le(6); b.u16le(0);     // OS version
    b.u16le(0); b.u16le(0);     // Image version
    b.u16le(6); b.u16le(0);     // Subsystem version
    b.u32le(0);                 // Win32VersionValue
    b.u32le(sizeOfImage);
    b.u32le(sizeOfHeaders);
    b.u32le(0);                 // CheckSum (0 مقبول؛ لا يتحقق منه المحمّل لملفات عادية)
    b.u16le(subsystem);
    b.u16le(dllCharacteristics);
    b.u64le(0x100000);          // SizeOfStackReserve
    b.u64le(0x1000);            // SizeOfStackCommit
    b.u64le(0x100000);          // SizeOfHeapReserve
    b.u64le(0x1000);            // SizeOfHeapCommit
    b.u32le(0);                 // LoaderFlags
    b.u32le(16);                // NumberOfRvaAndSizes
    // دليل البيانات الـ16 (Export, Import, Resource, Exception, Security, BaseReloc, Debug,
    // Architecture, GlobalPtr, TLS, LoadConfig, BoundImport, IAT, DelayImport, CLR, Reserved)
    for (int i = 0; i < 16; ++i) {
        uint32_t rva = 0, size = 0;
        if (i == 0) { rva = exportDirRva; size = exportDirSize; }
        if (i == 1) { rva = importDirRva; size = importDirSize; }
        b.u32le(rva); b.u32le(size);
    }
}

void writeSectionHeader(ByteBuf& b, const std::string& name, uint32_t virtualSize, uint32_t rva,
                         uint32_t sizeOfRawData, uint32_t pointerToRawData, uint32_t characteristics) {
    b.fixedStr(name, 8, '\0');
    b.u32le(virtualSize);
    b.u32le(rva);
    b.u32le(sizeOfRawData);
    b.u32le(pointerToRawData);
    b.u32le(0); b.u32le(0);   // PointerToRelocations/Linenumbers
    b.u16le(0); b.u16le(0);   // NumberOf Relocations/Linenumbers
    b.u32le(characteristics);
}

constexpr uint16_t NUM_SECTIONS = 1;
constexpr uint32_t OPT_HDR_SIZE = 240;
constexpr uint32_t HEADERS_RAW_SIZE_UNALIGNED = 64 /*DOS*/ + 4 /*PE sig*/ + 20 /*COFF*/ + OPT_HDR_SIZE + 40 * NUM_SECTIONS;

} // namespace

std::string buildExecutable(uint32_t exitCode) {
    const uint64_t imageBase = 0x140000000ull;
    const uint32_t sectionRva = SECTION_ALIGN; // أول قسم يبدأ عند RVA=0x1000 بالتقليد المتبع
    const uint32_t headersFileSize = alignUp(HEADERS_RAW_SIZE_UNALIGNED, FILE_ALIGN);

    // ---- تخطيط محتوى قسم .text الوحيد (كود + جدول استيراد كامل: Directory/ILT/IAT/Hint-Name/DLLName) ----
    const uint32_t codeOff = 0;
    const uint32_t codeSize = 19; // and rsp,-16(4) + sub rsp,0x20(4) + mov ecx,imm32(5) + call [rip+disp32](6)

    uint32_t off = codeOff + codeSize;
    off = alignUp(off, 4);
    const uint32_t importDirOff = off; const uint32_t importDirSize = 40; // وصفان (Descriptor حقيقي + منتهي بصفر)
    off = importDirOff + importDirSize;
    const uint32_t iltOff = off; const uint32_t iltSize = 16; // مدخل واحد + منتهي بصفر (8 بايت لكل مدخل، PE32+)
    off = iltOff + iltSize;
    const uint32_t iatOff = off; const uint32_t iatSize = 16;
    off = iatOff + iatSize;
    const uint32_t hintNameOff = off; // Hint(2) + "ExitProcess\0"(12) = 14 بايت (زوجي أصلاً)
    const std::string funcName = "ExitProcess";
    const uint32_t hintNameSize = uint32_t(2 + funcName.size() + 1);
    off = hintNameOff + hintNameSize;
    const uint32_t dllNameOff = off; const std::string dllName = "KERNEL32.dll";
    const uint32_t dllNameSize = uint32_t(dllName.size() + 1);
    off = dllNameOff + dllNameSize;
    const uint32_t sectionContentLen = off;

    const uint32_t codeRva = sectionRva + codeOff;
    const uint32_t importDirRva = sectionRva + importDirOff;
    const uint32_t iltRva = sectionRva + iltOff;
    const uint32_t iatRva = sectionRva + iatOff;
    const uint32_t hintNameRva = sectionRva + hintNameOff;
    const uint32_t dllNameRva = sectionRva + dllNameOff;

    // ---- توليد كود x86_64 حقيقي: ExitProcess(exitCode) عبر جدول الاستيراد أعلاه ----
    ByteBuf code;
    code.u8(0x48); code.u8(0x83); code.u8(0xE4); code.u8(0xF0);       // and rsp, -16 (محاذاة إلزامية قبل CALL وفق ABI)
    code.u8(0x48); code.u8(0x83); code.u8(0xEC); code.u8(0x20);       // sub rsp, 0x20 (Shadow Space الإلزامية x64)
    code.u8(0xB9); code.u32le(exitCode);                               // mov ecx, exitCode (أول وسيط x64: RCX)
    const uint32_t callInstrEndOffsetInCode = 13 + 6;                  // موضع نهاية FF 15 disp32 داخل الكود
    const uint32_t ripAfterCall = codeRva + callInstrEndOffsetInCode;
    const int32_t disp32 = int32_t(iatRva - ripAfterCall);
    code.u8(0xFF); code.u8(0x15); code.i32le(disp32);                  // call qword ptr [rip+disp32] -> يستدعي ExitProcess فعلياً عبر IAT
    if (code.pos() != codeSize) throw std::runtime_error("PE exe: internal code-size mismatch");

    ByteBuf section;
    section.bytes(code.data);
    section.zeros(importDirOff - section.pos());
    // IMAGE_IMPORT_DESCRIPTOR حقيقي لِـ KERNEL32.dll
    section.u32le(iltRva); section.u32le(0); section.u32le(0); section.u32le(dllNameRva); section.u32le(iatRva);
    section.zeros(20); // وصف منتهي بصفر (إلزامي: يُعلِم المحمّل بنهاية قائمة المكتبات المستوردة)
    section.zeros(iltOff - section.pos());
    section.u64le(uint64_t(hintNameRva)); // ILT[0]: استيراد بالاسم (البت العلوي=0) يشير لـ Hint/Name
    section.u64le(0);                     // ILT[1]: منتهي بصفر
    section.zeros(iatOff - section.pos());
    section.u64le(uint64_t(hintNameRva)); // IAT[0]: نفس قيمة ILT قبل الربط — المحمّل يستبدلها بعنوان ExitProcess الحقيقي عند التحميل
    section.u64le(0);                     // IAT[1]: منتهي بصفر
    section.zeros(hintNameOff - section.pos());
    section.u16le(0);                     // Hint (لا يهم، 0 صالح)
    section.str(funcName, true);
    section.zeros(dllNameOff - section.pos());
    section.str(dllName, true);
    if (section.pos() != sectionContentLen) throw std::runtime_error("PE exe: internal section-size mismatch");

    const uint32_t sectionRawSize = alignUp(sectionContentLen, FILE_ALIGN);
    const uint32_t sizeOfImage = alignUp(sectionRva + alignUp(sectionContentLen, SECTION_ALIGN), SECTION_ALIGN);

    ByteBuf b;
    writeDosHeader(b, 64);
    writeCoffHeader(b, NUM_SECTIONS, uint16_t(OPT_HDR_SIZE),
                     0x0002 /*EXECUTABLE_IMAGE*/ | 0x0020 /*LARGE_ADDRESS_AWARE*/ | 0x0100 /*32BIT_MACHINE — يُتجاهَل لِـ x64 لكن شائع وجوده*/);
    writeOptionalHeader64(b, codeRva, codeRva, imageBase, sectionRawSize, sizeOfImage, headersFileSize,
                           3 /*IMAGE_SUBSYSTEM_WINDOWS_CUI*/, 0,
                           importDirRva, importDirSize, 0, 0);
    // خاصية القسم: كود + قابل للتنفيذ والقراءة والكتابة (مدموج فيه جدول الاستيراد/IAT القابل للكتابة وقت الربط)
    writeSectionHeader(b, ".text", sectionContentLen, sectionRva, sectionRawSize, headersFileSize,
                        0x60000020u | 0x80000000u);
    b.zeros(headersFileSize - b.pos());
    b.bytes(section.data);
    b.zeros(headersFileSize + sectionRawSize - b.pos());

    return b.toString();
}

std::string buildDll(const std::string& exportName, int32_t returnValue, const std::string& dllFileName) {
    const uint64_t imageBase = 0x180000000ull;
    const uint32_t sectionRva = SECTION_ALIGN;
    const uint32_t headersFileSize = alignUp(HEADERS_RAW_SIZE_UNALIGNED, FILE_ALIGN);

    // ---- كود الدالة المُصدَّرة: mov eax, returnValue ; ret (تماماً كما في مولّد ELF .so) ----
    const uint32_t codeOff = 0, codeSize = 6;

    uint32_t off = alignUp(codeOff + codeSize, 4);
    const uint32_t funcArrayOff = off; const uint32_t funcArraySize = 4;      // AddressOfFunctions[1]
    off = funcArrayOff + funcArraySize;
    const uint32_t nameArrayOff = off; const uint32_t nameArraySize = 4;      // AddressOfNames[1]
    off = nameArrayOff + nameArraySize;
    const uint32_t ordArrayOff = off; const uint32_t ordArraySize = 2;        // AddressOfNameOrdinals[1]
    off = ordArrayOff + ordArraySize;
    off = alignUp(off, 4);
    const uint32_t exportDirOff = off; const uint32_t exportDirSize = 40;     // IMAGE_EXPORT_DIRECTORY
    off = exportDirOff + exportDirSize;
    const uint32_t nameStrOff = off; const uint32_t nameStrSize = uint32_t(exportName.size() + 1);
    off = nameStrOff + nameStrSize;
    const uint32_t dllNameOff = off; const uint32_t dllNameSize = uint32_t(dllFileName.size() + 1);
    off = dllNameOff + dllNameSize;
    const uint32_t sectionContentLen = off;

    const uint32_t codeRva = sectionRva + codeOff;
    const uint32_t funcArrayRva = sectionRva + funcArrayOff;
    const uint32_t nameArrayRva = sectionRva + nameArrayOff;
    const uint32_t ordArrayRva = sectionRva + ordArrayOff;
    const uint32_t exportDirRva = sectionRva + exportDirOff;
    const uint32_t nameStrRva = sectionRva + nameStrOff;
    const uint32_t dllNameRva = sectionRva + dllNameOff;

    ByteBuf section;
    section.u8(0xB8); section.u32le(uint32_t(returnValue)); section.u8(0xC3); // mov eax, imm32 ; ret
    section.zeros(funcArrayOff - section.pos());
    section.u32le(codeRva);                 // AddressOfFunctions[0] = RVA الدالة المُصدَّرة
    section.zeros(nameArrayOff - section.pos());
    section.u32le(nameStrRva);              // AddressOfNames[0] = RVA اسمها النصي
    section.zeros(ordArrayOff - section.pos());
    section.u16le(0);                        // AddressOfNameOrdinals[0] = 0 (الفهرس ضمن AddressOfFunctions)
    section.zeros(exportDirOff - section.pos());
    section.u32le(0);                        // Characteristics
    section.u32le(0);                        // TimeDateStamp
    section.u16le(0); section.u16le(0);       // Major/MinorVersion
    section.u32le(dllNameRva);               // Name -> اسم ملف الـDLL
    section.u32le(1);                        // Base (رقم الأورديننال الأول)
    section.u32le(1);                        // NumberOfFunctions
    section.u32le(1);                        // NumberOfNames
    section.u32le(funcArrayRva);             // AddressOfFunctions
    section.u32le(nameArrayRva);             // AddressOfNames
    section.u32le(ordArrayRva);              // AddressOfNameOrdinals
    section.zeros(nameStrOff - section.pos());
    section.str(exportName, true);
    section.zeros(dllNameOff - section.pos());
    section.str(dllFileName, true);
    if (section.pos() != sectionContentLen) throw std::runtime_error("PE dll: internal section-size mismatch");

    const uint32_t sectionRawSize = alignUp(sectionContentLen, FILE_ALIGN);
    const uint32_t sizeOfImage = alignUp(sectionRva + alignUp(sectionContentLen, SECTION_ALIGN), SECTION_ALIGN);

    ByteBuf b;
    writeDosHeader(b, 64);
    writeCoffHeader(b, NUM_SECTIONS, uint16_t(OPT_HDR_SIZE),
                     0x0002 /*EXECUTABLE_IMAGE*/ | 0x0020 /*LARGE_ADDRESS_AWARE*/ | 0x2000 /*DLL*/);
    // AddressOfEntryPoint=0: لا DllMain — حالة صالحة رسمياً، يتجاهلها المحمّل عند التحميل
    writeOptionalHeader64(b, 0, codeRva, imageBase, sectionRawSize, sizeOfImage, headersFileSize,
                           3 /*IMAGE_SUBSYSTEM_WINDOWS_CUI*/, 0,
                           0, 0, exportDirRva, exportDirSize);
    writeSectionHeader(b, ".text", sectionContentLen, sectionRva, sectionRawSize, headersFileSize,
                        0x60000020u | 0x80000000u);
    b.zeros(headersFileSize - b.pos());
    b.bytes(section.data);
    b.zeros(headersFileSize + sectionRawSize - b.pos());

    return b.toString();
}

std::string buildCom(uint8_t exitCode) {
    // .com: بلا أي ترويسة إطلاقاً — أول بايت في الملف هو أول تعليمة تُنفَّذ فعلياً عند
    // العنوان الحقيقي 0x100 (يحمّله DOS/DOSBox هناك دوماً بالتعريف). كود حقيقي 16-بت:
    //   mov al, exitCode   ; B0 imm8
    //   mov ah, 0x4C       ; B4 4C            (دالة DOS: Terminate with return code)
    //   int 0x21           ; CD 21            (استدعاء مقاطعة DOS حقيقية لإنهاء البرنامج)
    ByteBuf b;
    b.u8(0xB0); b.u8(exitCode);
    b.u8(0xB4); b.u8(0x4C);
    b.u8(0xCD); b.u8(0x21);
    return b.toString();
}

} // namespace binfmt::pe
