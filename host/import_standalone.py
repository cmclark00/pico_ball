#!/usr/bin/env python3
"""
import_standalone.py -- pull captured Pokémon off the standalone vault board
over WebUSB (its vendor interface) and decode them into ./vault.

The vault firmware is a WebUSB vendor-class device (VID 0x2E8A) with no CDC serial
(Android won't expose CDC to WebUSB, so the WebUI uses the same vendor interface).
This sends the 'd' (dump) command over the vendor bulk endpoints, reads the hex
records, parses each with the proven engine, and saves them under vault/dex/.

Usage:
    python host/import_standalone.py        # auto-detect the board (2E8A vendor)
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from picovault import engine, savedata  # noqa: E402

PICO_VID = 0x2E8A  # Raspberry Pi (the vault firmware's VID)


class _FakeLink:
    def send_byte(self, b, n):
        pass

    def receive_byte(self, n=None):
        return 0


def _open_vault():
    """Find the vault board, claim its WebUSB vendor interface; return
    (dev, ep_out, ep_in) or (None, None, None)."""
    import usb.core
    import usb.util

    dev = usb.core.find(idVendor=PICO_VID)
    if dev is None:
        return None, None, None
    if sys.platform != "win32":
        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
        except Exception:  # noqa: BLE001
            pass
    try:  # macOS/libusb errors if a configuration is already active; only set if not
        dev.get_active_configuration()
    except usb.core.USBError:
        dev.set_configuration()
    itf = next(i for i in dev.get_active_configuration() if i.bInterfaceClass == 0xFF)
    ep_out = usb.util.find_descriptor(
        itf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
        == usb.util.ENDPOINT_OUT)
    ep_in = usb.util.find_descriptor(
        itf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
        == usb.util.ENDPOINT_IN)
    try:  # 0x22 "connect" so the firmware mirrors its console output to us
        dev.ctrl_transfer(0x21, 0x22, 0x0001, itf.bInterfaceNumber, None)
    except Exception:  # noqa: BLE001
        pass
    return dev, ep_out, ep_in


def _read_dump():
    import usb.core
    import usb.util

    dev, ep_out, ep_in = _open_vault()
    if dev is None:
        return None
    try:
        try:
            ep_out.write(b"d")
        except usb.core.USBError as exc:
            if "ccess" in str(exc):  # "Access denied" -> another process holds it
                print("The vault is in use by another program. Close the WebUI tab\n"
                      "(or any browser/app connected to it over WebUSB) and retry.",
                      file=sys.stderr)
                raise SystemExit(2)
            raise
        records, buf = [], ""
        deadline = time.time() + 15
        done = False
        while time.time() < deadline and not done:
            try:
                buf += bytes(ep_in.read(64, timeout=400)).decode(errors="replace")
            except Exception:  # noqa: BLE001 - timeout
                pass
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if line.startswith("MON "):
                    parts = line.split(" ", 4)  # MON <gen> <species> <len> <hex>
                    records.append((int(parts[1]), int(parts[2]), bytes.fromhex(parts[4])))
                elif line == "END":
                    done = True
        return records
    finally:
        usb.util.dispose_resources(dev)


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
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault"))
    args = ap.parse_args()

    try:
        print("Reading the vault over WebUSB (2E8A vendor interface)...")
        records = _read_dump()
        if records is None:
            print("Vault board not found. Plug it into USB (it shows as a WebUSB\n"
                  "vendor device, 2E8A). On Linux you may need the udev rule from\n"
                  "scripts/setup.sh.", file=sys.stderr)
            return 2
        if not records:
            print("No dex entries returned. Has the board captured anything yet?")
            return 1

        out_dir = os.path.join(os.path.abspath(args.out), "dex")
        with engine.engine_cwd():
            traders = {}
            for g in (1, 2, 3):
                t = engine.build_trader(_FakeLink(), verbose=False, sanity=True, gen=g)
                engine.prime_capture_session(t)
                traders[g] = t
            for gen, species, rec in records:
                try:
                    path = _save_mon(traders, gen, rec, out_dir)
                    print(f"  Gen {gen} #{species:<3} -> {os.path.basename(path)}")
                except Exception as exc:  # noqa: BLE001
                    print(f"  Gen {gen} #{species:<3} -> skipped ({exc})")
        print(f"\nSaved {len(records)} dex Pokémon into {out_dir}.")
        return 0
    except RuntimeError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
