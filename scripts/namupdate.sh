#!/bin/bash
# Installs (or updates to) the newest NAM Home build from GitHub.
set -e
URL="https://github.com/alithosanesti33-tech/nam-home-/releases/download/latest/NAM-Home.zip"
TMP=$(mktemp -d)
echo "Downloading the latest NAM Home..."
curl -fL --progress-bar "$URL" -o "$TMP/NAM-Home.zip"
osascript -e 'quit app "NAM Home"' >/dev/null 2>&1 || true
sleep 1
rm -rf "/Applications/NAM Home.app"
ditto -x -k "$TMP/NAM-Home.zip" /Applications/
xattr -cr "/Applications/NAM Home.app"
rm -rf "$TMP"
echo "Installed. Opening NAM Home..."
open "/Applications/NAM Home.app"
