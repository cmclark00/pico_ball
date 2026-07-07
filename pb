#!/usr/bin/env bash
# pico_ball launcher — runs the host tools with the project venv so you don't
# have to `source host/.venv/bin/activate` or type long paths.
#
#   ./pb setup                  one-time setup (venv, trade engine, Gen 3 bits)
#   ./pb extract [--gen N]      capture a party from the cart into the vault
#   ./pb inject  [record]       trade a vaulted Pokémon back into the cart
#   ./pb list                   show everything in the vault, decoded
#   ./pb export-sav [--gen N]   write a PKHeX/emulator .sav
#   ./pb gen3-boot              multiboot Gen3-to-GenX onto the GBA
#   ./pb import                 pull standalone-firmware captures over USB
#   ./pb webui [port]           serve the WebUI (default http://localhost:8000)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="$HERE/host/.venv/bin/python"

usage() { sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

# Run a host tool inside the project venv (created by ./pb setup).
run_py() {
    if [ ! -x "$PY" ]; then
        echo "Project venv not found at host/.venv — run ./pb setup first." >&2
        exit 1
    fi
    exec "$PY" "$@"
}

cmd="${1:-help}"
shift || true
case "$cmd" in
    setup)           exec bash "$HERE/scripts/setup.sh" "$@" ;;
    extract)         run_py "$HERE/host/extract.py" "$@" ;;
    inject)          run_py "$HERE/host/inject.py" "$@" ;;
    list|vault)      run_py "$HERE/host/vault.py" "$@" ;;
    export-sav|sav)  run_py "$HERE/host/export_sav.py" "$@" ;;
    gen3-boot|boot)  run_py "$HERE/host/gen3_boot.py" "$@" ;;
    import)          run_py "$HERE/host/import_standalone.py" "$@" ;;
    webui)           exec python3 -m http.server "${1:-8000}" --directory "$HERE/webui" ;;
    help|-h|--help)  usage ;;
    *) echo "unknown command: $cmd" >&2; echo >&2; usage >&2; exit 1 ;;
esac
