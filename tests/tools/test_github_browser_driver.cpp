// harness: أوامر من stdin:  tap NAME | text NAME | tick | dump
#include "indsin/rin_indsin_c_api.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
static std::string J(void* s) { char* j = rin_indsin_session_render_json(s); std::string o = j; rin_free_string(j); return o; }
static bool rect(const std::string& j, const std::string& name, double& x, double& y, double& w, double& h) {
    size_t p = j.find("\"fabric\""); size_t q = j.find("\"name\":\"" + name + "\"", p); if (q == std::string::npos) return false;
    auto num = [&](const char* k) { size_t a = j.find(std::string("\"") + k + "\":", q) + strlen(k) + 3; return atof(j.c_str() + a); };
    x = num("x"); y = num("y"); w = num("w"); h = num("h"); return true;
}
static std::string textOf(const std::string& j, const std::string& name) {
    size_t p = j.find("\"fabric\""); size_t q = j.find("\"name\":\"" + name + "\"", p); if (q == std::string::npos) return "<none>";
    size_t e = j.find("\"children\"", q); size_t nx = j.find("\"name\":\"", q + 8);
    size_t a = j.find("\"text\":\"", q); if (a == std::string::npos || (nx != std::string::npos && a > nx)) return "<notext>";
    a += 8; std::string o; while (a < j.size() && j[a] != '"') { if (j[a] == '\\' && a + 1 < j.size()) { a++; o += j[a] == 'n' ? '\n' : j[a]; } else o += j[a]; a++; } return o;
}
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); std::string src; char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) src.append(b, n); fclose(f);
    void* s = rin_indsin_session_create(src.c_str(), 420);
    std::string cmd, arg;
    while (std::cin >> cmd) {
        if (cmd == "dump") { std::cin >> arg; FILE* o = fopen(arg.c_str(), "wb"); fputs(J(s).c_str(), o); fclose(o); continue; }
        std::cin >> arg;
        std::string j = J(s);
        if (cmd == "tap") { double x, y, w, h; if (!rect(j, arg, x, y, w, h)) { printf("tap %s: not found\n", arg.c_str()); continue; }
            char* r = rin_indsin_session_tap(s, x + w / 2, y + h / 2); std::string R = r; rin_free_string(r);
            size_t e = R.find("\"error\":\""); printf("tap %s -> handled=%d%s\n", arg.c_str(), R.find("\"handled\":true") != std::string::npos, e == std::string::npos ? "" : (" ERROR " + R.substr(e, 160)).c_str()); }
        else if (cmd == "text") printf("%s = %s\n", arg.c_str(), textOf(j, arg).c_str());
        else if (cmd == "has") { double x, y, w, h; printf("%s %s (h=%.0f)\n", arg.c_str(), rect(j, arg, x, y, w, h) ? "present" : "absent", rect(j, arg, x, y, w, h) ? h : 0); }
    }
    rin_indsin_session_free(s);
}
