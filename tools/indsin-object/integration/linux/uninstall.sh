#!/usr/bin/env sh
# Removes everything installed by install.sh.
set -eu
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
rm -rf "$DATA/indsin" "$HOME/.local/bin/indsin-open" "$DATA/mime/packages/indsin.xml" "$DATA/applications/indsin-open.desktop"
for s in 16 32 48 128 256 512; do
  rm -f "$DATA/icons/hicolor/${s}x${s}/mimetypes/application-x-indsin.png" "$DATA/icons/hicolor/${s}x${s}/apps/indsin.png"
done
rm -f "$DATA/icons/hicolor/scalable/mimetypes/application-x-indsin.svg" "$DATA/icons/hicolor/scalable/apps/indsin.svg"
command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$DATA/mime" || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DATA/applications" || true
echo "indsin file association removed."
