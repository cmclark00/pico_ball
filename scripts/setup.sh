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
