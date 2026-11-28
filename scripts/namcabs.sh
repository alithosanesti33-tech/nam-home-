#!/bin/bash
# Cab Studio IR helper.
#   ~/namcabs.sh open        opens the download pages for free cab IRs (you click download)
#   ~/namcabs.sh open tweed  just the tweed Deluxe ones
#   ~/namcabs.sh             moves the IRs you downloaded into Cab Studio (no questions)
# Nothing here is uploaded anywhere: the IRs stay on your drive.

LIBFILE="$HOME/Library/Application Support/NAM Home/library-path.txt"
if [ -f "$LIBFILE" ]; then NH="$(cat "$LIBFILE")"; else NH="$HOME/Music/NAM Home"; fi
INBOX="$NH/Cabs/Inbox"
TWEED="$INBOX/Tweed Deluxe 5E3 1x12"

if [ "$1" = "open" ]; then
  echo "Opening the download pages. Download each one (they land in Downloads), then run: ~/namcabs.sh"
  echo "If a page has moved or doesn't load, just skip it."
  # tweed Deluxe (press t when sorting)
  open "https://origineffects.com/product/ir-cab-library/"                      # free (email checkout): 5E3 tweed + Brown Deluxe, Twin, AC30, Marshall, Bogner, Magnatone
  open "https://www.tone3000.com/tones/1958-deluxe-ir-1615"                     # free (sign in): Oxford Alnico in a 1958 5E3
  open "https://www.tone3000.com/tones/speed-shop-a12q-eminence-1x12-ir-53370"   # free (sign in): Weber A12Q in an open-back 1x12
  open "https://www.tone3000.com/tones/impulses-for-diy-tweed-deluxe-5e3-head-63881"  # free (sign in): DIY 5E3
  if [ "$2" != "tweed" ]; then
    # other cabs (press y when sorting)
    open "https://wilkinsonaudio.com/collections/free/products/gods-cab"         # God's Cab: Mesa 4x12 Rectifier, 700+ IRs, many mics and positions
    open "http://cabs.kalthallen.de/"                                            # Kalthallen Cabs: several cabs, every mic position
    open "http://www.redwirez.com/free1960g12m25s.jsp"                           # Redwirez: Marshall 1960A with G12M-25 Greenbacks, 17 mics
    open "https://soundwoofer.com/Library/Index"                                 # Soundwoofer: free library of many cabs (pick the ones you like)
    open "https://zachweeks.gumroad.com/l/tl806cabir"                            # Zach Weeks: TL806 + 1960B (type 0 for the price)
    open "https://www.celestionplus.com/free-download/"                          # Celestion's own free speaker IRs
  fi
  exit 0
fi

# never create folders on your Mac's own disk when the SSD isn't plugged in
case "$NH" in
  /Volumes/*) VOL="$(echo "$NH" | cut -d/ -f1-3)"; [ -d "$VOL" ] || { echo "Your library drive ($VOL) isn't plugged in. Nothing moved."; exit 1; } ;;
esac
mkdir -p "$INBOX" || exit 1

# Works out by itself which downloads are cab IR packs (zips full of WAVs, or small WAV files) and
# leaves everything else alone. The tweed Deluxe packs all go into one cab, one speaker each.
tweed_speaker() {
  local n; n="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
  case "$n" in
    *1958*deluxe*|*oxford*)            echo "Oxford Alnico (1958 Deluxe)" ;;
    *speed*shop*|*a12q*)               echo "Weber A12Q (Speed Shop)" ;;
    *usa*tweed*|*audio*assault*)       echo "Jensen P12Q (Audio Assault 5C3)" ;;
    *tweed*|*5e3*|*5c3*)               echo "Tweed speaker" ;;
    *)                                 echo "" ;;
  esac
}
is_ir_zip() {   # has WAVs in it, and no apps or installers
  local list; list="$(unzip -l "$1" 2>/dev/null)" || return 1
  echo "$list" | grep -qi '\.wav$' || return 1
  echo "$list" | grep -qiE '\.(app|pkg|dmg|exe|mp3|m4a)(/|$)' && return 1
  return 0
}

cd "$HOME/Downloads" || exit 1
shopt -s nullglob nocaseglob
moved=0
for f in *.zip; do
  is_ir_zip "$f" || continue
  SPK="$(tweed_speaker "$f")"
  TMP="$(mktemp -d)"
  ditto -x -k "$f" "$TMP" || continue
  rm -rf "$TMP/__MACOSX"
  TW="$(find "$TMP" -type d -iname '*tweed*')"
  if [ -n "$SPK" ]; then
    # a tweed pack: all of it is one speaker of the tweed Deluxe
    mkdir -p "$TWEED/$SPK"; ditto "$TMP" "$TWEED/$SPK"
    echo "Tweed Deluxe <- $f  (speaker: $SPK)"
  elif [ -n "$TW" ]; then
    # a library with a tweed cab inside (Origin Effects): the tweed joins the tweed Deluxe, the rest stays a pack
    echo "$TW" | while IFS= read -r d; do
      [ -d "$d" ] || continue
      sub="$(basename "$(dirname "$d")")"
      mkdir -p "$TWEED/Jensen P12Q (Origin 5E3)/$sub"
      ditto "$d" "$TWEED/Jensen P12Q (Origin 5E3)/$sub/$(basename "$d")" && rm -r "$d"
    done
    REST="$INBOX/$(basename "$f" .zip)"; mkdir -p "$REST"; ditto "$TMP" "$REST"
    echo "Tweed Deluxe <- tweed part of $f  (speaker: Jensen P12Q); the other cabs -> $(basename "$f" .zip)"
  else
    REST="$INBOX/$(basename "$f" .zip)"; mkdir -p "$REST"; ditto "$TMP" "$REST"
    echo "Cab Studio <- $f"
  fi
  rm -r "$TMP"; rm -f "$f"
  moved=$((moved + 1))
done
for f in *.wav *.aif *.aiff; do
  # cab IRs are tiny; anything over 2 MB is probably a song or recording, so it stays put
  [ "$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f")" -lt 2000000 ] || continue
  SPK="$(tweed_speaker "$f")"
  if [ -n "$SPK" ]; then mkdir -p "$TWEED/$SPK"; mv "$f" "$TWEED/$SPK/"; echo "Tweed Deluxe <- $f"
  else mv "$f" "$INBOX/"; echo "Cab Studio <- $f"; fi
  moved=$((moved + 1))
done
echo
if [ "$moved" -eq 0 ]; then echo "No IR downloads found in Downloads."
else echo "Done: $moved moved. Open Cab Studio in NAM Home and they're in the cab list."; fi
