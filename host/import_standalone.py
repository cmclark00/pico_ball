#!/usr/bin/env python3
"""
import_standalone.py -- pull captured Pokémon off the standalone vault board
(Path B firmware) over USB serial and decode them into ./vault.

The firmware stores each capture as raw trade sections
(random=10 | party=418 | patches=197). This sends the 'd' (dump) command, reads
the hex records, applies the patch list, parses the party with the proven
engine, and saves each captured party under vault/standalone_<n>/.

Usage:
    python host/import_standalone.py                 # auto-detect the board
    python host/import_standalone.py --port /dev/ttyACM0
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from picovault import engine, savedata  # noqa: E402

PICO_VID = 0x2E8A  # Raspberry Pi (pico_stdio_usb default)


class _FakeLink:
    def send_byte(self, b, n):
        pass

    def receive_byte(self, n=None):
        return 0


def _find_port():
    import serial.tools.list_ports

    for p in serial.tools.list_ports.comports():
        if p.vid == PICO_VID:
            return p.device
    return None


def _read_dump(port):
    import serial

    with serial.Serial(port, 115200, timeout=2) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b"d")
        ser.flush()
        records = []
        deadline = time.time() + 15
        while time.time() < deadline:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                continue
            if line.startswith("MON "):
                # MON <gen> <species> <len> <hex>
                parts = line.split(" ", 4)
                gen, species = int(parts[1]), int(parts[2])
                records.append((gen, species, bytes.fromhex(parts[4])))
            elif line == "END":
                break
        return records


def _save_mon(traders, gen, rec, vault_dir):
    """rec = a single dex entry's bytes (struct + OT + nickname). Build a 1-mon
    party for its generation and save it (.pk1 + .json)."""
    party = engine.build_inject_party(traders[gen], list(rec))
    return savedata.save_party_member(
        party, 0, vault_dir, engine.ENGINE_DIR, prefix="dex", gen=gen)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--port", help="serial port (default: auto-detect 2E8A:*)")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault"))
    args = ap.parse_args()

    try:
        port = args.port or _find_port()
        if not port:
            print("Standalone board not found. Plug it in (USB) or pass --port.",
                  file=sys.stderr)
            return 2
        print(f"Reading from {port}...")
        records = _read_dump(port)
        if not records:
            print("No dex entries returned. Has the board captured anything yet?")
            return 1

        out_dir = os.path.join(os.path.abspath(args.out), "dex")
        with engine.engine_cwd():
            traders = {}
            for g in (1, 2):
                t = engine.build_trader(_FakeLink(), verbose=False, sanity=True, gen=g)
                engine.prime_capture_session(t)
                traders[g] = t
            for gen, species, rec in records:
                path = _save_mon(traders, gen, rec, out_dir)
                print(f"  Gen {gen} #{species:<3} -> {os.path.basename(path)}")
        print(f"\nSaved {len(records)} dex Pokémon into {out_dir}.")
        return 0
    except RuntimeError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
