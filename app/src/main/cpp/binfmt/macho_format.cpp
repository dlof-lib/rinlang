#include "macho_format.h"
#include <stdexcept>

namespace binfmt::macho {

namespace {

constexpr uint32_t MH_MAGIC_64 = 0xFEEDFACF;
constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr uint32_t CPU_SUBTYPE_X86_64_ALL = 0x00000003;
constexpr uint32_t MH_EXECUTE = 0x2, MH_DYLIB = 0x6;
constexpr uint32_t LC_SEGMENT_64 = 0x19, LC_SYMTAB = 0x2, LC_DYSYMTAB = 0x0B,
                    LC_UNIXTHREAD = 0x5, LC_ID_DYLIB = 0x0D;
constexpr uint32_t VM_PROT_READ = 1, VM_PROT_WRITE = 2, VM_PROT_EXECUTE = 4;

void putSegName(ByteBuf& b, const char* n) { b.fixedStr(n, 16, '\0'); }

// LC_SEGMENT_64 (بلا أقسام): 72 بايت
void writeSegmentCmdHeader(ByteBuf& b, const char* segname, uint64_t vmaddr, uint64_t vmsize,
                            uint64_t fileoff, uint64_t filesize, uint32_t maxprot, uint32_t initprot,
                            uint32_t nsects, uint32_t cmdsize) {
    b.u32le(LC_SEGMENT_64);
    b.u32le(cmdsize);
    putSegName(b, segname);
    b.u64le(vmaddr);
    b.u64le(vmsize);
    b.u64le(fileoff);
    b.u64le(filesize);
    b.u32le(maxprot);
    b.u32le(initprot);
    b.u32le(nsects);
    b.u32le(0); // flags
}

// section_64: 80 بايت
void writeSection64(ByteBuf& b, const char* sectname, const char* segname, uint64_t addr, uint64_t size,
                     uint32_t offset, uint32_t align, uint32_t flags) {
    b.fixedStr(sectname, 16, '\0');
    b.fixedStr(segname, 16, '\0');
    b.u64le(addr);
    b.u64le(size);
    b.u32le(offset);
    b.u32le(align);
    b.u32le(0); // reloff
    b.u32le(0); // nreloc
    b.u32le(flags);
    b.u32le(0); b.u32le(0); b.u32le(0); // reserved1/2/3
}

// كود x86_64 حقيقي خاص بـ Darwin: exit(status) عبر syscall مباشرة (فئة BSD: bit 0x2000000):
//   mov edi, status        ; BF imm32
//   mov eax, 0x2000001     ; B8 imm32   (SYS_exit=1 مع علامة فئة Unix/BSD)
//   syscall                ; 0F 05
std::vector<uint8_t> exitSyscallCode(uint32_t status) {
    ByteBuf c;
    c.u8(0xBF); c.u32le(status);
    c.u8(0xB8); c.u32le(0x2000001u);
    c.u8(0x0F); c.u8(0x05);
    return c.data;
}

std::vector<uint8_t> returnValueCode(int32_t value) {
    ByteBuf c;
    c.u8(0xB8); c.u32le(uint32_t(value));
    c.u8(0xC3);
    return c.data;
}

} // namespace

std::string buildExecutable(uint8_t exitCode) {
    auto code = exitSyscallCode(exitCode);
    const uint64_t codeSize = code.size();
    const uint64_t textVmBase = 0x100000000ull; // بعد __PAGEZERO (أول 4 جيجابايت محجوزة/غير مُعيَّنة)

    const uint32_t headerSize = 32;
    const uint32_t pagezeroCmdSize = 72;
    const uint32_t textCmdSize = 72 + 80; // ترويسة القطاع + قسم __text واحد
    const uint32_t unixthreadCmdSize = 8 + 8 + 168; // cmd+cmdsize + flavor+count + x86_thread_state64
    const uint32_t ncmds = 3;
    const uint32_t sizeofcmds = pagezeroCmdSize + textCmdSize + unixthreadCmdSize;

    const uint32_t codeOff = headerSize + sizeofcmds;
    const uint32_t fileEnd = codeOff + uint32_t(codeSize);
    const uint64_t textVmSize = ((fileEnd + 0xFFFu) & ~0xFFFu); // مقرَّب لأعلى لحجم صفحة 4KB

    const uint64_t entryVA = textVmBase + codeOff;

    ByteBuf b;
    // ---- mach_header_64 ----
    b.u32le(MH_MAGIC_64);
    b.u32le(CPU_TYPE_X86_64);
    b.u32le(CPU_SUBTYPE_X86_64_ALL);
    b.u32le(MH_EXECUTE);
    b.u32le(ncmds);
    b.u32le(sizeofcmds);
    b.u32le(0); // flags
    b.u32le(0); // reserved (حصرياً في النسخة 64-بت)

    // ---- LC_SEGMENT_64 __PAGEZERO: منطقة عنوان افتراضي محجوزة غير مُعيَّنة الصلاحيات إطلاقاً ----
    writeSegmentCmdHeader(b, "__PAGEZERO", 0, textVmBase, 0, 0, 0, 0, 0, pagezeroCmdSize);

    // ---- LC_SEGMENT_64 __TEXT: يغطي الملف كاملاً (ترويسة+أوامر تحميل+كود) ----
    writeSegmentCmdHeader(b, "__TEXT", textVmBase, textVmSize, 0, fileEnd,
                           VM_PROT_READ | VM_PROT_EXECUTE, VM_PROT_READ | VM_PROT_EXECUTE, 1, textCmdSize);
    writeSection64(b, "__text", "__TEXT", entryVA, codeSize, codeOff, 4, 0x80000400u /*PURE_INSTRUCTIONS|SOME_INSTRUCTIONS*/);

    // ---- LC_UNIXTHREAD: يضبط RIP مباشرة عند بدء العملية (بلا dyld إطلاقاً، تماماً كأسلوب
    // الثنائيات الساكنة الكلاسيكية على Mach-O) ----
    b.u32le(LC_UNIXTHREAD);
    b.u32le(unixthreadCmdSize);
    b.u32le(4);  // x86_THREAD_STATE64
    b.u32le(42); // x86_THREAD_STATE64_COUNT (عدد كلمات 32-بت في البنية = 168/4)
    // rax,rbx,rcx,rdx,rdi,rsi,rbp,rsp,r8..r15,rip,rflags,cs,fs,gs — كلها صفر عدا rip
    for (int i = 0; i < 16; ++i) b.u64le(0); // rax..r15 (16 حقلاً قبل rip)
    b.u64le(entryVA); // rip
    b.u64le(0); b.u64le(0); b.u64le(0); b.u64le(0); // rflags, cs, fs, gs

    if (b.pos() != codeOff) throw std::runtime_error("Mach-O exe: internal header-size mismatch");
    b.bytes(code);

    return b.toString();
}

std::string buildDylib(const std::string& exportName, int32_t returnValue, const std::string& installName) {
    auto code = returnValueCode(returnValue);
    const uint64_t codeSize = code.size();
    const uint64_t textVmBase = 0; // في dylib العناوين نسبية دوماً (لا أساس تحميل ثابت له معنى)

    const uint32_t headerSize = 32;
    const uint32_t textCmdSize = 72 + 80;
    // LC_ID_DYLIB: header 24 بايت (cmd+cmdsize+dylib struct بلا الاسم) + اسم النص بمحاذاة 8 بايت
    auto alignUp = [](uint32_t v, uint32_t a) { uint32_t r = v % a; return r ? v + (a - r) : v; };
    const uint32_t idDylibNameLen = uint32_t(installName.size() + 1);
    const uint32_t idDylibCmdSize = alignUp(24 + idDylibNameLen, 8);
    const uint32_t symtabCmdSize = 24;
    const uint32_t dysymtabCmdSize = 80;
    const uint32_t ncmds = 4;
    const uint32_t sizeofcmds = textCmdSize + idDylibCmdSize + symtabCmdSize + dysymtabCmdSize;

    const uint32_t codeOff = headerSize + sizeofcmds;
    uint32_t off = codeOff + uint32_t(codeSize);
    off = alignUp(off, 8);
    const uint32_t symoff = off;
    const uint32_t nsyms = 1;
    off = symoff + nsyms * 16; // nlist_64 = 16 بايت
    const uint32_t stroff = off;
    // اصطلاح Mach-O/Darwin: رموز C المُصدَّرة تُخزَّن بشرطة سفلية بادئة "_" (dlsym تضيفها تلقائياً بحثاً)
    std::string strtab; strtab.push_back('\0');
    const uint32_t symNameOff = uint32_t(strtab.size()); strtab += "_" + exportName; strtab.push_back('\0');
    const uint32_t strsize = alignUp(uint32_t(strtab.size()), 8);
    const uint32_t fileEnd = stroff + strsize;

    const uint64_t textVmSize = ((fileEnd + 0xFFFu) & ~0xFFFu);
    const uint64_t funcVA = textVmBase + codeOff;

    ByteBuf b;
    b.u32le(MH_MAGIC_64);
    b.u32le(CPU_TYPE_X86_64);
    b.u32le(CPU_SUBTYPE_X86_64_ALL);
    b.u32le(MH_DYLIB);
    b.u32le(ncmds);
    b.u32le(sizeofcmds);
    b.u32le(0x80u); // MH_TWOLEVEL — مساحة أسماء ثنائية المستوى، الوضع الافتراضي الاعتيادي لأي dylib عادية
    b.u32le(0);

    writeSegmentCmdHeader(b, "__TEXT", textVmBase, textVmSize, 0, fileEnd,
                           VM_PROT_READ | VM_PROT_EXECUTE, VM_PROT_READ | VM_PROT_EXECUTE, 1, textCmdSize);
    writeSection64(b, "__text", "__TEXT", funcVA, codeSize, codeOff, 4, 0x80000400u);

    // ---- LC_ID_DYLIB: هوية هذه المكتبة نفسها (الاسم الذي يراه المستدعي/الرابط) ----
    b.u32le(LC_ID_DYLIB);
    b.u32le(idDylibCmdSize);
    b.u32le(24); // offset اسم dylib داخل بنية الأمر نفسه (مباشرة بعد الحقول الثابتة)
    b.u32le(0);  // timestamp
    b.u32le(0x00010000); // current_version (1.0.0 مُرمَّزة x.y.z)
    b.u32le(0x00010000); // compatibility_version
    b.str(installName, true);
    b.zeros(idDylibCmdSize - (24 + idDylibNameLen));

    // ---- LC_SYMTAB: جدول الرموز الكلاسيكي (نمط nlist) — يحوي رمزنا العام المُصدَّر ----
    b.u32le(LC_SYMTAB);
    b.u32le(symtabCmdSize);
    b.u32le(symoff);
    b.u32le(nsyms);
    b.u32le(stroff);
    b.u32le(strsize);

    // ---- LC_DYSYMTAB: مطلوبة رسمياً مع أي dylib (حتى لو كل الرموز "خارجية معرَّفة محلياً") ----
    b.u32le(LC_DYSYMTAB);
    b.u32le(dysymtabCmdSize);
    b.u32le(0);      // ilocalsym
    b.u32le(0);      // nlocalsym
    b.u32le(0);      // iextdefsym
    b.u32le(nsyms);  // nextdefsym — رمزنا الوحيد يُصنَّف "معرَّف خارجي" (مُصدَّر)
    b.u32le(nsyms);  // iundefsym
    b.u32le(0);      // nundefsym
    b.u32le(0); b.u32le(0); // tocoff, ntoc
    b.u32le(0); b.u32le(0); // modtaboff, nmodtab
    b.u32le(0); b.u32le(0); // extrefsymoff, nextrefsyms
    b.u32le(0); b.u32le(0); // indirectsymoff, nindirectsyms
    b.u32le(0); b.u32le(0); // extreloff, nextrel
    b.u32le(0); b.u32le(0); // locreloff, nlocrel

    if (b.pos() != codeOff) throw std::runtime_error("Mach-O dylib: internal header-size mismatch");
    b.bytes(code);
    b.zeros(symoff - b.pos());
    // nlist_64: n_strx(4) n_type(1) n_sect(1) n_desc(2) n_value(8)
    b.u32le(symNameOff);
    b.u8(0x0F); // N_SECT(0x0e) | N_EXT(0x1) — رمز عام معرَّف داخل قسم
    b.u8(1);    // n_sect = 1 (أول قسم: __text)
    b.u16le(0);
    b.u64le(funcVA);
    b.zeros(stroff - b.pos());
    b.str(strtab);
    b.zeros(fileEnd - b.pos());

    return b.toString();
}

} // namespace binfmt::macho
