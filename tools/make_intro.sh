#!/usr/bin/env bash
# Records the scripted gameplay intro and encodes docs/intro.mp4 and
# docs/intro.gif. Needs ffmpeg (with libx264) on the PATH.
#
# Usage: tools/make_intro.sh [path/to/soakthebun]
#   defaults to build/Release/soakthebun (Visual Studio) or build/soakthebun
set -euo pipefail

cd "$(dirname "$0")/.."
GAME=${1:-}
if [[ -z "$GAME" ]]; then
  if [[ -x build/Release/soakthebun || -x build/Release/soakthebun.exe ]]; then
    GAME=build/Release/soakthebun
  else
    GAME=build/soakthebun
  fi
fi

W=1280
H=720
FPS=30
OUT=docs
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

echo "Recording gameplay..."
"$GAME" --record - --size "${W}x${H}" --fps "$FPS" |
  ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r "$FPS" -i - \
    -c:v ffv1 "$WORK/intro.mkv"

echo "Encoding $OUT/intro.mp4..."
ffmpeg -y -loglevel error -i "$WORK/intro.mkv" \
  -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart "$OUT/intro.mp4"

echo "Encoding $OUT/intro.gif..."
ffmpeg -y -loglevel error -i "$WORK/intro.mkv" \
  -vf "fps=15,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
  -loop 0 "$OUT/intro.gif"

ls -l "$OUT/intro.mp4" "$OUT/intro.gif"
