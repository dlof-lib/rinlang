// tools/test_indsin_media.cpp — اختبارات Indsin Media (pickMedia/uploadMedia/clearMedia/removeMedia).
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iindsin ../../../../tools/test_indsin_media.cpp rin_lexer.cpp rin_parser.cpp \
//       rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz -o test_indsin_media
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { if (cond) std::cout << "  [PASS] " << label << "\n"; \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } } while (0)

using namespace indsin;
static StrandPtr byName(const StrandPtr& r, const std::string& n) {
    return findAny(r, [&](const StrandPtr& s){ return s->name == n; });
}
static TapResult tapOn(PipelineResult& r, const std::string& name) {
    Indsin e; e.layout(r.fabric, {0, 400, 0, 1e9}, 0, 0);
    auto s = byName(r.fabric, name);
    return dispatchTap(r.fabric, r.warp, r.program, s->geometry.x + 2, s->geometry.y + 2);
}

int main() {
    std::string src = R"(
warp photo = "";
@view.Column=root
  @view.Button=choose label="pick"; onTap=pickMedia("image", photo, false, 1); .end/view
  @view.Button=send label="send"; onTap=uploadMedia(photo, "https://example.com/up"); .end/view
  @view.Button=bad label="bad"; onTap=uploadMedia(photo, "file:///etc/passwd"); .end/view
  @view.Button=clr label="clr"; onTap=clearMedia(photo); .end/view
  @view.Button=cam label="cam"; onTap=pickMedia("camera", shot, false, 5, 1280, 70); .end/view
  @view.Button=camv label="camv"; onTap=pickMedia("camera:video", clip); .end/view
  @view.Button=rt label="rt"; onTap=uploadMedia(photo, "https://example.com/up", "file", "", 4); .end/view
  @view.Button=stop label="stop"; onTap=cancelUpload(photo); .end/view
  @view.Button=app label="app"; onTap=pickMedia("image", gal2, "append"); .end/view
  @view.Button=auth label="auth"; onTap=uploadMedia(photo, "https://example.com/up", "file", "Bearer abc"); .end/view
  @view.Text=st text=photo_status; .end/view
.end/view
)";
    auto r = runColdPipeline(src);
    CHECK(r.ok, "pipeline ok: " + r.errorMessage); std::cout.flush(); if (!r.ok) return 1;
    CHECK(r.warp.has("photo_status") && r.warp.get("photo_status").asString() == "idle", "derived cells seeded");

    auto t = tapOn(r, "choose");
    CHECK(t.mediaPick && t.pick.cell == "photo" && t.pick.kind == media::Kind::IMAGE && t.pick.maxMb == 1, "pickMedia -> request");

    t = tapOn(r, "send");
    CHECK(!t.mediaUpload && !t.error.empty() && r.warp.get("photo_status").asString() == "error", "upload with nothing selected -> error");

    std::string err;
    auto ch = media::applyPicked(r.warp, "photo",
        R"([{"path":"media/a.jpg","name":"a.jpg","mime":"image/jpeg","size":2048}])", media::Kind::IMAGE, 1, false, false, err);
    CHECK(err.empty() && r.warp.get("photo").asString() == "media/a.jpg" && r.warp.get("photo_count").asNumber() == 1, "picked -> cells");
    CHECK(r.warp.get("photo_sizeText").asString() == "2.0 KB" && r.warp.get("photo_status").asString() == "picked", "size text + status");

    t = tapOn(r, "bad");
    CHECK(!t.mediaUpload && r.warp.get("photo_status").asString() == "error", "file:// upload URL rejected");
    media::applyPicked(r.warp, "photo", R"([{"path":"media/a.jpg","size":10}])", media::Kind::IMAGE, 0, false, false, err);
    t = tapOn(r, "send");
    CHECK(t.mediaUpload && t.upload.url == "https://example.com/up" && r.warp.get("photo_status").asString() == "uploading", "upload request + uploading state");
    t = tapOn(r, "send");
    CHECK(!t.mediaUpload && !t.error.empty() && r.warp.get("photo_status").asString() == "uploading", "second upload refused without clobbering state");

    t = tapOn(r, "cam");
    CHECK(t.mediaPick && t.pick.source == "camera" && t.pick.kind == media::Kind::IMAGE && t.pick.maxDim == 1280 && t.pick.quality == 70 && !t.pick.multiple, "camera pick + maxDim/quality");
    t = tapOn(r, "camv");
    CHECK(t.mediaPick && t.pick.source == "camera" && t.pick.kind == media::Kind::VIDEO && t.pick.maxDim == 0 && t.pick.quality == 85, "camera:video defaults");
    CHECK(r.warp.get("photo_attempt").asNumber() == 1, "attempt = 1 on first upload");
    media::applyProgress(r.warp, "photo", "uploading", 0, "3");
    CHECK(r.warp.get("photo_attempt").asNumber() == 3, "retry attempt number -> _attempt");
    // cancel while uploading: back to picked, selection kept
    t = tapOn(r, "stop");
    CHECK(t.mediaCancel && t.cancelCell == "photo" && r.warp.get("photo_status").asString() == "picked" && r.warp.get("photo_count").asNumber() == 1, "cancelUpload -> picked, selection kept");
    t = tapOn(r, "stop");
    CHECK(!t.mediaCancel && !t.error.empty(), "cancelUpload with no upload -> error");
    t = tapOn(r, "rt");
    CHECK(t.mediaUpload && t.upload.retries == 4, "uploadMedia retries arg (clamped 0..5)");
    t = tapOn(r, "stop");
    t = tapOn(r, "auth");
    CHECK(t.mediaUpload && t.upload.auth == "Bearer abc" && t.upload.retries == 2, "uploadMedia auth arg carried; default retries = 2");
    t = tapOn(r, "app");
    CHECK(t.mediaPick && t.pick.append && t.pick.multiple, "pickMedia append mode");
    { std::string e2; CHECK(!media::validateAuth("a\r\nX: y", e2), "auth header injection rejected"); }
    media::applyProgress(r.warp, "photo", "done", 0, "{\"ok\":true}");
    CHECK(r.warp.get("photo_progress").asNumber() == 100 && r.warp.get("photo_response").asString() == "{\"ok\":true}", "done -> 100% + response");

    // validation
    err.clear();
    media::applyPicked(r.warp, "photo", R"([{"path":"../x.jpg","size":1}])", media::Kind::IMAGE, 0, false, false, err);
    CHECK(!err.empty() && r.warp.get("photo_status").asString() == "error", "path traversal rejected");
    err.clear();
    media::applyPicked(r.warp, "photo", R"([{"path":"media/v.mp4","size":1}])", media::Kind::IMAGE, 0, false, false, err);
    CHECK(!err.empty(), "kind mismatch rejected");
    err.clear();
    media::applyPicked(r.warp, "photo", R"([{"path":"media/big.png","size":5242880}])", media::Kind::IMAGE, 1, false, false, err);
    CHECK(!err.empty(), "size limit enforced");

    // multiple + remove + clear
    err.clear();
    media::applyPicked(r.warp, "gal", R"([{"path":"m/1.png","size":1},{"path":"m/2.png","size":1},{"path":"m/3.png","size":1}])",
                       media::Kind::IMAGE, 0, true, false, err);
    CHECK(r.warp.get("gal_count").asNumber() == 3, "multiple selection");
    std::vector<std::string> c2;
    CHECK(media::removeAt(r.warp, "gal", 1, c2) && r.warp.get("gal_count").asNumber() == 2 && !media::removeAt(r.warp, "gal", 9, c2), "removeAt + range check");
    t = tapOn(r, "clr");
    CHECK(r.warp.get("photo_count").asNumber() == 0 && r.warp.get("photo_status").asString() == "idle", "clearMedia");

    std::cout << (failures ? "FAILED\n" : "ALL PASSED\n");
    return failures ? 1 : 0;
}
