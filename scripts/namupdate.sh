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
