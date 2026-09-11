#pragma once
#include <string>
#include <vector>

namespace rin::artifact {

struct Options {
    std::string name;
    std::string format = "svg";
    int width = 600;
    int height = 180;
    int size = 512;
    std::string type = "code128";
};

std::string sanitizeName(const std::string& name);
std::string uniqueFilename(const std::string& prefix, const std::string& extension);
std::string uuidV4();
std::string sha256Hex(const std::string& data);
std::string barcodeSvg(const std::string& data, const Options& options);
std::string qrSvg(const std::string& data, const Options& options);

// Android supplies a real QR encoder (ZXing) through JNI. Desktop/CLI builds may provide
// their own implementation later; without one qrSvg reports a clear unsupported error.
using QrBridge = std::string (*)(const std::string&, int, int);
void setQrBridge(QrBridge bridge);

} // namespace rin::artifact
