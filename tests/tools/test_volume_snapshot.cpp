#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include <iostream>

int main() {
    std::string source = R"(
        @Volume=archive
            text region = "eu";
            @container.doc=logs
                text label = "سجلات";
            .end/container.doc
            @Containers.Group=cold
                @container.doc=old_logs
                    text label = "أرشيف قديم";
                .end/container.doc
            .end/Containers.Group
        .end/Volume

        print "hasVolume(archive):"; print hasVolume("archive");
        print "volumeNames():"; print volumeNames();
        print "volumeVars(archive):"; print volumeVars("archive");
        print "volumeContainers(archive):"; print volumeContainers("archive");
        print "volumeContainerCount(archive):"; print volumeContainerCount("archive");
        print "volumeHasContainer(archive, old_logs):"; print volumeHasContainer("archive", "old_logs");
        print "volumeSnapshot(archive):"; print volumeSnapshot("archive");
        print "groupParent(cold) [كعضو Group داخل Volume]:"; print groupParent("cold");
    )";
    rin::Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    rin::Parser parser(tokens);
    auto statements = parser.parse();
    rin::Interpreter interp;
    std::cout << interp.run(statements);
    return 0;
}
