// pe_format.h — مولّد PE32+ حقيقي (x86_64) بالبايت: تنفيذي (.exe) باستيراد حقيقي لدالة
// ExitProcess من kernel32.dll (جدول استيراد ILT/IAT/Hint-Name كامل)، ومكتبة ديناميكية
// (.dll) بجدول تصدير حقيقي (Export Directory) يُصدِّر دالة واحدة، إضافة إلى .com خام
// (MS-DOS 16-bit real mode) بلا أي ترويسة إطلاقاً. لا اعتماد على أي مكتبة PE/COFF خارجية.
#pragma once
#include "bytebuf.h"
#include <string>
#include <cstdint>

namespace binfmt::pe {

// PE32+ تنفيذي حقيقي: عند التشغيل يستدعي ExitProcess(exitCode) فعلياً عبر kernel32.dll
// (استيراد ديناميكي حقيقي، لا محاكاة) ثم ينتهي.
std::string buildExecutable(uint32_t exitCode);

// PE32+ DLL حقيقية تُصدِّر دالة واحدة باسم exportName (جسمها: mov eax, imm32 ; ret) عبر
// جدول تصدير (Export Directory) حقيقي، بلا نقطة دخول DllMain (AddressOfEntryPoint=0،
// حالة صالحة رسمياً يتجاهل معها المحمّل استدعاء DllMain).
std::string buildDll(const std::string& exportName, int32_t returnValue, const std::string& dllFileName = "output.dll");

// ملف .com خام لِـ MS-DOS (وضع حقيقي 16-بت): لا ترويسة إطلاقاً، يبدأ التنفيذ من أول بايت
// عند العنوان 0x100. جسمه: mov al, exitCode ; mov ah, 0x4C ; int 0x21 (استدعاء إنهاء DOS حقيقي).
std::string buildCom(uint8_t exitCode);

} // namespace binfmt::pe
