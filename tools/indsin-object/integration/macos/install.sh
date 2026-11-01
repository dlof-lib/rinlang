#!/usr/bin/env sh
# Builds "indsin Viewer.app" in ~/Applications and registers the .indsin document type
# (icon + double-click handler). Requires macOS with Node.js. Undo: ./uninstall.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
PKG="$ROOT/tools/indsin-object"
ICONS="$ROOT/assets/branding/indsin"
APP="$HOME/Applications/indsin Viewer.app"
NODE=$(command -v node || true)
[ -n "$NODE" ] || { echo "error: Node.js 16+ is required" >&2; exit 1; }

mkdir -p "$HOME/Applications"
rm -rf "$APP"
TMP=$(mktemp -t indsin).applescript
cat > "$TMP" <<SCRIPT
on open theFiles
	repeat with f in theFiles
		do shell script "RIN_INDSIN_RUN=\"\${RIN_INDSIN_RUN:-}\" '$NODE' '$APP/Contents/Resources/indsin/bin/indsin-open.js' " & quoted form of POSIX path of f & " >/dev/null 2>&1 &"
	end repeat
end open
on run
	display dialog "Double-click a .indsin file (or drop one on this app) to preview it." buttons {"OK"} default button "OK" with title "indsin Viewer"
end run
SCRIPT
osacompile -o "$APP" "$TMP"
rm -f "$TMP"

R="$APP/Contents/Resources"
mkdir -p "$R/indsin/bin" "$R/indsin/src"
cp "$PKG/bin/indsin-open.js" "$PKG/bin/indsin-object.js" "$R/indsin/bin/"
cp "$PKG/src/indsin_highlight.js" "$PKG/src/indsin_object.js" "$R/indsin/src/"
cp "$ICONS/indsin.icns" "$R/applet.icns"
cp "$ICONS/indsin_file.icns" "$R/indsin_file.icns"

PL="$APP/Contents/Info.plist"; PB=/usr/libexec/PlistBuddy
$PB -c "Set :CFBundleName 'indsin Viewer'" "$PL" 2>/dev/null || $PB -c "Add :CFBundleName string 'indsin Viewer'" "$PL"
$PB -c "Add :CFBundleIdentifier string dev.rin.indsin.viewer" "$PL" 2>/dev/null || $PB -c "Set :CFBundleIdentifier dev.rin.indsin.viewer" "$PL"
$PB -c "Delete :CFBundleDocumentTypes" "$PL" 2>/dev/null || true
$PB -c "Add :CFBundleDocumentTypes array" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0 dict" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:CFBundleTypeName string 'indsin UI document'" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:CFBundleTypeRole string Viewer" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:CFBundleTypeIconFile string indsin_file" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:LSItemContentTypes array" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:LSItemContentTypes:0 string dev.rin.indsin" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions array" "$PL"
$PB -c "Add :CFBundleDocumentTypes:0:CFBundleTypeExtensions:0 string indsin" "$PL"
$PB -c "Delete :UTExportedTypeDeclarations" "$PL" 2>/dev/null || true
$PB -c "Add :UTExportedTypeDeclarations array" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0 dict" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeIdentifier string dev.rin.indsin" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeDescription string 'indsin UI document'" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeIconFile string indsin_file" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeConformsTo array" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeConformsTo:0 string public.plain-text" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeTagSpecification dict" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeTagSpecification:public.filename-extension array" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeTagSpecification:public.filename-extension:0 string indsin" "$PL"
$PB -c "Add :UTExportedTypeDeclarations:0:UTTypeTagSpecification:public.mime-type string text/x-indsin" "$PL"

touch "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP" >/dev/null 2>&1 || true
echo "Installed: $APP"
echo "Set it as the default: Finder → select a .indsin file → Get Info → Open with → indsin Viewer → Change All."
