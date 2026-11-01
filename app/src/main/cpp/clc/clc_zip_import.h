// clc_zip_import.h — قارئ ZIP بسيط self-contained (Store/Deflate فقط، بلا تشفير
// وبلا Zip64) يُستخدَم حصراً لأمر `clc convert in.zip out.rcl` (استيراد/تشغيل بيني
// interoperability مع أرشيفات خارجية). هذا ليس جزءاً من صيغة CLC نفسها ولا يُستخدم
// في pack/unpack العاديين — فقط جسر تحويل باتجاه واحد (zip -> مجلد مؤقت -> pack عادي).
#pragma once
#include <string>

namespace clc {

// يفكّ ضغط كل محتويات ملف zip إلى مجلد على القرص. يرمي ClcFormatError عند أي
// عنصر مشفّر أو مضغوط بطريقة غير مدعومة (method != 0 و != 8) أو مسار غير آمن.
void extractZipToDirectory(const std::string& zipPath, const std::string& destDir);

} // namespace clc
