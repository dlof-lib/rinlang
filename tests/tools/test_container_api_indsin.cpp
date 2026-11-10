// container.api ✕ indsin — يتحقق أن حقول حاوية api (status/data/loading) تُحدِّث الواجهة بعد لمسة (استدعاء متزامن)
// وبعد نبضة session_tick (استدعاء مجدوَل after:1s). بناء (من app/src/main/cpp):
//   g++ -std=c++23 -I. tests/tools/... كل ملفات المحرّك ما عدا cli_main  -lz -lpthread -ldl
#include "indsin/rin_indsin_c_api.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static int failures = 0;
#define CHECK(c, l) do { if (c) printf("  [PASS] %s\n", l); else { printf("  [FAIL] %s\n", l); failures++; } } while (0)

static std::string textOf(void* s, const char* name) {
    char* j = rin_indsin_session_render_json(s); std::string J = j; rin_free_string(j);
    size_t p = J.find(std::string("\"name\":\"") + name + "\""); if (p == std::string::npos) return "<none>";
    size_t t = J.find("\"text\":\"", p); if (t == std::string::npos) return "<notext>"; t += 8;
    return J.substr(t, J.find('"', t) - t);
}
static const char* kSync = R"RIN(
<Rin>
@container.api=shop
    let data = nil; let status = 0; let error = nil; let loading = false; let updated = 0;
    route method="GET" path="/n" status=200 body={"n": 3};
.end/container.api
fun load() { callApi("shop", "GET", "/n"); }
fun later() { callApi("shop", "GET", "/n", {after: 1s}); }
</Rin>
<Column#Root padding={comfortable} gap={normal}>
  <Text#St text={"s=" + shop.status} />
  <Button#Go text="load" onTap={load()} />
  <Button#Later text="later" onTap={later()} />
</Column>
)RIN";

static const char* kPreview = R"RIN(
<Rin>
@container.api=cards
    let data = nil; let status = 0; let preview = nil;
    route method="GET" path="/pic" status=200 body="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
.end/container.api
fun load() { callApi("cards", "GET", "/pic"); }
</Rin>
<Column#Root padding={comfortable} gap={normal}>
  <Image#Pic src={cards.preview} width={64} height={64} />
  <Button#Go text="load" onTap={load()} />
</Column>
)RIN";

int main() {
    void* s = rin_indsin_session_create(kSync, 400);
    CHECK(textOf(s, "St") == "s=0", "initial status is 0");
    char* r = rin_indsin_session_tap(s, 30, 70);   // Go
    std::string R = r; rin_free_string(r);
    CHECK(R.find("\"handler\":\"load()\"") != std::string::npos && R.find("\"shop\"") != std::string::npos, "tap on load() reports the container as changed");
    CHECK(textOf(s, "St") == "s=200", "sync callApi updates the view after a tap");
    rin_indsin_session_free(s);

    s = rin_indsin_session_create(kSync, 400);
    r = rin_indsin_session_tap(s, 30, 125); rin_free_string(r);   // Later
    CHECK(textOf(s, "St") == "s=0", "scheduled call has not run yet");
    bool ticking = false;
    for (int i = 0; i < 4; i++) { usleep(500000); r = rin_indsin_session_tick(s); std::string T = r; rin_free_string(r); if (T.find("\"ticking\":true") != std::string::npos) ticking = true; }
    CHECK(textOf(s, "St") == "s=200", "tick runs the scheduled api call and refreshes the view");
    (void)ticking;
    rin_indsin_session_free(s);

    // صورة معاينة: <Image src={cards.preview}> يتحوّل من بلا مصدر إلى data_uri بعد استدعاء api
    s = rin_indsin_session_create(kPreview, 400);
    char* j = rin_indsin_session_render_json(s); std::string J0 = j; rin_free_string(j);
    CHECK(J0.find("\"src_kind\":\"data_uri\"") == std::string::npos, "no image source before the call");
    r = rin_indsin_session_tap(s, 100, 115); rin_free_string(r);
    j = rin_indsin_session_render_json(s); std::string J1 = j; rin_free_string(j);
    CHECK(J1.find("\"src_kind\":\"data_uri\"") != std::string::npos, "api preview becomes the Image's data_uri source");
    rin_indsin_session_free(s);
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures;
}
