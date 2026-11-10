// tools/test_rin_zip.cpp — اختبار محرّك rin_zip.h منفرداً.
//   g++ -std=c++17 -O1 -I app/src/main/cpp tools/test_rin_zip.cpp -lz -o /tmp/test_rin_zip
//   /tmp/test_rin_zip <workdir>      (يكتب <workdir>/out_*.zip ويقرأ <workdir>/ext_*.zip إن وُجدت)
#include "rin_zip.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace rin::zipx;

static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; } } while (0)
static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::ostringstream o; o << f.rdbuf(); return o.str(); }
static void spit(const std::string& p, const std::string& d) { std::ofstream f(p, std::ios::binary); f << d; }
template <class F> static bool throws(F f) { try { f(); } catch (const ZipError&) { return true; } return false; }

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    std::string text; for (int i = 0; i < 400; ++i) text += "مرحباً من Rin — سطر رقم " + std::to_string(i) + "\n";
    std::string bin; { unsigned x = 12345; for (int i = 0; i < 5000; ++i) { x = x * 1103515245u + 12345u; bin.push_back(char(x >> 16)); } }

    Writer w;
    Writer::Add a; a.mtime = 1700000000;
    w.addDir("docs", a.mtime);
    w.addFile("docs/نص عربي.txt", text, a);
    w.addFile("empty.txt", "", a);
    w.addFile("random.bin", bin, a);
    Writer::Add lv0 = a; lv0.level = 0;
    w.addFile("stored.txt", text, lv0);
    Writer::Add enc = a; enc.password = "s3cret";
    w.addFile("secret.txt", "سرّي جداً: " + text, enc);
    w.setComment("hello zip");
    std::string z = w.finish();
    spit(dir + "/out_engine.zip", z);

    Reader r(z);
    CHECK(r.entries().size() == 6);
    CHECK(r.comment() == "hello zip");
    const Entry* e = r.find("docs/نص عربي.txt");
    CHECK(e && e->method == 8 && e->csize < e->usize);
    CHECK(e && r.read(*e) == text);
    CHECK(r.read(*r.find("empty.txt")).empty());
    CHECK(r.read(*r.find("random.bin")) == bin);
    CHECK(r.find("random.bin")->method == 0);        // غير قابل للضغط → Store تلقائي
    CHECK(r.find("stored.txt")->method == 0);        // level 0
    CHECK(r.find("docs/")->isDir());
    CHECK(throws([&] { r.read(*r.find("secret.txt")); }));               // بلا كلمة مرور
    CHECK(throws([&] { r.read(*r.find("secret.txt"), "wrong"); }));      // كلمة خاطئة
    CHECK(r.read(*r.find("secret.txt"), "s3cret") == "سرّي جداً: " + text);

    // تعديل: حذف + إضافة + إعادة تسمية بلا فكّ
    Writer w2;
    for (const Entry& en : r.entries()) {
        if (en.name == "stored.txt") continue;
        w2.addRaw(r, en, en.name == "random.bin" ? "data/r.bin" : "");
    }
    w2.addFile("new.txt", "جديد", a);
    Reader r2(w2.finish());
    CHECK(r2.entries().size() == 6);
    CHECK(!r2.find("stored.txt") && !r2.find("random.bin"));
    CHECK(r2.find("data/r.bin") && r2.read(*r2.find("data/r.bin")) == bin);
    CHECK(r2.read(*r2.find("secret.txt"), "s3cret").size() > 10);        // التشفير يبقى عند النسخ الخام
    CHECK(r2.read(*r2.find("new.txt")) == "جديد");

    // استبدال بنفس الاسم
    Writer w3; w3.addFile("a.txt", "one", a); w3.addFile("a.txt", "two", a);
    Reader r3(w3.finish());
    CHECK(r3.entries().size() == 1 && r3.read(*r3.find("a.txt")) == "two");

    // أمان الأسماء
    CHECK(throws([] { safeName("../x"); }));
    CHECK(throws([] { safeName("a/../../x"); }));
    CHECK(throws([] { safeName("/etc/passwd"); }));
    CHECK(throws([] { safeName("C:/win"); }));
    CHECK(safeName("a\\b//c/./d.txt") == "a/b/c/d.txt");
    CHECK(safeName("dir/") == "dir/");
    CHECK(globMatch("*.TXT", "docs/a.txt") && globMatch("docs/?.txt", "docs/a.txt") && !globMatch("*.bin", "a.txt"));

    // تلف: بايت واحد داخل البيانات → CRC/فكّ
    {
        std::string bad = z;
        size_t pos = r.find("docs/نص عربي.txt")->localOffset + 30 + 22 + 40;
        bad[pos] = char(bad[pos] ^ 0x55);
        Reader rb(bad);
        CHECK(throws([&] { rb.read(*rb.find("docs/نص عربي.txt")); }));
    }
    // أرشيف مبتور / ليس zip
    CHECK(throws([&] { Reader x(z.substr(0, z.size() / 2)); }));
    CHECK(throws([&] { Reader x("not a zip at all, just text......................"); }));
    // حدود الأمان
    { Limits l; l.maxEntries = 2; CHECK(throws([&] { Reader x(z, l); })); }
    { Limits l; l.maxTotal = 100; CHECK(throws([&] { Reader x(z, l); })); }
    { Limits l; l.maxEntrySize = 100; Reader x(z, l); CHECK(throws([&] { x.read(*x.find("docs/نص عربي.txt")); })); }

    // gzip
    std::string g = gzipCompress(text, 9);
    CHECK(g.size() < text.size() && gzipDecompress(g) == text);
    CHECK(throws([&] { gzipDecompress("garbage-not-gzip"); }));
    CHECK(throws([&] { gzipDecompress(g, 100); }));
    spit(dir + "/out_engine.txt.gz", g);

    // أرشيف أنشأته أداة zip الخارجية (إن وُجد)
    std::string ext = slurp(dir + "/ext_system.zip");
    if (!ext.empty()) {
        Reader re(ext);
        CHECK(re.find("hello.txt") && re.read(*re.find("hello.txt")) == "hello from system zip\n");
        CHECK(re.find("sub/") && re.find("sub/نص.txt") && re.read(*re.find("sub/نص.txt")) == "نص عربي\n");
        CHECK(re.find("pw.txt") && re.find("pw.txt")->encrypted() && re.read(*re.find("pw.txt"), "abc123") == "protected by system zip\n");
        std::cout << "ext_system.zip: " << re.entries().size() << " entries OK\n";
    }
    std::cout << (total - fails) << "/" << total << " passed\n";
    return fails ? 1 : 0;
}
