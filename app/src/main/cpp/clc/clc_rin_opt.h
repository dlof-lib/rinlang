// clc_rin_opt.h — مرحلة "Rin/Text Optimization" + "Token/Dictionary Compression"
// من الـ pipeline المطلوب، قبل مرحلة DEFLATE (Entropy Compression).
//
// الفكرة: تُبنى قاموس مشترك (Rin Symbol Table) من أكثر الرموز/الكلمات تكراراً عبر
// كل ملفات .rin/النصوص في المشروع (أمثلة: container.open، object، return...)، ثم
// يُستبدَل كل ظهور لرمز بمرجع مضغوط (2 بايت بدل النص الكامل). الناتج يُمرَّر بعدها
// لـ DEFLATE فيستفيد من التكرار المتبقي على مستوى البتّات.
//
// الأمان اللفظي: لا نغيّر معنى الكود إطلاقاً — هذا استبدال نصي عكوس بالكامل على
// مستوى البايت (byte-for-byte)، ولو ظهر بايت التحكّم المحجوز (0x01) في المصدر
// الأصلي (نادر جداً، محرف تحكّم) يُرمَّز escape خاص لإعادته حرفياً دون التباس.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace clc {

constexpr uint8_t RIN_OPT_ESCAPE = 0x01;
constexpr uint8_t RIN_OPT_LITERAL_ESCAPE_MARKER = 0xFF; // ESC+0xFF => بايت 0x01 حرفي
constexpr size_t  RIN_OPT_MAX_DICT_SIZE = 250; // ids 0..249، محجوز 0xFF لل escape الحرفي
constexpr size_t  RIN_OPT_MIN_TOKEN_LEN = 4;
constexpr size_t  RIN_OPT_MIN_OCCURRENCES = 3;

// هل يستحق هذا الملف مرحلة تحسين Rin/نصوص؟ (امتداد .rin/.txt/.md/.json/.rincfg...
// أو محتوى خالٍ من بايتات NUL و"يبدو" نصاً UTF-8 صالحاً غالباً).
bool looksLikeTextOrRin(const std::string& path, const std::vector<uint8_t>& content);
bool hasRinExtension(const std::string& path);

// بناء القاموس على مرحلتين يسمحان بمعالجة تدفقية (ملف تلو الآخر) بلا الحاجة لحمل
// كل محتوى المشروع في الذاكرة دفعة واحدة (يُقرأ كل ملف نصي/Rin مرة، تُحسب ترددات
// رموزه، ثم يُطرَح محتواه من الذاكرة قبل الانتقال للتالي):
void accumulateTokenFrequency(const std::vector<uint8_t>& content, std::map<std::string,uint64_t>& freq);
std::vector<std::string> finalizeDictionary(const std::map<std::string,uint64_t>& freq);

// يستبدل الرموز بمراجع القاموس (Encode) — عكوس بالكامل عبر decodeWithDictionary.
std::vector<uint8_t> encodeWithDictionary(const std::vector<uint8_t>& data, const std::vector<std::string>& dict);
// يعيد النص الأصلي بالضبط (Decode) — يجب أن يعطي ORIGINAL == RESTORED دائماً.
std::vector<uint8_t> decodeWithDictionary(const std::vector<uint8_t>& data, const std::vector<std::string>& dict);

} // namespace clc
