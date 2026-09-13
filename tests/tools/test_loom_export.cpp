// tools/test_loom_export.cpp — tests for the PNG/screenshot export engine (spec §21-23):
//   - writePNG()/rasterizeToBuffer() (rin_loom_paint.h): a real, spec-valid PNG encoder built on
//     top of the SAME rasterizer rasterizeToPPM() already used, not a second renderer.
//   - exportFabricToPNG(): whole-Fabric screenshot AND single-Container subtree export.
//   - exportPNG()/screenshot()/exportImage() wired into onTap dispatch (rin_loom_needle.h).
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_export.cpp rin_lexer.cpp rin_parser.cpp \
//       rin_interpreter.cpp rin_http.cpp diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz -o test_loom_export
#include "rin_loom_pipeline.h"
#include "rin_loom_needle.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static loom::StrandPtr byName(const loom::StrandPtr& root, const std::string& n) {
    return loom::findAny(root, [&](const loom::StrandPtr& s){ return s->name == n; });
}
static void layoutAt(loom::PipelineResult& r, double width = 400) {
    loom::Loom engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}

// Reads back just enough of a PNG file to check it's structurally real: the 8-byte signature,
// the IHDR chunk, and the width/height it reports -- without pulling in a PNG *decoder*
// (which would be a second implementation of the very thing we're testing).
struct PngHeader { bool valid = false; uint32_t width = 0, height = 0; unsigned char colorType = 0; };
static PngHeader readPngHeader(const std::string& path) {
    PngHeader h;
    std::ifstream f(path, std::ios::binary);
    if (!f) return h;
    unsigned char sig[8];
    f.read((char*)sig, 8);
    const unsigned char expect[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    if (!f || memcmp(sig, expect, 8) != 0) return h;
    unsigned char lenBuf[4], typeBuf[4], ihdr[13];
    f.read((char*)lenBuf, 4); f.read((char*)typeBuf, 4);
    if (!f || memcmp(typeBuf, "IHDR", 4) != 0) return h;
    f.read((char*)ihdr, 13);
    if (!f) return h;
    h.width  = (ihdr[0]<<24)|(ihdr[1]<<16)|(ihdr[2]<<8)|ihdr[3];
    h.height = (ihdr[4]<<24)|(ihdr[5]<<16)|(ihdr[6]<<8)|ihdr[7];
    h.colorType = ihdr[9];
    h.valid = true;
    return h;
}

int main() {
    // 1. rasterizeToBuffer / writePNG: direct unit test, no .rin source involved --------------
    {
        std::cout << "-- writePNG(): raw encoder correctness --\n";
        loom::DrawList list;
        list.push_back({loom::DrawOp::FILL_RECT, {0,0,10,6}, {200,10,10}, "", 0, 0, 0});
        auto buf = loom::rasterizeToBuffer(list, 10, 6);
        CHECK(buf.size() == 10u*6u*3u, "buffer sized W*H*3");
        CHECK(buf[0] == 200 && buf[1] == 10 && buf[2] == 10, "top-left pixel matches the fill color");

        std::string path = "/tmp/loom_export_unit_test.png";
        std::remove(path.c_str());
        bool ok = loom::writePNG(path, 10, 6, buf);
        CHECK(ok, "writePNG() reports success");
        auto hdr = readPngHeader(path);
        CHECK(hdr.valid, "output file has a valid PNG signature + IHDR chunk");
        CHECK(hdr.width == 10 && hdr.height == 6, "IHDR reports the exact requested dimensions");
        CHECK(hdr.colorType == 2, "IHDR color type is truecolor RGB (2)");

        CHECK(!loom::writePNG("/tmp/loom_export_bad.png", 0, 6, buf), "zero width is rejected, not a crash");
        CHECK(!loom::writePNG("/tmp/loom_export_bad2.png", 10, 6, {}), "undersized buffer is rejected, not a crash");
    }

    // 2. Whole-Fabric screenshot from a real .rin source ---------------------------------------
    {
        std::cout << "-- screenshot(): whole-Fabric export via onTap --\n";
        std::string src = R"(
@view.Column=root
  @view.Button=shot label="Shot"; onTap=screenshot("/tmp/loom_export_full.png"); .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            loom::Dye dye;
            auto shot = byName(r.fabric, "shot");
            double x = shot->geometry.x + 1, y = shot->geometry.y + 1;
            std::remove("/tmp/loom_export_full.png");
            auto res = loom::dispatchTap(r.fabric, r.warp, r.program, x, y, nullptr, &dye);
            CHECK(res.exported && res.error.empty(), "screenshot() reports exported, no error");
            CHECK(res.exportedPath == "/tmp/loom_export_full.png", "reports the exact path it wrote");
            auto hdr = readPngHeader("/tmp/loom_export_full.png");
            CHECK(hdr.valid, "the file it claims to have written is a real PNG");
            CHECK((int)hdr.width == (int)std::ceil(r.fabric->geometry.w) &&
                  (int)hdr.height == (int)std::ceil(r.fabric->geometry.h),
                  "PNG dimensions match the full Fabric's laid-out geometry");
        }
    }

    // 3. Subtree export: exportPNG(target, "path.png") ------------------------------------------
    {
        std::cout << "-- exportPNG(container, path): subtree-only export --\n";
        std::string src = R"(
@view.Column=root
  @view.Container=profile
    @view.Text=name label="Name"; .end/view
  .end/view
  @view.Button=shot label="Shot"; onTap=exportPNG(profile, "/tmp/loom_export_profile.png"); .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            loom::Dye dye;
            auto profile = byName(r.fabric, "profile");
            auto shot = byName(r.fabric, "shot");
            double x = shot->geometry.x + 1, y = shot->geometry.y + 1;
            std::remove("/tmp/loom_export_profile.png");
            auto res = loom::dispatchTap(r.fabric, r.warp, r.program, x, y, nullptr, &dye);
            CHECK(res.exported && res.error.empty(), "exportPNG(container, path) dispatches without error");
            auto hdr = readPngHeader("/tmp/loom_export_profile.png");
            CHECK(hdr.valid, "subtree PNG file is valid");
            CHECK((int)hdr.width == (int)std::ceil(profile->geometry.w) &&
                  (int)hdr.height == (int)std::ceil(profile->geometry.h),
                  "PNG dimensions match ONLY the profile Container's box, not the whole screen");
            CHECK(hdr.width < (uint32_t)std::ceil(r.fabric->geometry.w) || hdr.height < (uint32_t)std::ceil(r.fabric->geometry.h),
                  "subtree export is smaller than a full-screen export would be (sanity check it's really scoped)");
        }
    }

    // 4. Error-safety: missing Dye, unknown target, unrecognized callee --------------------------
    {
        std::cout << "-- error safety --\n";
        std::string src = R"(
@view.Column=root
  @view.Button=a label="A"; onTap=exportPNG("/tmp/loom_export_no_dye.png"); .end/view
  @view.Button=b label="B"; onTap=exportPNG(ghost, "/tmp/loom_export_ghost.png"); .end/view
.end/view
)";
        auto r = loom::runColdPipeline(src);
        CHECK(r.ok, "parses");
        if (r.ok) {
            layoutAt(r);
            auto a = byName(r.fabric, "a");
            auto resNoDye = loom::dispatchTap(r.fabric, r.warp, r.program, a->geometry.x+1, a->geometry.y+1);
            CHECK(!resNoDye.exported && !resNoDye.error.empty(), "exportPNG() without a Dye reports a clear error, not a crash");

            loom::Dye dye;
            auto b = byName(r.fabric, "b");
            auto resGhost = loom::dispatchTap(r.fabric, r.warp, r.program, b->geometry.x+1, b->geometry.y+1, nullptr, &dye);
            CHECK(!resGhost.exported && !resGhost.error.empty(), "exportPNG(unknownId, path) reports a clear error, not a crash");
        }
    }

    // 5. Backward compatibility: old rasterizeToPPM() unaffected --------------------------------
    {
        std::cout << "-- backward compatibility: rasterizeToPPM() unchanged --\n";
        loom::DrawList list;
        list.push_back({loom::DrawOp::FILL_RECT, {0,0,4,4}, {50,60,70}, "", 0, 0, 0});
        std::string path = "/tmp/loom_export_compat.ppm";
        std::remove(path.c_str());
        loom::rasterizeToPPM(list, 4, 4, path);
        std::ifstream f(path, std::ios::binary);
        std::string header; std::getline(f, header);
        CHECK(header == "P6", "still writes a P6 PPM header exactly as before");
    }

    std::cout << "\n" << (failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
