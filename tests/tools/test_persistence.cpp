// tools/test_persistence.cpp
// يثبت أن save/installation أصبحا تنفيذاً فعلياً على القرص (وليس مجرد رسائل طباعة):
//   1) تشغيل أول (interpreter1) يعرّف حاوية، يحفظها (save)، ويثبّتها (installation).
//   2) تشغيل ثانٍ منفصل تماماً (interpreter2 جديد، يحاكي عملية/تشغيل لاحق مختلف) على نفس basePath
//      يتحقق أن isInstalled() ترى التثبيت من التشغيل الأول (فهرس حقيقي على القرص)،
//      ثم يستخدم loadInstalled() لقراءة نسخة الحاوية المحفوظة فعلياً وإعادة تنفيذها،
//      ويطبع متغيراً منها لإثبات أن البيانات نفسها عادت فعلياً من الملف على القرص.
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>
#include <cstdio>

static std::string runSrc(const std::string& source, const std::string& basePath) {
    rin::Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    rin::Parser parser(tokens);
    auto statements = parser.parse();
    rin::Interpreter interp;
    interp.setBasePath(basePath);
    return interp.run(statements);
}

int main() {
    const std::string workspace = "persistence_workspace";

    std::string run1 = R"(
        @container=profile
            text name = "Droy";
            let level = 7;
            let tags = ["dlof", "rin", "android"];
            installation profile;
            save;
        .end/container
    )";

    std::string run2 = R"(
        print isInstalled("profile");
        let ok = loadInstalled("profile");
        print ok;
        // متغيرات الحاوية المحمَّلة تعيش داخل بيئتها الخاصة (كأي container)؛
        // للوصول إليها من الخارج نربطها (tying) داخل حاوية جديدة، تماماً كأي حاوية عادية معرَّفة في نفس الملف.
        @container=check
            tying with=profile;
            print name;
            print level;
            print tags;
        .end/container
    )";

    try {
        std::cout << "---- التشغيل الأول (يحفظ ويثبّت) ----\n";
        std::cout << runSrc(run1, workspace);

        std::cout << "\n---- التشغيل الثاني (Interpreter جديد تماماً، يقرأ من القرص) ----\n";
        std::cout << runSrc(run2, workspace);
    } catch (rin::RinError& e) {
        std::cout << "Parse/Lex error at line " << e.line << ": " << e.message << std::endl;
        return 1;
    }
    return 0;
}
