#!/usr/bin/env bash
# pico_ball launcher — runs the host tools with the project venv so you don't
# have to `source host/.venv/bin/activate` or type long paths.
#
#   ./pb extract [--gen N]      capture a party from the cart into the vault
#   ./pb inject  [record]       trade a vaulted Pokémon back into the cart
#   ./pb list                   show everything in the vault, decoded
#   ./pb export-sav [--gen N]   write a PKHeX/emulator .sav
#   ./pb gen3-boot              multiboot Gen3-to-GenX onto the GBA
#   ./pb import                 pull standalone-firmware captures over USB
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="$HERE/host/.venv/bin/python"
if [ ! -x "$PY" ]; then
    echo "Project venv not found at host/.venv — run ./scripts/setup.sh first." >&2
    exit 1
fi

usage() { sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

cmd="${1:-help}"
shift || true
case "$cmd" in
    extract)         exec "$PY" "$HERE/host/extract.py" "$@" ;;
    inject)          exec "$PY" "$HERE/host/inject.py" "$@" ;;
    list|vault)      exec "$PY" "$HERE/host/vault.py" "$@" ;;
    export-sav|sav)  exec "$PY" "$HERE/host/export_sav.py" "$@" ;;
    gen3-boot|boot)  exec "$PY" "$HERE/host/gen3_boot.py" "$@" ;;
    import)          exec "$PY" "$HERE/host/import_standalone.py" "$@" ;;
    help|-h|--help)  usage ;;
    *) echo "unknown command: $cmd" >&2; echo >&2; usage >&2; exit 1 ;;
esac
