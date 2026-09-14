#include "elf_format.h"

namespace binfmt::elf {

namespace {

constexpr uint16_t ET_REL = 1, ET_EXEC = 2, ET_DYN = 3;
constexpr uint16_t EM_X86_64 = 0x3E;

constexpr uint32_t SHT_NULL = 0, SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3,
                    SHT_HASH = 5, SHT_DYNAMIC = 6, SHT_DYNSYM = 11;
constexpr uint64_t SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4;

constexpr uint32_t PT_LOAD = 1, PT_DYNAMIC = 2;
constexpr uint32_t PF_X = 1, PF_W = 2, PF_R = 4;

void writeIdent(ByteBuf& b) {
    b.u8(0x7F); b.u8('E'); b.u8('L'); b.u8('F');
    b.u8(2);      // ELFCLASS64
    b.u8(1);      // ELFDATA2LSB
    b.u8(1);      // EV_CURRENT
    b.u8(0);      // ELFOSABI_SYSV
    b.zeros(8);   // ABI version + padding
}

void writeEhdr(ByteBuf& b, uint16_t e_type, uint64_t e_entry, uint64_t e_phoff, uint64_t e_shoff,
               uint16_t e_phnum, uint16_t e_shnum, uint16_t e_shstrndx) {
    writeIdent(b);
    b.u16le(e_type);
    b.u16le(EM_X86_64);
    b.u32le(1);              // e_version
    b.u64le(e_entry);
    b.u64le(e_phoff);
    b.u64le(e_shoff);
    b.u32le(0);               // e_flags
    b.u16le(64);              // e_ehsize
    b.u16le(e_phnum ? 56 : 0);// e_phentsize
    b.u16le(e_phnum);
    b.u16le(64);              // e_shentsize
    b.u16le(e_shnum);
    b.u16le(e_shstrndx);
}

void writePhdr(ByteBuf& b, uint32_t p_type, uint32_t p_flags, uint64_t p_offset,
               uint64_t p_vaddr, uint64_t p_filesz, uint64_t p_memsz, uint64_t p_align) {
    b.u32le(p_type);
    b.u32le(p_flags);
    b.u64le(p_offset);
    b.u64le(p_vaddr);
    b.u64le(p_vaddr);  // p_paddr == p_vaddr (لا معنى للعنوان الفيزيائي هنا)
    b.u64le(p_filesz);
    b.u64le(p_memsz);
    b.u64le(p_align);
}

void writeShdr(ByteBuf& b, uint32_t sh_name, uint32_t sh_type, uint64_t sh_flags, uint64_t sh_addr,
               uint64_t sh_offset, uint64_t sh_size, uint32_t sh_link, uint32_t sh_info,
               uint64_t sh_addralign, uint64_t sh_entsize) {
    b.u32le(sh_name);
    b.u32le(sh_type);
    b.u64le(sh_flags);
    b.u64le(sh_addr);
    b.u64le(sh_offset);
    b.u64le(sh_size);
    b.u32le(sh_link);
    b.u32le(sh_info);
    b.u64le(sh_addralign);
    b.u64le(sh_entsize);
}

void writeSym(ByteBuf& b, uint32_t st_name, uint8_t st_info, uint8_t st_other, uint16_t st_shndx,
              uint64_t st_value, uint64_t st_size) {
    b.u32le(st_name);
    b.u8(st_info);
    b.u8(st_other);
    b.u16le(st_shndx);
    b.u64le(st_value);
    b.u64le(st_size);
}

uint8_t symInfo(uint8_t bind, uint8_t type) { return uint8_t((bind << 4) | (type & 0xF)); }
constexpr uint8_t STB_GLOBAL = 1, STT_FUNC = 2;

// كود x86_64 حقيقي: exit(status) عبر syscall مباشرة (لا يحتاج libc):
//   mov edi, status   ; BF imm32
//   mov eax, 60       ; B8 3C 00 00 00      (SYS_exit = 60)
//   syscall           ; 0F 05
std::vector<uint8_t> exitSyscallCode(uint32_t status) {
    ByteBuf c;
    c.u8(0xBF); c.u32le(status);
    c.u8(0xB8); c.u32le(60);
    c.u8(0x0F); c.u8(0x05);
    return c.data;
}

// كود x86_64 حقيقي لدالة تُصدَّر: mov eax, imm32 ; ret — تعيد قيمة ثابتة للمستدعي.
std::vector<uint8_t> returnValueCode(int32_t value) {
    ByteBuf c;
    c.u8(0xB8); c.u32le(uint32_t(value));
    c.u8(0xC3);
    return c.data;
}

} // namespace

std::string buildExecutable(uint8_t exitCode) {
    const uint64_t base = 0x400000;
    auto code = exitSyscallCode(exitCode);

    const uint64_t ehdrSize = 64;
    const uint64_t phdrOff = ehdrSize, phdrSize = 56, phCount = 1;
    const uint64_t codeOff = phdrOff + phdrSize * phCount;
    const uint64_t codeSize = code.size();

    std::string shstrtab; shstrtab.push_back('\0');
    const uint32_t nameText = uint32_t(shstrtab.size()); shstrtab += ".text"; shstrtab.push_back('\0');
    const uint32_t nameShstr = uint32_t(shstrtab.size()); shstrtab += ".shstrtab"; shstrtab.push_back('\0');

    const uint64_t shstrOff = codeOff + codeSize;
    const uint64_t shstrSize = shstrtab.size();

    uint64_t shOff = shstrOff + shstrSize;
    { uint64_t rem = shOff % 8; if (rem) shOff += 8 - rem; }
    const uint64_t shCount = 3;
    const uint64_t totalSize = shOff + 64 * shCount;

    ByteBuf b;
    writeEhdr(b, ET_EXEC, base + codeOff, phdrOff, shOff, uint16_t(phCount), uint16_t(shCount), 2);
    writePhdr(b, PT_LOAD, PF_R | PF_X, 0, base, totalSize, totalSize, 0x1000);
    b.bytes(code);
    b.str(shstrtab);
    b.alignTo(8);

    // sh[0] NULL
    writeShdr(b, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);
    // sh[1] .text
    writeShdr(b, nameText, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, base + codeOff, codeOff, codeSize, 0, 0, 1, 0);
    // sh[2] .shstrtab
    writeShdr(b, nameShstr, SHT_STRTAB, 0, 0, shstrOff, shstrSize, 0, 0, 1, 0);

    return b.toString();
}

std::string buildSharedObject(const std::string& exportName, int32_t returnValue) {
    // ملاحظة هندسية حاسمة (اكتُشفت بالاختبار الفعلي عبر dlopen/dlsym حقيقيَين، وليس نظرياً):
    // إن وُضع قسم .dynamic/.dynsym/.hash داخل نفس PT_LOAD الذي يحوي كود .text، ينهار المحمّل
    // الديناميكي الفعلي (ld-linux.so) بخطأ segfault عند القراءة من عنوان صغير جداً (0x8) أثناء
    // "_dl_setup_hash"/معالجة الجدول الديناميكي — بصرف النظر عن صلاحيات RWX الممنوحة لذلك
    // القطاع. الإصلاح الحقيقي (مثبَت تجريبياً على هذا الجهاز عبر ./diag أعلاه): PT_LOAD منفصل
    // خاص بكتلة .dynsym/.dynstr/.hash/.dynamic على صفحة (page) خاصة بها، تماماً كما تفعله
    // مخرجات ld/gcc الحقيقية (RELRO-style page split).
    auto code = returnValueCode(returnValue);
    const uint64_t codeSize = code.size();
    constexpr uint64_t PAGE = 0x1000;

    const uint64_t ehdrSize = 64, phdrSize = 56, phCount = 3; // LOAD(code) + LOAD(dynblock) + DYNAMIC
    const uint64_t phdrOff = ehdrSize;
    const uint64_t codeOff = phdrOff + phdrSize * phCount;

    auto alignUp = [](uint64_t v, uint64_t a) { uint64_t r = v % a; return r ? v + (a - r) : v; };

    // كتلة .dynsym/.dynstr/.hash/.dynamic تبدأ دوماً عند إزاحة PAGE بالضبط (صفحة ثانية، هوية
    // كاملة vaddr==offset) لضمان توافقها كـ PT_LOAD مستقل الصلاحيات (RW) عن صفحة الكود (RX).
    const uint64_t dynsymOff = PAGE;
    const uint64_t dynsymSize = 24 * 2; // NULL + رمز واحد

    const uint64_t dynstrOff = dynsymOff + dynsymSize;
    std::string dynstr; dynstr.push_back('\0');
    const uint32_t exportNameOff = uint32_t(dynstr.size()); dynstr += exportName; dynstr.push_back('\0');
    const uint64_t dynstrSize = dynstr.size();

    const uint64_t hashOff = alignUp(dynstrOff + dynstrSize, 4);
    const uint64_t hashSize = 4 * 5; // nbucket, nchain, bucket[1], chain[2]

    const uint64_t dynamicOff = alignUp(hashOff + hashSize, 8);
    const uint64_t dynamicSize = 16 * 6; // DT_HASH, DT_STRTAB, DT_SYMTAB, DT_STRSZ, DT_SYMENT, DT_NULL
    const uint64_t dynBlockEnd = dynamicOff + dynamicSize;

    std::string shstrtab; shstrtab.push_back('\0');
    auto addName = [&](const char* n) { uint32_t off = uint32_t(shstrtab.size()); shstrtab += n; shstrtab.push_back('\0'); return off; };
    const uint32_t nText = addName(".text");
    const uint32_t nDynsym = addName(".dynsym");
    const uint32_t nDynstr = addName(".dynstr");
    const uint32_t nHash = addName(".hash");
    const uint32_t nDynamic = addName(".dynamic");
    const uint32_t nShstr = addName(".shstrtab");

    // .shstrtab وجدول رؤوس الأقسام يقعان بعد كتلة dynamic مباشرة، بلا حاجة لأي PT_LOAD
    // (لا يحتاجهما المحمّل الديناميكي وقت التشغيل إطلاقاً — فقط أدوات مثل readelf/objdump).
    const uint64_t shstrOff = dynBlockEnd;
    const uint64_t shstrSize = shstrtab.size();

    uint64_t shOff = alignUp(shstrOff + shstrSize, 8);
    const uint64_t shCount = 7; // NULL,.text,.dynsym,.dynstr,.hash,.dynamic,.shstrtab

    // فهارس الأقسام (لحقول sh_link/sh_info وst_shndx)
    const uint16_t idxText = 1, idxDynsym = 2, idxDynstr = 3, idxShstr = 6;

    ByteBuf b;
    writeEhdr(b, ET_DYN, 0, phdrOff, shOff, uint16_t(phCount), uint16_t(shCount), idxShstr);
    // LOAD #1: صفحة الكود فقط (قابلة للقراءة والتنفيذ)
    writePhdr(b, PT_LOAD, PF_R | PF_X, 0, 0, codeOff + codeSize, codeOff + codeSize, PAGE);
    // LOAD #2: صفحة كتلة dynsym/dynstr/hash/dynamic وحدها (قابلة للقراءة والكتابة)
    writePhdr(b, PT_LOAD, PF_R | PF_W, PAGE, PAGE, dynBlockEnd - PAGE, dynBlockEnd - PAGE, PAGE);
    writePhdr(b, PT_DYNAMIC, PF_R | PF_W, dynamicOff, dynamicOff, dynamicSize, dynamicSize, 8);

    b.bytes(code);
    b.zeros(dynsymOff - b.pos());
    // dynsym[0] = STN_UNDEF فارغ إلزامي
    writeSym(b, 0, 0, 0, 0, 0, 0);
    // dynsym[1] = الرمز المُصدَّر
    writeSym(b, exportNameOff, symInfo(STB_GLOBAL, STT_FUNC), 0, idxText, codeOff, codeSize);

    b.zeros(dynstrOff - b.pos());
    b.str(dynstr);

    b.zeros(hashOff - b.pos());
    b.u32le(1);   // nbucket = 1 (دلو واحد يكفي منطقياً لأي عدد صغير من الرموز؛ hash%1 == 0 دوماً)
    b.u32le(2);   // nchain = عدد رموز dynsym (يشمل STN_UNDEF)
    b.u32le(1);   // bucket[0] -> فهرس أول رمز في هذا الدلو = 1 (رمزنا المُصدَّر)
    b.u32le(0);   // chain[0] (غير مستخدَم لفهرس 0/STN_UNDEF)
    b.u32le(0);   // chain[1] = 0 => نهاية السلسلة بعد رمزنا

    b.zeros(dynamicOff - b.pos());
    auto dyn = [&](uint64_t tag, uint64_t val) { b.u64le(tag); b.u64le(val); };
    dyn(4, hashOff);      // DT_HASH
    dyn(5, dynstrOff);    // DT_STRTAB
    dyn(6, dynsymOff);    // DT_SYMTAB
    dyn(10, dynstrSize);  // DT_STRSZ
    dyn(11, 24);          // DT_SYMENT
    dyn(0, 0);            // DT_NULL

    b.zeros(shstrOff - b.pos());
    b.str(shstrtab);
    b.alignTo(8);

    writeShdr(b, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);
    writeShdr(b, nText, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, codeOff, codeOff, codeSize, 0, 0, 1, 0);
    writeShdr(b, nDynsym, SHT_DYNSYM, SHF_ALLOC, dynsymOff, dynsymOff, dynsymSize, idxDynstr, 1, 8, 24);
    writeShdr(b, nDynstr, SHT_STRTAB, SHF_ALLOC, dynstrOff, dynstrOff, dynstrSize, 0, 0, 1, 0);
    writeShdr(b, nHash, SHT_HASH, SHF_ALLOC, hashOff, hashOff, hashSize, idxDynsym, 0, 4, 4);
    writeShdr(b, nDynamic, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, dynamicOff, dynamicOff, dynamicSize, idxDynstr, 0, 8, 16);
    writeShdr(b, nShstr, SHT_STRTAB, 0, 0, shstrOff, shstrSize, 0, 0, 1, 0);

    return b.toString();
}

std::string buildRelocatable(const std::string& symbolName, int32_t returnValue) {
    auto code = returnValueCode(returnValue);
    const uint64_t codeSize = code.size();

    const uint64_t ehdrSize = 64;
    const uint64_t codeOff = ehdrSize; // بلا Program Headers إطلاقاً في ET_REL

    const uint64_t symtabOff = codeOff + codeSize;
    const uint64_t symtabSize = 24 * 2; // NULL + رمز عام واحد

    std::string strtab; strtab.push_back('\0');
    const uint32_t symNameOff = uint32_t(strtab.size()); strtab += symbolName; strtab.push_back('\0');
    const uint64_t strtabOff = symtabOff + symtabSize;
    const uint64_t strtabSize = strtab.size();

    std::string shstrtab; shstrtab.push_back('\0');
    auto addName = [&](const char* n) { uint32_t off = uint32_t(shstrtab.size()); shstrtab += n; shstrtab.push_back('\0'); return off; };
    const uint32_t nText = addName(".text");
    const uint32_t nSymtab = addName(".symtab");
    const uint32_t nStrtab = addName(".strtab");
    const uint32_t nNoteStack = addName(".note.GNU-stack"); // قسم فارغ إعلامي: يخبر الرابط أن المكدّس
                                                              // غير قابل للتنفيذ، فيتجنّب تحذير/سلوك قديم مُهمَل
    const uint32_t nShstr = addName(".shstrtab");

    const uint64_t shstrOff = strtabOff + strtabSize;
    const uint64_t shstrSize = shstrtab.size();

    uint64_t shOff = shstrOff + shstrSize;
    { uint64_t rem = shOff % 8; if (rem) shOff += 8 - rem; }
    const uint64_t shCount = 6; // NULL,.text,.symtab,.strtab,.note.GNU-stack,.shstrtab
    const uint16_t idxText = 1, idxStrtab = 3, idxShstr = 5;

    ByteBuf b;
    writeEhdr(b, ET_REL, 0, 0, shOff, 0, uint16_t(shCount), idxShstr);
    b.bytes(code);
    b.zeros(symtabOff - b.pos());
    writeSym(b, 0, 0, 0, 0, 0, 0); // symtab[0] = فارغ إلزامي
    // symtab[1]: رمز عام (STB_GLOBAL) من نوع دالة، بقيمة = إزاحته داخل .text (صفر هنا لأنه أول شيء فيه)
    writeSym(b, symNameOff, symInfo(STB_GLOBAL, STT_FUNC), 0, idxText, 0, codeSize);
    b.zeros(strtabOff - b.pos());
    b.str(strtab);
    b.zeros(shstrOff - b.pos());
    b.str(shstrtab);
    b.alignTo(8);

    writeShdr(b, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);
    writeShdr(b, nText, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0, codeOff, codeSize, 0, 0, 1, 0);
    // sh_info لـ .symtab = فهرس أول رمز "غير محلي" (لا رموز محلية هنا فتبدأ من 1)
    writeShdr(b, nSymtab, SHT_SYMTAB, 0, 0, symtabOff, symtabSize, idxStrtab, 1, 8, 24);
    writeShdr(b, nStrtab, SHT_STRTAB, 0, 0, strtabOff, strtabSize, 0, 0, 1, 0);
    writeShdr(b, nNoteStack, SHT_PROGBITS, 0, 0, shstrOff, 0, 0, 0, 1, 0); // قسم فارغ (size=0) بلا ALLOC
    writeShdr(b, nShstr, SHT_STRTAB, 0, 0, shstrOff, shstrSize, 0, 0, 1, 0);

    return b.toString();
}

} // namespace binfmt::elf
