// cli/linux/src/pkg/package_ops.h
// ============================================================================
// RinPM :: Package — Builder/Installer/Validator/Publisher (قسم 26 من
// المواصفة، مجمَّعة هنا في مساحات أسماء فرعية كي تبقى مستقلة منطقياً دون
// تفتيت غير ضروري لعدد الملفات).
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include "manifest.h"
#include "resolver.h"
#include "cache.h"
#include "registry.h"

namespace rinpm::package {

// Validator ------------------------------------------------------------------
namespace validator {
// يتحقق من صحة بنية مجلد حزمة قبل التحزيم/النشر: rin.toml صالح، src/ موجود،
// LICENSE/README موصى بهما (تحذير لا خطأ). يرمي RinManifestError عند خلل حقيقي.
void validatePackageDirectory(const std::string& dir, std::vector<std::string>& warningsOut);
}

// Installer ------------------------------------------------------------------
namespace installer {
// يثبِّت حزمة واحدة محلولة: يتحقق من الكاش أولاً (لا إعادة تنزيل)، وإلا
// يُنزِّل من registry، يتحقق من checksum، يفك الأرشيف في الكاش العام.
// offline=true يمنع أي محاولة تنزيل: يفشل إن لم تكن الحزمة في الكاش مسبقاً.
// يرمي RinChecksumError عند عدم تطابق البصمة، RinPackageNotFound عند فشل التنزيل.
void installResolvedPackage(const ResolvedPackage& pkg, Registry& reg, const Cache& cache, bool offline);
}

// Builder ----------------------------------------------------------------
namespace builder {
// يبني أرشيف .rinpkg من مجلد مشروع/حزمة إلى destDir. يعيد مسار الأرشيف الناتج.
std::string buildArchive(const Manifest& m, const std::string& projectDir, const std::string& destDir);
}

// Publisher --------------------------------------------------------------
namespace publisher {
// ينفّذ تسلسل النشر الكامل (قسم 15): تحقق من المانيفست، تحقق من البنية، بناء
// الأرشيف، حساب checksum، ثم استدعاء registry.publishPackage. يرمي عند أول فشل.
PackageMeta publish(const Manifest& m, const std::string& projectDir, const std::string& buildDir, Registry& reg);
}

} // namespace rinpm::package
