// cli/linux/src/pkg/archive.h
// ============================================================================
// RinPM :: Archive — تنسيق أرشيف حزمة بسيط وقابل للتحقق (name-version.rinpkg)
// بلا اعتماديات خارجية (لا zlib هنا عمداً: طبقة الحزم يجب أن تبقى مستقلة عن
// محرك اللغة). الصيغة: رأس نصي "RINPKG1\n" ثم عدد الملفات، ثم لكل ملف:
// "<relative-path>\t<size>\n" متبوعاً بالبايتات الخام. يُحسب SHA-256 على
// محتوى الأرشيف بالكامل من قِبل الطبقة الأعلى (Security) لا من هذه الطبقة.
// ============================================================================
#pragma once
#include <string>
#include <vector>

namespace rinpm {

// يحزم كل الملفات الموجودة تحت srcDir (بشكل متكرر) إلى أرشيف واحد في destArchivePath.
// يتجاهل rin.lock وأي مجلد packages/ أو .git داخل srcDir (لا تُنشر أدوات التطوير).
// يعيد قائمة المسارات النسبية المضمَّنة (لأغراض التقرير/الاختبار).
std::vector<std::string> packDirectory(const std::string& srcDir, const std::string& destArchivePath);

// يفك أرشيف .rinpkg إلى destDir (يُنشئ المجلدات الفرعية عند الحاجة).
// يعيد قائمة المسارات النسبية المستخرجة.
std::vector<std::string> unpackArchive(const std::string& archivePath, const std::string& destDir);

} // namespace rinpm
