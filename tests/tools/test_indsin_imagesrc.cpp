// tools/test_indsin_imagesrc.cpp — Image Sources: Src, SrcSet, URL, LocalPath, RemotePath, Base64, DataURI,
// Blob, Upload, Download (rin_indsin_imagesrc.h). Build: same command as test_indsin_collapsible.cpp.
#include "rin_indsin_pipeline.h"
#include "rin_indsin_needle.h"
#include "rin_indsin_query.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)
using namespace indsin::imagesrc;
using indsin::StrandPtr;

static StrandPtr byName(const StrandPtr& root, const std::string& n) {
    return indsin::findAny(root, [&](const StrandPtr& s){ return s->name == n; });
}
static const char* PNG_B64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

int main() {
    std::cout << "-- classify --\n";
    CHECK(classify("https://x.com/a.png") == SourceKind::URL, "https URL");
    CHECK(classify("//cdn.x.com/a.png") == SourceKind::URL, "protocol-relative URL");
    CHECK(classify("data:image/png;base64,AAAA") == SourceKind::DATA_URI, "data URI");
    CHECK(classify("blob:https://x.com/1-2") == SourceKind::BLOB, "blob handle");
    CHECK(classify("photos/a.png") == SourceKind::LOCAL_PATH, "relative path is local");
    CHECK(classify("file:///sdcard/a.png") == SourceKind::LOCAL_PATH, "file:// is local");
    CHECK(classify("/img/a.png", true) == SourceKind::REMOTE_PATH && classify("/img/a.png", false) == SourceKind::LOCAL_PATH, "'/x' is remote only when a base exists");
    CHECK(classify("javascript:alert(1)") == SourceKind::NONE && classify("ftp://h/a.png") == SourceKind::NONE, "foreign schemes are refused");
    CHECK(classify("") == SourceKind::NONE, "empty is none");

    std::cout << "-- base64 / mime sniffing --\n";
    std::string clean;
    CHECK(cleanBase64(PNG_B64, clean) && base64DecodedSize(clean) == 70, "valid base64 -> 70 decoded bytes");
    CHECK(!cleanBase64("not base64!!", clean) && !cleanBase64("A", clean) && !cleanBase64("AA=A", clean), "invalid base64 rejected");
    CHECK(sniffMime(base64Prefix(PNG_B64)) == "image/png", "PNG magic bytes");
    CHECK(sniffMime(base64Prefix("/9j/4AAQSkZJRg==")) == "image/jpeg", "JPEG magic bytes");
    CHECK(sniffMime(base64Prefix("R0lGODlhAQABAAAAACw=")) == "image/gif", "GIF magic bytes");
    CHECK(sniffMime(base64Prefix("UklGRiQAAABXRUJQVlA4")) == "image/webp", "WebP magic bytes");
    CHECK(sniffMime(base64Prefix("PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciLz4=")) == "image/svg+xml", "SVG text");

    std::cout << "-- srcset --\n";
    auto c = parseSrcSet("small.jpg 400w, med.jpg 800w,large.jpg 1600w");
    CHECK(c.size() == 3 && c[1].url == "med.jpg" && c[1].w == 800, "parses width descriptors (with/without space after comma)");
    CHECK(pickCandidate(c, 390, 1)->url == "small.jpg", "slot 390 @1x -> 400w");
    CHECK(pickCandidate(c, 390, 2)->url == "med.jpg", "slot 390 @2x needs 780 -> 800w");
    CHECK(pickCandidate(c, 2000, 1)->url == "large.jpg", "nothing big enough -> the largest");
    auto d = parseSrcSet("a.png, b.png 2x, c.png 3x");
    CHECK(d.size() == 3 && pickCandidate(d, 0, 1)->url == "a.png" && pickCandidate(d, 0, 2)->url == "b.png" && pickCandidate(d, 0, 2.5)->url == "c.png", "density descriptors (none = 1x)");
    auto e = parseSrcSet("data:image/png;base64,AAAA 1x, big.png 2x");
    CHECK(e.size() == 2 && e[0].url == "data:image/png;base64,AAAA", "data: URIs containing commas survive srcset parsing");

    std::cout << "-- local path safety / join --\n";
    std::string out, err;
    CHECK(normalizeLocalPath("./a//b/./c.png", out, err) && out == "a/b/c.png", "normalizes ./ and //");
    CHECK(!normalizeLocalPath("../secret.png", out, err) && !normalizeLocalPath("a/../../b.png", out, err), "'..' is refused");
    CHECK(normalizeLocalPath("file:///sdcard/a.png", out, err) && out == "/sdcard/a.png", "file:// stripped, absolute kept");
    CHECK(joinUrl("https://cdn.x.com/", "/img/a.png") == "https://cdn.x.com/img/a.png", "joinUrl dedupes slashes");

    std::cout << "-- pipeline: Image/Avatar normalization --\n";
    std::string src = std::string(R"RIN(
@view.Column=root
  @view.Image=url    src="https://x.com/a.png"; .end/view
  @view.Image=remote remotePath="/img/a.png"; base="https://cdn.x.com"; .end/view
  @view.Image=local  localPath="./photos//a.png"; .end/view
  @view.Image=bare   src="photos/b.jpg"; .end/view
  @view.Image=b64    base64=")RIN") + PNG_B64 + R"RIN("; width=80; .end/view
  @view.Image=b64m   base64="AAAAAAAAAAAA"; mime="image/png"; .end/view
  @view.Image=b64bad base64="@@@"; .end/view
  @view.Image=datai  dataUri="data:image/png;base64,)RIN" + PNG_B64 + R"RIN("; .end/view
  @view.Image=datahtml src="data:text/html;base64,PGgxPmhpPC9oMT4="; .end/view
  @view.Image=blob   blob="blob:https://x.com/1234"; .end/view
  @view.Image=evil   src="javascript:alert(1)"; .end/view
  @view.Image=trav   src="../../etc/passwd"; .end/view
  @view.Image=fb     src="javascript:x"; fallback="fallback.png"; .end/view
  @view.Image=sset   srcset="s.jpg 400w, l.jpg 1600w"; width=390; .end/view
  @view.Image=prio   src="first.png"; url="https://x.com/second.png"; .end/view
  @view.Avatar=av    src="https://x.com/me.png"; .end/view
.end/view
)RIN";
    auto r = indsin::runColdPipeline(src);
    CHECK(r.ok, "parses");
    if (r.ok) {
        auto at = [&](const char* n, const char* k) { auto s = byName(r.fabric, n); return s ? s->attrStr(k, "<none>") : std::string("<no strand>"); };
        CHECK(at("url", "src") == "https://x.com/a.png" && at("url", "src_kind") == "url" && at("url", "src_mime") == "image/png", "URL -> kind url, mime from extension");
        CHECK(at("remote", "src") == "https://cdn.x.com/img/a.png" && at("remote", "src_kind") == "url" && at("remote", "src_from") == "remotePath", "RemotePath + base -> joined URL");
        CHECK(at("local", "src") == "photos/a.png" && at("local", "src_kind") == "local_path", "LocalPath normalized");
        CHECK(at("bare", "src_kind") == "local_path" && at("bare", "src_mime") == "image/jpeg", "bare src is a local path, mime from .jpg");
        CHECK(at("b64", "src_kind") == "base64" && at("b64", "src_mime") == "image/png" && at("b64", "src_bytes") == "70", "Base64: sniffed PNG, 70 bytes");
        CHECK(at("b64", "src").rfind("data:image/png;base64,iVBOR", 0) == 0, "Base64 becomes a renderable data: URI in src");
        CHECK(at("b64m", "src_mime") == "image/png" && at("b64m", "src").rfind("data:image/png;base64,", 0) == 0, "mime= hint used when bytes aren't recognizable");
        CHECK(at("b64bad", "src_error") != "<none>" && at("b64bad", "src_kind") == "none", "invalid base64 -> src_error, kind none");
        CHECK(at("datai", "src_kind") == "data_uri" && at("datai", "src_mime") == "image/png", "DataURI accepted");
        CHECK(at("datahtml", "src_error").find("not an image") != std::string::npos && at("datahtml", "src").empty(), "non-image data: URI refused and src blanked");
        CHECK(at("blob", "src") == "blob:https://x.com/1234" && at("blob", "src_kind") == "blob", "Blob handle passes through");
        CHECK(at("evil", "src_kind") == "none" && at("evil", "src").empty() && at("evil", "src_error") != "<none>", "javascript: source refused and blanked");
        CHECK(at("trav", "src_error").find("..") != std::string::npos && at("trav", "src").empty(), "path traversal refused");
        CHECK(at("fb", "src") == "fallback.png" && at("fb", "src_from") == "fallback" && at("fb", "src_error") != "<none>", "fallback= used, original refusal still reported");
        CHECK(at("sset", "src") == "s.jpg" && at("sset", "src_from") == "srcset", "SrcSet picks the smallest candidate covering the 390px slot");
        CHECK(at("prio", "src") == "first.png", "precedence: src beats url");
        CHECK(at("av", "src_kind") == "url", "Avatar sources are normalized too");
    }

    std::cout << "-- dynamic src=<cell> re-classified on change --\n";
    {
        auto r2 = indsin::runColdPipeline(R"(
warp pic = "photos/a.png";
@view.Column=root
  @view.Image=img src=pic; .end/view
.end/view
)");
        CHECK(r2.ok, "parses");
        if (r2.ok) {
            auto img = byName(r2.fabric, "img");
            CHECK(img->attrStr("src_kind") == "local_path", "starts as a local path");
            r2.warp.set("pic", indsin::Value::txt(std::string("data:image/png;base64,") + PNG_B64));
            std::unordered_map<indsin::StrandId, StrandPtr> index; index[img->id] = img;
            indsin::Shuttle sh; sh.applyWarpChange("pic", r2.warp, r2.subs, index);
            CHECK(img->attrStr("src_kind") == "data_uri" && img->attrStr("src_mime") == "image/png", "after the cell changes to a data URI the strand is re-classified");
            r2.warp.set("pic", indsin::Value::txt("javascript:bad()"));
            sh.applyWarpChange("pic", r2.warp, r2.subs, index);
            CHECK(img->attrStr("src").empty() && img->attrStr("src_error") != "", "a hostile runtime value is refused too");
        }
    }

    std::cout << "-- Download / Upload actions --\n";
    {
        auto r3 = indsin::runColdPipeline(R"RIN(
@view.Column=root
  @view.Button=dl  label="Save";  onTap=downloadImage("https://x.com/img/cat.png", "my cat.png", saved); .end/view
  @view.Button=dl2 label="Save2"; onTap=downloadImage("https://x.com/img/dog.jpg?x=1"); .end/view
  @view.Button=dl3 label="Bad";   onTap=downloadImage("javascript:alert(1)"); .end/view
  @view.Button=dl4 label="Bad2";  onTap=downloadImage("https://u:p@x.com/a.png"); .end/view
  @view.Button=up  label="Up";    onTap=uploadImage(photo, "https://x.com/up"); .end/view
.end/view
)RIN");
        CHECK(r3.ok, "parses");
        if (r3.ok) {
            indsin::Indsin eng; eng.layout(r3.fabric, {0, 360, 0, 1e9}, 0, 0);
            auto tapOn = [&](const char* n) {
                auto b = byName(r3.fabric, n);
                return indsin::dispatchTap(r3.fabric, r3.warp, r3.program, b->geometry.x + 2, b->geometry.y + 2);
            };
            auto t = tapOn("dl");
            CHECK(t.imageDownload && t.error.empty(), "downloadImage() becomes a host request");
            CHECK(t.download.url == "https://x.com/img/cat.png" && t.download.filename == "my cat.png" && t.download.cell == "saved", "request carries url, sanitized filename, cell");
            CHECK(r3.warp.get("saved_status").asString() == "uploading", "cell gets the shared progress vocabulary (status)");
            CHECK(downloadRequestJson(t.download).find("\"op\":\"download\"") != std::string::npos, "request JSON has op=download");
            t = tapOn("dl2");
            CHECK(t.imageDownload && t.download.filename == "dog.jpg", "default filename = last URL segment without the query");
            t = tapOn("dl3");
            CHECK(!t.imageDownload && !t.error.empty(), "javascript: download refused with an error");
            t = tapOn("dl4");
            CHECK(!t.imageDownload && t.error.find("credentials") != std::string::npos, "credentials in URL refused");
            t = tapOn("up");
            CHECK(!t.mediaUpload && t.error.find("uploadMedia") != std::string::npos, "uploadImage() is routed through uploadMedia() validation (nothing picked yet)");
        }
        std::string e2;
        CHECK(safeFilename("../../a b/c?.png") == "_.._a b_c_.png" || safeFilename("../../a b/c?.png").find('/') == std::string::npos, "filename sanitizer strips path separators");
        CHECK(defaultFilename("data:image/jpeg;base64,/9j/4AAQ") == "image.jpg", "data URI download gets image.<ext>");
        CHECK(validateDownload(std::string("data:image/png;base64,") + PNG_B64, e2) && !validateDownload("data:text/html;base64,PGgxPg==", e2), "data: download only for images");
    }

    std::cout << (failures ? "\nFAILURES: " : "\nAll image-source tests passed. failures=") << failures << "\n";
    return failures ? 1 : 0;
}
