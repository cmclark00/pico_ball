#!/usr/bin/env bash
# Build the Poke Transporter GB multiboot ROM from source into
# third_party/ptgb/poke_transporter_gb_mb.gba, for the standalone Gen 1/2 -> Gen 3
# transfer (the board multiboots PTGB; tools/gen_baked_ptgb.py bakes it into the
# firmware). PTGB is MIT (GearsProgress / Striaton-Lab-Team).
#
# Requires devkitARM (GBA toolchain). If you don't have it, just flash the prebuilt
# firmware/prebuilt/pico_ball_vault.uf2 — PTGB is already baked into that.
#
# Why a custom build instead of a release download: the latest releases only ship
# the *cartridge* ROM (for flashcarts), not the *multiboot* (_mb) build the board
# needs.
#
# Caveat: PTGB's build downloads a live Google Sheet for its text tables, so this
# needs network and is only reproducible while that sheet matches the pinned
# commit's parser (it does at PTGB_COMMIT below; older release *tags* fail because
# the sheet has since drifted). If it ever breaks, just flash the prebuilt
# firmware/prebuilt/pico_ball_vault.uf2 — PTGB is already baked into it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_ROOT/third_party/ptgb"
OUT="$OUT_DIR/poke_transporter_gb_mb.gba"

# Pinned for reproducibility. Bump deliberately. (PTGB's PCCS pin == the host
# converter's pin in scripts/setup.sh, so both sides convert identically.)
PTGB_URL="https://github.com/GearsProgress/Poke_Transporter_GB.git"
PTGB_COMMIT="34732ca884cd0b400932a6552f042e5cf6109437"
PCCS_URL="https://github.com/GearsProgress/Pokemon-Community-Conversion-Standard.git"
PCCS_COMMIT="db430d84c27bf196ab61e07d4b4588ddc093da08"

: "${DEVKITARM:=/opt/devkitpro/devkitARM}"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITARM DEVKITPRO
if [ ! -x "$DEVKITARM/bin/arm-none-eabi-gcc" ] && ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "devkitARM not found (set DEVKITARM). Skipping PTGB build — the prebuilt" >&2
  echo "firmware already has PTGB baked in." >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SRC="$WORK/ptgb"

echo "==> Cloning Poke Transporter GB @ ${PTGB_COMMIT:0:9}"
git clone --quiet "$PTGB_URL" "$SRC"
git -C "$SRC" checkout --quiet "$PTGB_COMMIT"
# Submodule URL is SSH-only; fetch the matching PCCS over HTTPS instead.
rm -rf "$SRC/PCCS"
git clone --quiet "$PCCS_URL" "$SRC/PCCS"
git -C "$SRC/PCCS" checkout --quiet "$PCCS_COMMIT"

echo "==> Python deps for the text/table generators (isolated venv)"
PYVENV="$WORK/venv"
python3 -m venv "$PYVENV"
"$PYVENV/bin/pip" install --quiet --upgrade pip
"$PYVENV/bin/pip" install --quiet pandas openpyxl requests

echo "==> Building (devkitARM)"
# Two-pass: the first pass generates source/translated_text.cpp (and the data
# tables), but Make already expanded the source wildcard before it existed, so a
# second pass is needed to actually compile + link the generated source.
( cd "$SRC" && PATH="$PYVENV/bin:$PATH" make generate_data && PATH="$PYVENV/bin:$PATH" make )

MB="$(ls "$SRC"/*_mb.gba 2>/dev/null | head -1)"
if [ -z "$MB" ] || [ ! -f "$MB" ]; then
  echo "Build finished but no *_mb.gba was produced." >&2
  exit 1
fi
mkdir -p "$OUT_DIR"
cp "$MB" "$OUT"
echo "    built $OUT ($(( $(wc -c < "$OUT") / 1024 )) KB multiboot ROM)"
