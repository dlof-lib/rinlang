// يختبر rin_jni_utf.h (القسم غير المعتمد على JNI) على الجهاز:
//   g++ -std=c++17 -Iapp/src/main/cpp tools/test_jni_utf.cpp -o /tmp/test_jni_utf && /tmp/test_jni_utf
#include "rin_jni_utf.h"
#include <cstdio>
#include <random>
using namespace rin_jni::utf;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main() {
    // ASCII / عربي / إيموجي (4 بايت) / NUL
    for (const std::string s : {std::string("hello"), std::string("مرحبا بالعالم"), std::string("a😀b"),
                                 std::string("x\0y", 3), std::string("")}) {
        std::u16string u = toUtf16(s);
        CHECK(toUtf8(u.data(), u.size()) == s);
    }
    // الإيموجي = surrogate pair واحد وبـ 4 بايت UTF-8 (لا 6 كما في MUTF-8)
    std::u16string e = toUtf16("😀");
    CHECK(e.size() == 2 && e[0] == 0xD83D && e[1] == 0xDE00);
    CHECK(toUtf8(e.data(), e.size()).size() == 4);
    // بايتات تالفة => U+FFFD دون انهيار أو حلقة
    CHECK(toUtf16(std::string("\xFF\xFE")).size() == 2);
    CHECK(toUtf16(std::string("\xE2\x82")).size() == 2);             // مقطوع
    CHECK(toUtf16(std::string("\xC0\x80")).size() == 2);             // overlong NUL (MUTF-8) مرفوض
    CHECK(toUtf16(std::string("\xED\xA0\x80")).size() == 3);         // surrogate مرمَّز (CESU) مرفوض
    // surrogate يتيم في UTF-16
    char16_t lone[] = {0xD83D, u'a'};
    CHECK(toUtf8(lone, 2) == std::string("\xEF\xBF\xBD" "a"));
    // fuzz: لا انهيار، والخرج دائماً UTF-8 صالح يعود دورته بلا تغيير
    std::mt19937 r(1);
    for (int i = 0; i < 200000; ++i) {
        std::string g; int n = r() % 24; for (int k = 0; k < n; ++k) g.push_back((char)(r() & 0xFF));
        std::u16string u = toUtf16(g);
        std::string back = toUtf8(u.data(), u.size());
        std::u16string u2 = toUtf16(back);
        if (u != u2) { CHECK(false); break; }
    }
    std::printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
