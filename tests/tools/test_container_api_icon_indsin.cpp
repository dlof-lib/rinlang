// container.api ✕ icons ✕ indsin — أيقونة تُستورد من ردّ api فيرسمها <Icon icon={pack.icon}> بعد لمسة.
// بناء: كما test_container_api_indsin.cpp.
#include "indsin/rin_indsin_c_api.h"
#include "indsin/rin_indsin_icons.h"
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(c, l) do { if (c) printf("  [PASS] %s\n", l); else { printf("  [FAIL] %s\n", l); failures++; } } while (0)

static std::string textOf(void* s, const char* name) {
    char* j = rin_indsin_session_render_json(s); std::string J = j; rin_free_string(j);
    size_t p = J.find(std::string("\"name\":\"") + name + "\""); if (p == std::string::npos) return "<none>";
    size_t t = J.find("\"text\":\"", p); if (t == std::string::npos) return "<notext>"; t += 8;
    return J.substr(t, J.find('"', t) - t);
}
static std::string attrOf(void* s, const char* name, const char* attr) {
    char* j = rin_indsin_session_render_json(s); std::string J = j; rin_free_string(j);
    size_t p = J.find(std::string("\"name\":\"") + name + "\""); if (p == std::string::npos) return "<none>";
    std::string k = std::string("\"") + attr + "\":\""; size_t t = J.find(k, p); if (t == std::string::npos) return "<noattr>"; t += k.size();
    return J.substr(t, J.find('"', t) - t);
}
static const char* kApp = R"RIN(
<Rin>
@container.api=pack
    let data = nil; let status = 0; let icon = nil; let svg = nil;
    let iconName = "bolt-remote"; let iconGlyph = "G";
    route method="GET" path="/bolt" status=200 body={"svg": "<svg><path d='M13 2L3 14H12L11 22L21 10H12Z'/></svg>"};
.end/container.api
fun load() { callApi("pack", "GET", "/bolt"); }
</Rin>
<Column#Root padding={comfortable} gap={normal}>
  <Icon#Ic icon={pack.icon} />
  <Text#Nm text={"icon=" + pack.icon} />
  <Button#Go text="load" onTap={load()} />
</Column>
)RIN";

int main() {
    void* s = rin_indsin_session_create(kApp, 400);
    CHECK(attrOf(s, "Ic", "icon") == "", "icon attr is empty before the call");
    CHECK(!indsin::iconRegistry().hasVector("bolt-remote"), "icon is not registered before the call");
    char* r = rin_indsin_session_tap(s, 30, 108); rin_free_string(r);
    CHECK(indsin::iconRegistry().hasVector("bolt-remote"), "api response registers the vector path");
    CHECK(indsin::iconRegistry().vectorPath("bolt-remote") == "M13 2L3 14H12L11 22L21 10H12Z", "registered path equals the SVG's d");
    CHECK(attrOf(s, "Ic", "icon") == "bolt-remote" && indsin::iconRegistry().resolve("bolt-remote") == "G", "<Icon> now points at the imported icon, whose glyph resolves to G");
    CHECK(attrOf(s, "Nm", "text") == "icon=bolt-remote", "container field icon holds the registered name");
    rin_indsin_session_free(s);
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures;
}
