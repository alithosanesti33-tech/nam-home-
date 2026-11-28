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
# Sax recordings (about 100 MB), only when missing or outdated. They go in your NAM Home
# library folder (the one the app uses, e.g. on your external SSD), never in the app itself.
LIBFILE="$HOME/Library/Application Support/NAM Home/library-path.txt"
if [ -f "$LIBFILE" ]; then NH="$(cat "$LIBFILE")"; else NH="$HOME/Music/NAM Home"; fi
SAX="$NH/Instruments/Sax"
# never create the folder on your Mac's own disk when the SSD isn't plugged in
nh_ready() { [ -d "$NH" ] || { case "$NH" in /Volumes/*) [ -d "$(echo "$NH" | cut -d/ -f1-3)" ] && mkdir -p "$NH" 2>/dev/null ;; *) mkdir -p "$NH" 2>/dev/null ;; esac; }; }
if [ "$(cat "$SAX/.version" 2>/dev/null)" != "v1" ]; then
  if nh_ready; then
    echo "Downloading the sax recordings into $SAX ..."
    if curl -fL --progress-bar "https://github.com/alithosanesti33-tech/nam-home-/releases/download/sax-recordings/NAM-Home-Sax.zip" -o "$TMP/sax.zip"; then
      rm -rf "$SAX"; mkdir -p "$SAX"
      ditto -x -k "$TMP/sax.zip" "$SAX/"
      echo "Sax recordings installed."
    fi
  else
    echo "Couldn't reach $NH (is the SSD plugged in?). Sax recordings skipped; run this again with it connected."
  fi
fi
# Recorded drum kits for the drum machine (Drums/Kits), plus folders for your loops and drum loops
KITS="$NH/Drums/Kits"
if nh_ready; then
  mkdir -p "$NH/Loops" "$NH/Drums/Loops" "$NH/Setlists" "$NH/Cabs/Inbox"
  if [ "$(cat "$KITS/.version" 2>/dev/null)" != "v1" ]; then
    echo "Downloading the drum kits into $KITS ..."
    if curl -fL --progress-bar "https://github.com/alithosanesti33-tech/nam-home-/releases/download/drum-kits/NAM-Home-Drums.zip" -o "$TMP/drums.zip"; then
      rm -rf "$KITS"; mkdir -p "$KITS"
      ditto -x -k "$TMP/drums.zip" "$KITS/"
      echo "Drum kits installed. Put your own drum loops (WAV) in $NH/Drums/Loops"
    fi
  fi
else
  echo "Couldn't reach $NH (is the SSD plugged in?). Drum kits skipped; run this again with it connected."
fi
rm -rf "$TMP"
# make Finder and the Dock show the current icon instead of a cached one
touch "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP" >/dev/null 2>&1 || true
killall Dock >/dev/null 2>&1 || true
killall Finder >/dev/null 2>&1 || true
# the Cab Studio IR helper (~/namcabs.sh)
curl -fsSL "https://raw.githubusercontent.com/alithosanesti33-tech/nam-home-/main/scripts/namcabs.sh" -o "$HOME/namcabs.sh" 2>/dev/null && chmod +x "$HOME/namcabs.sh" || true
# keep this script itself up to date
curl -fsSL "https://raw.githubusercontent.com/alithosanesti33-tech/nam-home-/main/scripts/namupdate.sh" -o "$HOME/namupdate.sh.new" 2>/dev/null && mv "$HOME/namupdate.sh.new" "$HOME/namupdate.sh" && chmod +x "$HOME/namupdate.sh" || true
echo "Installed. Opening NAM Home..."
open "$APP"
