#!/usr/bin/env sh
# Registers the .indsin file type for the current user (no root needed):
# MIME type, file icons, "indsin Viewer" launcher, and double-click handler.
# Usage: ./install.sh        (undo with ./uninstall.sh)
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)                 # repository root
PKG="$ROOT/tools/indsin-object"
ICONS="$ROOT/assets/branding/indsin"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
BIN="$HOME/.local/bin"
HOME_DIR="$DATA/indsin"

command -v node >/dev/null 2>&1 || { echo "error: Node.js 16+ is required (https://nodejs.org)" >&2; exit 1; }

# 1) self-contained copy of the launcher (so moving the repo never breaks the association)
mkdir -p "$HOME_DIR/bin" "$HOME_DIR/src" "$BIN"
cp "$PKG/bin/indsin-open.js" "$PKG/bin/indsin-object.js" "$HOME_DIR/bin/"
cp "$PKG/src/indsin_highlight.js" "$PKG/src/indsin_object.js" "$HOME_DIR/src/"
cat > "$BIN/indsin-open" <<WRAP
#!/usr/bin/env sh
exec node "$HOME_DIR/bin/indsin-open.js" "\$@"
WRAP
chmod +x "$BIN/indsin-open"

# 2) MIME type
mkdir -p "$DATA/mime/packages"
cp "$HERE/indsin-mime.xml" "$DATA/mime/packages/indsin.xml"

# 3) icons: file icon for the MIME type, product icon for the launcher
for s in 16 32 48 128 256 512; do
  d="$DATA/icons/hicolor/${s}x${s}"
  mkdir -p "$d/mimetypes" "$d/apps"
  cp "$ICONS/indsin_file_icon_$s.png" "$d/mimetypes/application-x-indsin.png"
  cp "$ICONS/indsin_icon_$s.png" "$d/apps/indsin.png"
done
mkdir -p "$DATA/icons/hicolor/scalable/mimetypes" "$DATA/icons/hicolor/scalable/apps"
cp "$ICONS/indsin_file_icon.svg" "$DATA/icons/hicolor/scalable/mimetypes/application-x-indsin.svg"
cp "$ICONS/indsin_icon.svg" "$DATA/icons/hicolor/scalable/apps/indsin.svg"

# 4) launcher entry + default handler
mkdir -p "$DATA/applications"
sed "s|@BIN@|$BIN|g" "$HERE/indsin-open.desktop" > "$DATA/applications/indsin-open.desktop"

command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$DATA/mime" || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DATA/applications" || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f "$DATA/icons/hicolor" 2>/dev/null || true
command -v xdg-mime >/dev/null 2>&1 && xdg-mime default indsin-open.desktop text/x-indsin || true

echo "indsin file association installed."
echo "  launcher : $BIN/indsin-open   (make sure $BIN is on your PATH)"
echo "  live preview: export RIN_INDSIN_RUN=/path/to/rin_indsin_run"
