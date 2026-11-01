// cli/linux/src/pkg/registry.h
// ============================================================================
// RinPM :: Registry — تجريد يسمح بمصادر متعددة للحزم (قسم 8 من المواصفة).
// المشروع لا يعتمد على أي Registry خارجي أثناء الاختبارات: LocalRegistry
// يعمل بالكامل على القرص المحلي (~/.rin/registry/local بشكل افتراضي) وهو
// المستخدَم فعلياً في اختبارات RinPM والتطوير.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "semver.h"
#include "manifest.h"

namespace rinpm {

struct PackageMeta {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> authors;
    std::string license;
    std::string repository;
    std::string homepage;
    std::vector<std::string> keywords;
    std::vector<DependencySpec> dependencyNames;   // اسم التبعية + قيدها الحقيقي كما نُشر في rin.toml للحزمة
    std::string rinVersionConstraint;
    std::vector<std::string> platforms;
    std::string checksum; // sha256 لأرشيف .rinpkg المقابل في هذا الـ Registry
};

// واجهة Registry مجرّدة: أي مصدر حزم (محلي/ملف/HTTP/مستقبلي) يطبّقها.
class Registry {
public:
    virtual ~Registry() = default;

    virtual std::string kindLabel() const = 0; // "local" | "file" | "http"
    virtual std::string sourceLabel() const = 0; // يُخزَّن في rin.lock كـ "source"

    // كل الإصدارات المتاحة لاسم حزمة معيّن (فارغة إن لم توجد الحزمة إطلاقاً).
    virtual std::vector<Version> availableVersions(const std::string& name) const = 0;

    // بيانات وصفية كاملة لإصدار محدد. يرمي RinPackageNotFound إن لم يوجد.
    virtual PackageMeta fetchMeta(const std::string& name, const std::string& version) const = 0;

    // يُنزِّل/ينسخ أرشيف .rinpkg للإصدار المطلوب إلى destArchivePath.
    // يعيد false إن تعذّر الوصول إلى الحزمة (لا يرمي لأخطاء الشبكة العادية،
    // كي يستطيع Resolver تجربة Registry التالي أو الإبلاغ بوضوح).
    virtual bool downloadPackage(const std::string& name, const std::string& version,
                                  const std::string& destArchivePath) const = 0;

    // بحث نصي بسيط بالاسم/الكلمات المفتاحية (`rin pkg search`).
    virtual std::vector<std::string> searchNames(const std::string& query) const = 0;

    // نشر حزمة جديدة (تُستخدم من `rin pkg publish`). ترمي RinRegistryError عند الفشل.
    virtual void publishPackage(const PackageMeta& meta, const std::string& archivePath) = 0;
};

// ----------------------------------------------------------------------------
// LocalRegistry: مصدر حقيقي بالكامل يعمل محلياً بلا شبكة إطلاقاً. يخزّن كل
// إصدار كـ: <root>/<name>/<version>/meta.toml + <name>-<version>.rinpkg
// هذا هو الـ Registry المستخدَم فعلياً في اختبارات RinPM والتطوير بدون إنترنت.
// ----------------------------------------------------------------------------
class LocalRegistry : public Registry {
public:
    explicit LocalRegistry(std::string root);

    std::string kindLabel() const override { return "local"; }
    std::string sourceLabel() const override { return "registry:local"; }

    std::vector<Version> availableVersions(const std::string& name) const override;
    PackageMeta fetchMeta(const std::string& name, const std::string& version) const override;
    bool downloadPackage(const std::string& name, const std::string& version,
                          const std::string& destArchivePath) const override;
    std::vector<std::string> searchNames(const std::string& query) const override;
    void publishPackage(const PackageMeta& meta, const std::string& archivePath) override;

    const std::string& root() const { return root_; }

private:
    std::string root_;
    std::string metaPath(const std::string& name, const std::string& version) const;
    std::string archivePath(const std::string& name, const std::string& version) const;
};

// FileRegistry: نفس منطق LocalRegistry لكن جذره أي مسار يحدده المستخدم (مثال:
// مجلد مشترك على الشبكة أو محرك خارجي) — يسمح بتوزيع حزم دون خادم HTTP فعلي.
class FileRegistry : public LocalRegistry {
public:
    explicit FileRegistry(std::string root) : LocalRegistry(std::move(root)) {}
    std::string kindLabel() const override { return "file"; }
    std::string sourceLabel() const override { return "registry:file:" + root(); }
};

// HttpRegistry: تجريد جاهز للتفعيل عند توفر خادم RinPM حقيقي (مثال:
// registry.rinlang.org). لا يُستخدم في الاختبارات الحالية لتفادي أي اعتماد
// على الشبكة، لكنه مصمَّم ليكون قابلاً للتفعيل الكامل دون تغيير Resolver:
// يستخدم نفس واجهة Registry ونفس بروتوكول JSON البسيط الموصوف في registry.cpp.
class HttpRegistry : public Registry {
public:
    explicit HttpRegistry(std::string baseUrl, std::string authToken = "");

    std::string kindLabel() const override { return "http"; }
    std::string sourceLabel() const override { return "registry:" + baseUrl_; }

    std::vector<Version> availableVersions(const std::string& name) const override;
    PackageMeta fetchMeta(const std::string& name, const std::string& version) const override;
    bool downloadPackage(const std::string& name, const std::string& version,
                          const std::string& destArchivePath) const override;
    std::vector<std::string> searchNames(const std::string& query) const override;
    void publishPackage(const PackageMeta& meta, const std::string& archivePath) override;

private:
    std::string baseUrl_;
    std::string authToken_;
};

} // namespace rinpm
