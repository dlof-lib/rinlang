// cli/linux/src/pkg/resolver.cpp
#include "resolver.h"
#include "errors.h"
#include <sstream>
#include <deque>
#include <set>
#include <algorithm>
#include <functional>

namespace rinpm {

Resolver::Resolver(std::vector<Registry*> registries, std::string currentRinVersion, std::string currentPlatform)
    : registries_(std::move(registries)),
      currentRinVersion_(std::move(currentRinVersion)),
      currentPlatform_(std::move(currentPlatform)) {}

Registry* Resolver::registryFor(const std::string& name) const {
    for (Registry* r : registries_) {
        if (!r->availableVersions(name).empty()) return r;
    }
    return registries_.empty() ? nullptr : registries_.front();
}

namespace {

struct Requirement {
    std::string requiredBy; // "<root>" أو اسم حزمة
    VersionConstraint constraint;
};

// يدمج قيداً جديداً في نطاق [min,max] تراكمي (تقاطع كل القيود المطلوبة لنفس الاسم).
// يعيد false إن أصبح النطاق فارغاً (تعارض إصدار).
bool intersectInto(std::optional<Version>& min, bool& minIncl, std::optional<Version>& max, bool& maxIncl,
                    const VersionConstraint& c) {
    if (c.matchesAny) return true;
    if (c.min) {
        if (!min || c.min->compare(*min) > 0 || (c.min->compare(*min) == 0 && !c.minInclusive)) {
            min = c.min; minIncl = c.minInclusive;
        }
    }
    if (c.max) {
        if (!max || c.max->compare(*max) < 0 || (c.max->compare(*max) == 0 && !c.maxInclusive)) {
            max = c.max; maxIncl = c.maxInclusive;
        }
    }
    if (min && max) {
        int cmp = min->compare(*max);
        if (cmp > 0) return false;
        if (cmp == 0 && !(minIncl && maxIncl)) return false;
    }
    return true;
}

std::optional<Version> pickBest(const std::optional<Version>& min, bool minIncl,
                                 const std::optional<Version>& max, bool maxIncl,
                                 const std::vector<Version>& available) {
    std::optional<Version> best;
    for (const Version& v : available) {
        if (min) {
            int c = v.compare(*min);
            if (c < 0 || (c == 0 && !minIncl)) continue;
        }
        if (max) {
            int c = v.compare(*max);
            if (c > 0 || (c == 0 && !maxIncl)) continue;
        }
        if (!best || v.compare(*best) > 0) best = v;
    }
    return best;
}

std::string renderTree(const std::vector<std::string>& chain) {
    std::ostringstream os;
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i == 0) { os << chain[i] << "\n"; continue; }
        for (size_t k = 1; k < i; ++k) os << "    ";
        os << "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 " << chain[i] << "\n";
    }
    return os.str();
}

} // namespace

ResolveResult Resolver::resolve(const Manifest& root, bool includeDev) const {
    // requirementsByName: كل القيود المفروضة على حزمة بعينها من أي حزمة تعتمد عليها.
    std::map<std::string, std::vector<Requirement>> requirementsByName;
    // edges: بنية الاعتماد الفعلية بعد الحل (لاكتشاف الدورات هيكلياً).
    std::map<std::string, std::vector<std::string>> edges;
    std::map<std::string, Version> resolvedVersion;
    std::map<std::string, PackageMeta> resolvedMeta;
    std::map<std::string, Registry*> pkgRegistry;

    std::deque<std::string> dirty;
    auto addRequirement = [&](const std::string& requiredBy, const std::string& depName, const std::string& raw) {
        VersionConstraint c;
        if (!tryParseConstraint(raw, c)) {
            throw RinVersionError("`" + requiredBy + "` requires `" + depName + "` with invalid version constraint `" +
                                   raw + "`");
        }
        requirementsByName[depName].push_back({requiredBy, c});
        edges[requiredBy].push_back(depName);
        if (std::find(dirty.begin(), dirty.end(), depName) == dirty.end()) dirty.push_back(depName);
    };

    for (auto& d : root.dependencies) addRequirement("<root>", d.name, d.constraintRaw);
    if (includeDev) for (auto& d : root.devDependencies) addRequirement("<root>", d.name, d.constraintRaw);

    while (!dirty.empty()) {
        std::string name = dirty.front();
        dirty.pop_front();

        Registry* reg = pkgRegistry.count(name) ? pkgRegistry[name] : registryFor(name);
        std::vector<Version> available = reg ? reg->availableVersions(name) : std::vector<Version>{};
        if (available.empty()) {
            std::ostringstream msg;
            msg << "package `" << name << "` was not found in any configured registry";
            throw RinPackageNotFound(msg.str());
        }
        pkgRegistry[name] = reg;

        std::optional<Version> min, max;
        bool minIncl = true, maxIncl = false;
        for (auto& req : requirementsByName[name]) {
            if (!intersectInto(min, minIncl, max, maxIncl, req.constraint)) {
                std::ostringstream msg;
                msg << "Dependency resolution failed\n\nPackage: " << name << "\n\nRequired by:\n";
                for (auto& r : requirementsByName[name]) {
                    msg << "  " << r.requiredBy << " requires " << name << " " << r.constraint.toString() << "\n";
                }
                msg << "\nReason:\n  Version conflict (no single version satisfies all the constraints above)\n";
                msg << "\nRun:\n  rin pkg tree\n";
                throw RinDependencyError(msg.str());
            }
        }

        std::optional<Version> chosen = pickBest(min, minIncl, max, maxIncl, available);
        if (!chosen) {
            std::ostringstream msg;
            msg << "Dependency resolution failed\n\nPackage: " << name << "\n\nRequired:\n";
            for (auto& r : requirementsByName[name]) msg << "  " << r.requiredBy << " requires " << r.constraint.toString() << "\n";
            msg << "\nFound:\n  no available version of `" << name << "` satisfies the constraint(s) above\n";
            msg << "\nReason:\n  Version conflict\n";
            throw RinDependencyError(msg.str());
        }

        bool changed = !resolvedVersion.count(name) || !(resolvedVersion[name] == *chosen);
        resolvedVersion[name] = *chosen;

        if (changed) {
            PackageMeta meta = reg->fetchMeta(name, chosen->toString());

            if (!meta.rinVersionConstraint.empty()) {
                VersionConstraint rc;
                if (tryParseConstraint(meta.rinVersionConstraint, rc) && !currentRinVersion_.empty()) {
                    Version curRin;
                    if (tryParseVersion(currentRinVersion_, curRin) && !rc.matches(curRin)) {
                        throw RinVersionError("package `" + name + "@" + chosen->toString() +
                                               "` requires Rin " + meta.rinVersionConstraint +
                                               " but this Rin is " + currentRinVersion_ +
                                               " (unsupported Rin version)");
                    }
                }
            }
            if (!meta.platforms.empty() && !currentPlatform_.empty()) {
                bool ok = std::find(meta.platforms.begin(), meta.platforms.end(), currentPlatform_) != meta.platforms.end();
                if (!ok) {
                    throw RinPackageError("RinPlatformError",
                        "package `" + name + "@" + chosen->toString() + "` does not support platform `" +
                        currentPlatform_ + "` (supports: " +
                        [&]{ std::string s; for (auto&p:meta.platforms){ if(!s.empty()) s+=", "; s+=p; } return s; }() + ")",
                        ExitCode::InvalidManifest);
                }
            }

            resolvedMeta[name] = meta;
            for (auto& dep : meta.dependencyNames) {
                addRequirement(name, dep.name, dep.constraintRaw);
            }
        } else {
            edges[name]; // تأكيد وجود المدخل حتى لو بلا تبعيات جديدة
        }
    }

    // اكتشاف الدورات: DFS مع مكدس الزيارة الحالي.
    {
        std::set<std::string> visited, inStack;
        std::vector<std::string> path;
        std::function<void(const std::string&)> dfs = [&](const std::string& node) {
            if (inStack.count(node)) {
                path.push_back(node);
                // اقتطاع المسار ليبدأ من أول ظهور للعقدة المكرَّرة (الدورة الفعلية فقط)
                auto it = std::find(path.begin(), path.end(), node);
                std::vector<std::string> cycle(it, path.end());
                std::ostringstream msg;
                msg << "Circular dependency detected\n\n" << renderTree(cycle);
                throw RinDependencyError(msg.str());
            }
            if (visited.count(node)) return;
            visited.insert(node);
            inStack.insert(node);
            path.push_back(node);
            for (auto& next : edges[node]) dfs(next);
            path.pop_back();
            inStack.erase(node);
        };
        dfs("<root>");
    }

    // ترتيب تثبيت طوبولوجي آمن: التبعيات أولاً.
    std::vector<std::string> order;
    std::set<std::string> emitted;
    std::function<void(const std::string&)> emit = [&](const std::string& node) {
        if (emitted.count(node)) return;
        emitted.insert(node);
        for (auto& next : edges[node]) emit(next);
        if (node != "<root>") order.push_back(node);
    };
    emit("<root>");

    ResolveResult result;
    for (auto& name : order) {
        ResolvedPackage rp;
        rp.name = name;
        rp.version = resolvedVersion[name];
        rp.meta = resolvedMeta[name];
        for (auto& dep : resolvedMeta[name].dependencyNames) rp.directDependencies.push_back(dep.name);
        result.packages.push_back(std::move(rp));
    }
    return result;
}

} // namespace rinpm
