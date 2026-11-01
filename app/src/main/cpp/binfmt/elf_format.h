// elf_format.h — مولّد ELF64 حقيقي (x86_64) بالبايت: تنفيذي (ET_EXEC)، مكتبة مشتركة
// (ET_DYN /.so) مع جدول تصدير ديناميكي حقيقي (dynsym/dynstr/hash/dynamic)، وملف كائن
// قابل لإعادة الربط (ET_REL /.o) لتجميعه لاحقاً داخل أرشيف ar (.a).
// لا اعتماد على أي مكتبة ELF خارجية (لا libelf ولا bfd) — بناء الرؤوس والجداول يدوياً.
#pragma once
#include "bytebuf.h"
#include <string>
#include <vector>
#include <cstdint>

namespace binfmt::elf {

// ينتج تنفيذياً x86_64 حقيقياً على لينكس: عند التشغيل يستدعي exit(exitCode) عبر syscall
// مباشرة (بلا libc، بلا أي اعتماديات ديناميكية) — ELF64 ET_EXEC صالح تماماً لتحميل النواة له.
std::string buildExecutable(uint8_t exitCode);

// ينتج مكتبة مشتركة (.so) حقيقية x86_64 (ET_DYN) تُصدِّر دالة واحدة باسم exportName
// جسمها: mov eax, returnValue ; ret — قابلة للتحميل عبر dlopen() واستدعاء عبر dlsym() فعلياً.
std::string buildSharedObject(const std::string& exportName, int32_t returnValue);

// ينتج ملف كائن قابل لإعادة الربط (.o، ET_REL) x86_64 يحوي رمزاً عاماً واحداً باسم
// symbolName (STB_GLOBAL) جسمه: mov eax, returnValue ; ret — صالح لتمريره لاحقاً إلى ar/ld.
std::string buildRelocatable(const std::string& symbolName, int32_t returnValue);

} // namespace binfmt::elf
