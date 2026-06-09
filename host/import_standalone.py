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
            if line.startswith("REC "):
                _, idx, length, hexdata = line.split(" ", 3)
                records.append(bytes.fromhex(hexdata))
            elif line == "END":
                break
        return records


def _decode_and_save(trader, record, vault_dir):
    from utilities.rby_trading_data_utils import RBYUtils

    if len(record) < 625:
        raise RuntimeError(f"record too short: {len(record)} bytes")
    section1 = list(record[10:428])   # party
    section2 = list(record[428:625])  # patch list
    RBYUtils.apply_patches(section1, section2, RBYUtils)
    party = trader.party_reader(section1)
    return savedata.save_party(party, vault_dir, engine.ENGINE_DIR)


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
            print("No records returned. Has the board captured anything yet?")
            return 1

        with engine.engine_cwd():
            trader = engine.build_trader(_FakeLink(), verbose=False, sanity=True)
            engine.prime_capture_session(trader)
            total = 0
            for i, rec in enumerate(records):
                sub = os.path.join(os.path.abspath(args.out), f"standalone_{i}")
                written = _decode_and_save(trader, rec, sub)
                total += len(written)
                names = ", ".join(info["species_name"] for _, info in written)
                print(f"  record {i}: {len(written)} Pokémon ({names}) -> {sub}")
        print(f"\nDecoded {len(records)} record(s), {total} Pokémon into {args.out}.")
        return 0
    except RuntimeError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
