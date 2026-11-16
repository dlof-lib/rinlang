// piano_cli — يحوّل مشهد piano (.puo/JSON) إلى SVG أو HTML.
//   piano_cli scene.puo [-o out.svg] [--html]      (الإدخال "-" = stdin)
//   g++ -std=c++17 -O2 piano_cli.cpp -o piano_cli
#include "piano.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

static std::string slurp(const std::string& p) {
    std::stringstream ss;
    if (p == "-") ss << std::cin.rdbuf();
    else {
        std::ifstream f(p, std::ios::binary);
        if (!f) throw std::runtime_error("piano: تعذّر فتح " + p);
        ss << f.rdbuf();
    }
    return ss.str();
}
int main(int argc, char** argv) {
    std::string in, out;
    bool html = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--html") html = true;
        else if (a == "--version") { std::cout << "piano_cli 1.0.0\n"; return 0; }
        else in = a;
    }
    if (in.empty()) { std::cerr << "usage: piano_cli scene.puo [-o out.svg] [--html]\n"; return 2; }
    try {
        piano::J c = piano::parse(slurp(in));
        std::string svg = piano::render_svg(c);
        std::string res = svg + "\n";
        if (html) {
            std::string bg = c.has("bg") && c.at("bg").s.size() && c.at("bg").s[0] == '#' ? c.at("bg").s : "#ffffff";
            std::string t = c.has("title") && piano::tostr(c.at("title")) != "" ? piano::tostr(c.at("title")) : "piano";
            res = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" + piano::esc(t) +
                  "</title>\n<style>html,body{margin:0;min-height:100%;background:" + bg + "}body{display:grid;place-items:center;min-height:100vh}svg{max-width:100%;height:auto}</style>\n</head>\n<body>\n" + svg + "\n</body>\n</html>\n";
        }
        if (out.empty()) std::cout << res;
        else { std::ofstream f(out, std::ios::binary); f << res; }
    } catch (std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
}
