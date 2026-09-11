# Rin Artifacts + Container Factory

Rin now exposes a built-in Artifact factory tied to the active `@container` runtime.

## Built-ins

```rin
uuid()
hash("hello")
filename("photo.png")
make.filename("image", "png")
```

Inside a container:

```rin
@container=Product
    let id = uuid();
    let f = container.make.file("product.txt", "Rin Artifact");
    let b = container.make.barcode("RIN-001", "product-barcode", "code128");
    let q = container.make.qr(id, "product-qr", 512);
.end/container
```

Generated artifacts are isolated under:

```text
<project-root>/containers/<container-name>/
```

### Supported artifact operations

- `container.make.file(name, content)` — writes a real UTF-8 text file.
- `container.make.barcode(data, name, "code128")` — generates a standards-compliant Code 128 SVG with checksum.
- `container.make.qr(data, name, size)` — generates a real QR SVG on Android through the ZXing bridge.
- `artifact.info(path)` — returns path/name/extension/size/existence as a Rin map.
- `hash(data)` / `make.hash(data)` — SHA-256.
- `uuid()` / `make.uuid()` — UUID v4.
- `filename(name)` / `make.filename(prefix, extension)` — safe and generated filenames.

The parser also accepts namespace calls such as `make.qr()`, `make.file()`, `container.make.qr()` and `container.make.barcode()`.

## Android editor

The Android app uses `RinArtifactBridge.kt` + ZXing for real QR generation. The existing editor syntax catalog and Rin snippets now expose the new Artifact/Container APIs.

## CLI

The standalone `rin_run` and `rincheck` binaries in the release package include the new parser and Artifact runtime. Barcode/file/hash/UUID work in the CLI. QR is intentionally platform-gated there until a desktop QR backend is supplied; Android uses the real ZXing backend.


## Desktop QR Backend

`rin_run` no longer depends on Android for QR generation. On desktop/CLI, `make.qr()` uses a native backend implemented in `rin_artifact.cpp` that loads `libqrencode` at runtime and converts the resulting QR matrix into standards-compliant SVG.

### Linux
Install the runtime library:

```bash
sudo apt install libqrencode4
```

The executable searches for `libqrencode.so.4` first and then `libqrencode.so`. If neither is available, Rin reports an explicit backend error instead of silently generating a fake QR.

### Example

```rin
let qr = make.qr("https://example.com", "example-qr", 512);
print qr;
```

This creates `example-qr.svg` in the current Rin project path. The SVG can be opened directly or embedded in HTML/UI output.
