#!/bin/bash
# Cab Studio IR helper.
#   ~/namcabs.sh open   opens the download pages for the tweed Deluxe IRs (you click download)
#   ~/namcabs.sh        sorts what you downloaded into Cab Studio's Inbox on your library drive
# Nothing here is uploaded anywhere: the IRs stay on your drive.

LIBFILE="$HOME/Library/Application Support/NAM Home/library-path.txt"
if [ -f "$LIBFILE" ]; then NH="$(cat "$LIBFILE")"; else NH="$HOME/Music/NAM Home"; fi
INBOX="$NH/Cabs/Inbox"
TWEED="$INBOX/Tweed Deluxe 5E3 1x12"

if [ "$1" = "open" ]; then
  echo "Opening the download pages. Download each one (they land in Downloads), then run: ~/namcabs.sh"
  open "https://origineffects.com/product/ir-cab-library/"                      # free; 5E3 Tweed Deluxe with its 1955 Jensen P12Q (+ 6 more cabs)
  open "https://www.tone3000.com/tones/1958-deluxe-ir-1615"                     # free; Oxford Alnico in a 1958 5E3
  open "https://www.tone3000.com/tones/speed-shop-a12q-eminence-1x12-ir-53370"   # free; Weber A12Q (P12Q-style) in an open-back 1x12
  open "https://www.tone3000.com/tones/impulses-for-diy-tweed-deluxe-5e3-head-63881"  # free; DIY 5E3
  exit 0
fi

# never create folders on your Mac's own disk when the SSD isn't plugged in
case "$NH" in
  /Volumes/*) VOL="$(echo "$NH" | cut -d/ -f1-3)"; [ -d "$VOL" ] || { echo "Your library drive ($VOL) isn't plugged in. Nothing moved."; exit 1; } ;;
esac
mkdir -p "$INBOX" || exit 1

ask() { local a; read -r -p "$1 " a </dev/tty; echo "$a"; }
speaker_for() {
  echo "  Which speaker is it?" >/dev/tty
  echo "    1) Jensen P12Q (Origin Effects 5E3)" >/dev/tty
  echo "    2) Oxford Alnico (1958 Deluxe)" >/dev/tty
  echo "    3) Weber A12Q (Speed Shop)" >/dev/tty
  echo "    4) DIY 5E3 (outmodedelectronics)" >/dev/tty
  echo "    5) Jensen P12Q (Audio Assault 5C3)" >/dev/tty
  echo "    6) another speaker (type its name)" >/dev/tty
  case "$(ask '  Number:')" in
    1) echo "Jensen P12Q (Origin 5E3)" ;;
    2) echo "Oxford Alnico (1958 Deluxe)" ;;
    3) echo "Weber A12Q (Speed Shop)" ;;
    4) echo "Jensen DIY 5E3" ;;
    5) echo "Jensen P12Q (Audio Assault 5C3)" ;;
    *) ask "  Speaker name:" ;;
  esac
}

cd "$HOME/Downloads" || exit 1
shopt -s nullglob nocaseglob
moved=0
for f in *.zip *.wav *.aif *.aiff; do
  echo
  echo "$f"
  case "$(ask '  Cab Studio? t = tweed Deluxe, y = other cab pack, n = skip:')" in
    t|T)
      SPK="$(speaker_for)"
      DEST="$TWEED/$SPK"; mkdir -p "$DEST"
      if [[ "$f" == *.zip || "$f" == *.ZIP ]]; then
        TMP="$(mktemp -d)"; ditto -x -k "$f" "$TMP" || { echo "  Couldn't unzip it."; rm -rf "$TMP"; continue; }
        # a library of several cabs (like Origin's): take just the tweed folders, keep the rest as its own pack
        TW="$(find "$TMP" -type d -iname '*tweed*' | grep -iv '__MACOSX')"
        if [ -n "$TW" ]; then
          echo "$TW" | while IFS= read -r d; do
            [ -d "$d" ] || continue
            sub="$(basename "$(dirname "$d")")"
            mkdir -p "$DEST/$sub"; ditto "$d" "$DEST/$sub/$(basename "$d")"; rm -rf "$d"
          done
          REST="$INBOX/$(basename "$f" .zip)"
          mkdir -p "$REST"; ditto "$TMP" "$REST"
          echo "  Tweed IRs -> Tweed Deluxe 5E3 1x12 / $SPK; the other cabs -> $(basename "$REST")"
        else
          ditto "$TMP" "$DEST"
          echo "  -> Tweed Deluxe 5E3 1x12 / $SPK"
        fi
        rm -rf "$TMP"; rm -f "$f"
      else
        mv "$f" "$DEST/" && echo "  -> Tweed Deluxe 5E3 1x12 / $SPK"
      fi
      moved=$((moved + 1)) ;;
    y|Y) mv "$f" "$INBOX/" && echo "  -> Inbox"; moved=$((moved + 1)) ;;
    *) echo "  skipped" ;;
  esac
done
echo
echo "Moved $moved. Open Cab Studio in NAM Home and they'll be in the cab list."
