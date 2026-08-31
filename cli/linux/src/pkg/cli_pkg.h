// cli/linux/src/pkg/cli_pkg.h
// ============================================================================
// RinPM :: CLI — ينفّذ `rin pkg <command> [...]` فعلياً (قسم 11 من المواصفة).
// يُستدعى من cli/linux/src/main.cpp عندما يكون الأمر الأول "pkg".
// ============================================================================
#pragma once
#include <vector>
#include <string>

namespace rinpm::cli {

// args: كل ما بعد "pkg" في سطر الأوامر (بدون "rin" و"pkg" أنفسهما).
// يعيد exit code مناسباً (انظر errors.h::ExitCode).
int run(const std::vector<std::string>& args, const std::string& currentRinVersion);

} // namespace rinpm::cli
