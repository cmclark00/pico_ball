#!/usr/bin/env bash
# One-time setup for Path A (PC-tethered extraction).
#   - creates a Python venv in host/.venv
#   - installs pyusb + pyserial
#   - clones the pinned MIT trade engine into third_party/
#   - prints the Linux udev rule for non-root USB access
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Pinned so behavior is reproducible. Bump deliberately.
ENGINE_URL="https://github.com/Lorenzooone/PokemonGB_Online_Trades"
ENGINE_COMMIT="0267d9a161f8275316c8497a1be04039dfa25131"
ENGINE_DIR="third_party/PokemonGB_Online_Trades"

echo "==> Python virtualenv (host/.venv)"
if [ -f host/.venv/bin/activate ]; then
  echo "    already exists — reusing"
else
  python3 -m venv host/.venv
fi
# shellcheck disable=SC1091
source host/.venv/bin/activate
pip install --quiet --upgrade pip
pip install --quiet -r host/requirements.txt
echo "    installed: $(pip list 2>/dev/null | grep -iE 'pyusb|pyserial' | tr '\n' ' ')"

echo "==> Trade engine ($ENGINE_DIR @ ${ENGINE_COMMIT:0:9})"
if [ -f "$ENGINE_DIR/utilities/rby_trading.py" ] && [ ! -d "$ENGINE_DIR/.git" ]; then
  echo "    already present (non-git copy) — leaving as-is"
elif [ -d "$ENGINE_DIR/.git" ]; then
  git -C "$ENGINE_DIR" fetch --quiet origin "$ENGINE_COMMIT" 2>/dev/null || git -C "$ENGINE_DIR" fetch --quiet origin
  git -C "$ENGINE_DIR" checkout --quiet "$ENGINE_COMMIT"
elif [ -e "$ENGINE_DIR" ] && [ -n "$(ls -A "$ENGINE_DIR" 2>/dev/null)" ]; then
  echo "    ERROR: $ENGINE_DIR exists, is not a git repo, and lacks the engine."
  echo "    Remove it and re-run: rm -rf '$ENGINE_DIR'"
  exit 1
else
  git clone --quiet "$ENGINE_URL" "$ENGINE_DIR"
  git -C "$ENGINE_DIR" fetch --quiet origin "$ENGINE_COMMIT" 2>/dev/null || true
  git -C "$ENGINE_DIR" checkout --quiet "$ENGINE_COMMIT"
fi
echo "    ready: $(ls "$ENGINE_DIR/utilities/rby_trading.py" >/dev/null 2>&1 && echo OK || echo MISSING)"

# --- Gen 3 (GBA) support artifacts -------------------------------------------
# Gen 3 games trade through Lorenzooone's Gen3-to-GenX program, multibooted into
# the GBA's RAM over the link cable. The adapter must run the reconfigurable
# firmware (4-byte SIO32 transfers). Both are pinned prebuilt releases, fetched
# here rather than redistributed (third_party/ is gitignored).
GEN3_DIR="third_party/gen3"
GEN3_MB_URL="https://github.com/Lorenzooone/Pokemon-Gen3-to-Gen-X/releases/download/1.1.14/pokemon_gen3_to_genx_mb.zip"
GEN3_FW_URL="https://github.com/Lorenzooone/gb-link-firmware-reconfigurable/releases/download/1.0.2/gbusb.uf2"

echo "==> Gen 3 artifacts ($GEN3_DIR)"
mkdir -p "$GEN3_DIR"
if [ ! -f "$GEN3_DIR/pokemon_gen3_to_genx_mb.gba" ]; then
  curl -fsSL -o "$GEN3_DIR/mb.zip" "$GEN3_MB_URL"
  python3 -c "
import zipfile, sys
with zipfile.ZipFile('$GEN3_DIR/mb.zip') as z:
    names = [n for n in z.namelist() if n.endswith('.gba')]
    assert names, 'no .gba in multiboot zip'
    open('$GEN3_DIR/pokemon_gen3_to_genx_mb.gba', 'wb').write(z.read(names[0]))
"
  rm -f "$GEN3_DIR/mb.zip"
  echo "    fetched pokemon_gen3_to_genx_mb.gba (Gen3-to-GenX 1.1.14)"
else
  echo "    pokemon_gen3_to_genx_mb.gba already present"
fi
if [ ! -f "$GEN3_DIR/gbusb_reconfigurable.uf2" ]; then
  curl -fsSL -o "$GEN3_DIR/gbusb_reconfigurable.uf2" "$GEN3_FW_URL"
  echo "    fetched gbusb_reconfigurable.uf2 (gb-link-firmware-reconfigurable 1.0.2)"
else
  echo "    gbusb_reconfigurable.uf2 already present"
fi

# The WebUI's Gen 3 path multiboots this image via WebUSB, so it must be servable
# from webui/. Copy it there (gitignored, like third_party/ — not redistributed).
cp -f "$GEN3_DIR/pokemon_gen3_to_genx_mb.gba" webui/pokemon_gen3_to_genx_mb.gba
echo "    copied multiboot image to webui/ (for the WebUI Gen 3 path)"

# Bake the image into a C array for the standalone firmware's on-device multiboot
# (also gitignored — generated from the non-redistributed homebrew).
python3 tools/gen_baked_gen3.py >/dev/null && \
  echo "    generated firmware baked_gen3_mb.c (standalone Gen 3 multiboot)"

cat <<'EOF'

==> Done.

Next:
  1. Flash the board (firmware/README.md). Expect `lsusb` to show cafe:4011.

  2. (Linux) install the udev rule so you don't need sudo:

       sudo tee /etc/udev/rules.d/99-gb-link.rules >/dev/null <<'RULE'
       SUBSYSTEM=="usb", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4011", MODE="0666"
       RULE
       sudo udevadm control --reload-rules && sudo udevadm trigger
     (re-plug the board afterward)

  3. Activate the venv and run a self-test (no Game Boy needed):

       source host/.venv/bin/activate
       python host/extract.py --selftest

  4. Connect the board to the GBA, boot Pokémon Blue, go to the Cable Club
     Trade Center, then:

       python host/extract.py
EOF
