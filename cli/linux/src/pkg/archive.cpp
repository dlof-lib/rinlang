// cli/linux/src/pkg/archive.cpp
#include "archive.h"
#include "errors.h"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <algorithm>

namespace rinpm {

namespace {

bool isDir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void mkdirsFor(const std::string& filePath) {
    std::string dir;
    auto pos = filePath.find_last_of('/');
    if (pos == std::string::npos) return;
    dir = filePath.substr(0, pos);
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        cur.push_back(dir[i]);
        if (dir[i] == '/' || i + 1 == dir.size()) {
            if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
        }
    }
}

// أسماء تُستبعد دائماً من الحزمة المنشورة (أدوات تطوير محلية، لا تخص المستهلكين).
bool isExcluded(const std::string& relPath) {
    static const std::vector<std::string> kExcludedDirs = {"packages/", ".git/", "build/"};
    for (auto& d : kExcludedDirs) {
        if (relPath.compare(0, d.size(), d) == 0) return true;
    }
    if (relPath == "rin.lock") return true;
    return false;
}

void collectFiles(const std::string& root, const std::string& relSoFar, std::vector<std::string>& out) {
    std::string base = relSoFar.empty() ? root : root + "/" + relSoFar;
    DIR* d = ::opendir(base.c_str());
    if (!d) return;
    struct dirent* ent;
    std::vector<std::string> names;
    while ((ent = ::readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    for (auto& name : names) {
        std::string rel = relSoFar.empty() ? name : relSoFar + "/" + name;
        std::string full = root + "/" + rel;
        if (isExcluded(rel)) continue;
        if (isDir(full)) {
            collectFiles(root, rel, out);
        } else {
            out.push_back(rel);
        }
    }
}

} // namespace

std::vector<std::string> packDirectory(const std::string& srcDir, const std::string& destArchivePath) {
    std::vector<std::string> files;
    collectFiles(srcDir, "", files);

    mkdirsFor(destArchivePath);
    std::ofstream out(destArchivePath, std::ios::binary | std::ios::trunc);
    if (!out) throw RinPackageError("RinPackageError", "could not create archive `" + destArchivePath + "`",
                                     ExitCode::GeneralError);

    out << "RINPKG1\n";
    out << files.size() << "\n";
    for (auto& rel : files) {
        std::ifstream in(srcDir + "/" + rel, std::ios::binary);
        if (!in) throw RinPackageError("RinPackageError", "could not read `" + rel + "` while packing",
                                        ExitCode::GeneralError);
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string content = buf.str();
        out << rel << "\t" << content.size() << "\n";
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    return files;
}

std::vector<std::string> unpackArchive(const std::string& archivePath, const std::string& destDir) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in) throw RinPackageError("RinPackageError", "could not open archive `" + archivePath + "`",
                                    ExitCode::GeneralError);
    std::string magic;
    std::getline(in, magic);
    if (magic != "RINPKG1") {
        throw RinPackageError("RinPackageError", "`" + archivePath + "` is not a valid .rinpkg archive",
                               ExitCode::GeneralError);
    }
    std::string countLine;
    std::getline(in, countLine);
    size_t count = 0;
    try { count = static_cast<size_t>(std::stoul(countLine)); }
    catch (...) {
        throw RinPackageError("RinPackageError", "corrupt archive header in `" + archivePath + "`",
                               ExitCode::GeneralError);
    }

    std::vector<std::string> files;
    for (size_t i = 0; i < count; ++i) {
        std::string header;
        if (!std::getline(in, header)) {
            throw RinPackageError("RinPackageError", "truncated archive `" + archivePath + "`", ExitCode::GeneralError);
        }
        auto tab = header.find('\t');
        if (tab == std::string::npos) {
            throw RinPackageError("RinPackageError", "corrupt archive entry in `" + archivePath + "`",
                                   ExitCode::GeneralError);
        }
        std::string rel = header.substr(0, tab);
        size_t size = 0;
        try { size = static_cast<size_t>(std::stoul(header.substr(tab + 1))); }
        catch (...) {
            throw RinPackageError("RinPackageError", "corrupt archive entry size in `" + archivePath + "`",
                                   ExitCode::GeneralError);
        }
        std::string content(size, '\0');
        in.read(&content[0], static_cast<std::streamsize>(size));
        if (static_cast<size_t>(in.gcount()) != size) {
            throw RinPackageError("RinPackageError", "truncated archive content in `" + archivePath + "`",
                                   ExitCode::GeneralError);
        }
        std::string full = destDir + "/" + rel;
        mkdirsFor(full);
        std::ofstream fout(full, std::ios::binary | std::ios::trunc);
        fout.write(content.data(), static_cast<std::streamsize>(content.size()));
        files.push_back(rel);
    }
    return files;
}

} // namespace rinpm
