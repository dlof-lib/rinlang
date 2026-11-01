// rin_vm — أداة سطر أوامر فوق مكتبة rin_vm.h (بلا اعتماد على محرّك Rin):
//   rin_vm pack prog.rin [-o prog.rvm]   يحزم البرنامج وكل ما يستورده في صورة واحدة
//   rin_vm info prog.rvm                  يعرض محتوى الصورة
//   rin_vm unpack prog.rvm [-d dir]       يستخرج المصدر والملفات المستوردة
// التنفيذ: `rin image prog.rvm [args…]`  أو  `rin_ava -fvm prog.rin -o prog`.
#include "rin_vm.h"
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>

static int usage() {
    std::cerr << "usage: rin_vm pack <prog.rin> [-o out.rvm]\n"
                 "       rin_vm info <prog.rvm>\n"
                 "       rin_vm unpack <prog.rvm> [-d dir]\n";
    return 2;
}

static void mkdirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0755);
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1], in = argv[2], out, dir = ".";
    for (int i = 3; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o") out = argv[++i];
        else if (a == "-d") dir = argv[++i];
    }
    std::string data;
    if (!rin_vm::readFile(in, data)) { std::cerr << "rin_vm: cannot read " << in << "\n"; return 1; }
    if (cmd == "pack") {
        auto name = in.substr(in.find_last_of('/') == std::string::npos ? 0 : in.find_last_of('/') + 1);
        auto im = rin_vm::build(name, in, data);
        if (out.empty()) { out = name; auto d = out.find_last_of('.'); if (d != std::string::npos) out = out.substr(0, d); out += ".rvm"; }
        if (!rin_vm::writeFile(out, rin_vm::serialize(im))) { std::cerr << "rin_vm: cannot write " << out << "\n"; return 1; }
        std::cout << rin_vm::describe(im) << "wrote " << out << "\n";
        return 0;
    }
    rin_vm::Image im; std::string err;
    if (!rin_vm::deserialize(data, im, &err)) { std::cerr << "rin_vm: " << in << ": " << err << "\n"; return 1; }
    if (cmd == "info") { std::cout << rin_vm::describe(im); return 0; }
    if (cmd == "unpack") {
        mkdirs(dir);
        if (!rin_vm::writeFile(dir + "/" + im.name, im.main)) { std::cerr << "rin_vm: cannot write\n"; return 1; }
        for (auto& kv : im.files) {
            if (kv.first.find("..") != std::string::npos || kv.first[0] == '/') continue;   // لا هروب خارج المجلد
            std::string p = dir + "/" + kv.first;
            mkdirs(p.substr(0, p.find_last_of('/')));
            rin_vm::writeFile(p, kv.second);
        }
        std::cout << "unpacked to " << dir << "\n";
        return 0;
    }
    return usage();
}
