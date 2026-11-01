#!/usr/bin/env sh
set -eu
APP="$HOME/Applications/indsin Viewer.app"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$APP" >/dev/null 2>&1 || true
rm -rf "$APP"
echo "indsin Viewer removed."
