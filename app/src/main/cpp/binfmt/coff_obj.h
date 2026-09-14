// coff_obj.h — كاتب ملف كائن COFF حقيقي (Windows .obj غير مربوط، x86_64) — نفس هيكل رأس
// IMAGE_FILE_HEADER المستخدَم داخل ملفات PE، لكن بلا غلاف MZ/PE (ملفات .obj خام كما ينتجها
// أي مُصرِّف قبل الربط). يُستخدَم كعضو داخل أرشيف ar لتكوين مكتبة ساكنة .lib حقيقية.
#pragma once
#include "bytebuf.h"
#include <string>
#include <cstdint>

namespace binfmt::coff {

// ملف .obj حقيقي (x86_64) يحوي رمزاً عاماً خارجياً واحداً باسم symbolName (C_EXT) داخل
// قسم .text، جسمه: mov eax, returnValue ; ret.
std::string buildObject(const std::string& symbolName, int32_t returnValue);

} // namespace binfmt::coff
