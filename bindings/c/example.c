/* example.c — استدعاء Rin من C/C++ مباشرة (أو أي لغة تدعم الربط مع C ABI:
 * Rust عبر extern "C"، Go عبر cgo، C# عبر P/Invoke، إلخ — نفس الأسلوب).
 *
 * البناء والربط (بعد بناء libRIn عبر bindings/CMakeLists.txt):
 *   gcc example.c -I ../../app/src/main/cpp -L ../build -lrin -o example
 *   LD_LIBRARY_PATH=../build ./example
 */
#include <stdio.h>
#include "rin_c_api.h"

int main(void) {
    printf("Engine: %s\n", rin_engine_version());

    char* out = rin_run("print \"Hello from Rin, called from C!\";", "");
    printf("Output: %s\n", out);
    rin_free_string(out);

    /* جلسة تحافظ على basePath واحد بين عدة استدعاءات */
    RinSession* s = rin_session_create("./rin_data");
    char* out2 = rin_session_run(s, "let x = 40 + 2; print x;");
    printf("Session output: %s\n", out2);
    rin_free_string(out2);
    rin_session_free(s);

    return 0;
}
