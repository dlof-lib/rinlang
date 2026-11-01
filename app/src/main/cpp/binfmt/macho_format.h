// macho_format.h — مولّد Mach-O64 حقيقي (x86_64) بالبايت: تنفيذي (LC_SEGMENT_64 +
// LC_UNIXTHREAD مباشرةً، ينهي العملية عبر syscall خام دون أي اعتماد على dyld) ومكتبة
// ديناميكية (.dylib، LC_ID_DYLIB + LC_SYMTAB/LC_DYSYMTAB بجدول رموز حقيقي).
// تنبيه هندسي صريح (انظر أيضاً التعليق أعلى كل دالة والـREADME المرفق): بُنيت هذه الوحدة
// ببايتات مطابقة للمواصفة المُوثَّقة رسمياً وتم التحقق من بنيتها آلياً هنا (لا بيئة macOS
// متاحة فعلياً في هذه البيئة)، لكنها لم تُشغَّل على نظام macOS حقيقي؛ ونظام macOS الحديث
// (خصوصاً Apple Silicon) يفرض توقيعاً رقمياً (ولو adhoc عبر codesign) قبل تنفيذ أي ثنائي
// أو تحميل أي dylib — خطوة يجب تنفيذها يدوياً على جهاز macOS فعلي بعد التوليد.
#pragma once
#include "bytebuf.h"
#include <string>
#include <cstdint>

namespace binfmt::macho {

std::string buildExecutable(uint8_t exitCode);
std::string buildDylib(const std::string& exportName, int32_t returnValue, const std::string& installName = "output.dylib");

} // namespace binfmt::macho
