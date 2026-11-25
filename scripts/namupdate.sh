#!/bin/bash
# Installs (or updates to) the newest NAM Home build from GitHub.
set -e
URL="https://github.com/alithosanesti33-tech/nam-home-/releases/download/latest/NAM-Home.zip"
APP="/Applications/NAM Home.app"
TMP=$(mktemp -d)
echo "Downloading the latest NAM Home..."
curl -fL --progress-bar "$URL" -o "$TMP/NAM-Home.zip"
osascript -e 'quit app "NAM Home"' >/dev/null 2>&1 || true
sleep 1
rm -rf "$APP"
ditto -x -k "$TMP/NAM-Home.zip" /Applications/
xattr -cr "$APP"
# the Logic plugin (Audio Unit)
COMP_DIR="$HOME/Library/Audio/Plug-Ins/Components"
if curl -fsL "https://github.com/alithosanesti33-tech/nam-home-/releases/download/latest/NAM-Home-AU.zip" -o "$TMP/NAM-Home-AU.zip"; then
  mkdir -p "$COMP_DIR"
  rm -rf "$COMP_DIR/NAM Home.component"
  ditto -x -k "$TMP/NAM-Home-AU.zip" "$COMP_DIR/"
  xattr -cr "$COMP_DIR/NAM Home.component"
  # only the app should answer namhome:// links (TONE3000 sign-in comes back through them)
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$COMP_DIR/NAM Home.component" >/dev/null 2>&1 || true
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP" >/dev/null 2>&1 || true
  killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
  echo "Logic plugin installed (Audio Units > NAM Home > NAM Home)."
fi
rm -rf "$TMP"
# make Finder and the Dock show the current icon instead of a cached one
touch "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP" >/dev/null 2>&1 || true
killall Dock >/dev/null 2>&1 || true
killall Finder >/dev/null 2>&1 || true
# keep this script itself up to date
curl -fsSL "https://raw.githubusercontent.com/alithosanesti33-tech/nam-home-/main/scripts/namupdate.sh" -o "$HOME/namupdate.sh.new" 2>/dev/null && mv "$HOME/namupdate.sh.new" "$HOME/namupdate.sh" && chmod +x "$HOME/namupdate.sh" || true
echo "Installed. Opening NAM Home..."
open "$APP"
