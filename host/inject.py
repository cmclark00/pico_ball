#!/usr/bin/env python3
"""
inject.py -- trade a vaulted Pokémon back INTO a Pokémon Blue cartridge.

A trade is a swap: the cartridge receives your vaulted Pokémon and gives up
whichever Pokémon you select on the Game Boy. That given-up Pokémon is NOT lost
-- we capture it into the vault automatically, so you can inject it back later.

Usage:
    python host/inject.py                      # list vault, pick interactively
    python host/inject.py vault/party02_Squirtle.pk1
    python host/inject.py --gen 2 ...          # Gold/Silver/Crystal
    python host/inject.py --out vault          # where to save the given-up mon

Flow in-game: Pokémon Center -> upstairs -> Cable Club -> TRADE CENTER -> table,
then pick the Pokémon you want to give away when prompted.
"""
import argparse
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from picovault.usb_link import UsbLink  # noqa: E402
from picovault import engine, savedata  # noqa: E402


def _pick_record(path_arg, vault_dir, gen):
    if path_arg:
        if not os.path.isfile(path_arg):
            raise RuntimeError(f"No such file: {path_arg}")
        return path_arg
    ext = "*.pk3" if gen == 3 else "*.pk1"
    recs = sorted(glob.glob(os.path.join(vault_dir, ext)))
    if not recs:
        raise RuntimeError(
            f"No {ext} records in {vault_dir}. Capture some first with extract.py."
        )
    print("Vaulted Pokémon:")
    for i, p in enumerate(recs):
        print(f"  [{i}] {os.path.basename(p)}")
    sel = input("Inject which number? ").strip()
    if not sel.isdigit() or int(sel) >= len(recs):
        raise RuntimeError("Invalid selection.")
    return recs[int(sel)]


def _load_record(record_path, gen):
    """Read the record bytes. For a Gen 3 .pk3, prefer the sibling JSON's
    raw_record_hex (the lossless 149-byte engine record, including mail)."""
    if gen == 3:
        json_path = os.path.splitext(record_path)[0] + ".json"
        if os.path.isfile(json_path):
            import json
            try:
                hexrec = json.load(open(json_path)).get("raw_record_hex")
                if hexrec:
                    return bytes.fromhex(hexrec)
            except (ValueError, OSError):
                pass
    with open(record_path, "rb") as fh:
        return fh.read()


def inject(record_path, vault_dir, verbose, sanity, gen):
    record = _load_record(record_path, gen)

    print(f"Injecting {os.path.basename(record_path)} (Gen {gen})...")
    link = UsbLink()
    link.open()
    try:
        if gen == 3 and not link.configure(4):
            print("Gen 3 needs the reconfigurable firmware (4-byte SIO32 "
                  "transfers).\nFlash third_party/gen3/gbusb_reconfigurable.uf2 "
                  "and multiboot first:\n  python host/gen3_boot.py",
                  file=sys.stderr)
            return 1

        with engine.engine_cwd():
            trader = engine.build_trader(link, verbose=verbose, sanity=sanity, gen=gen)
            engine.prime_capture_session(trader)
            party = engine.build_inject_party(trader, record)
            send_data = party.create_trading_data(trader.special_sections_len)

            if gen == 3:
                print(
                    "\nOn the GBA: Gen3-to-GenX must be at its Gen 3 trade screen.\n"
                    "When asked, select the Pokémon you want to GIVE AWAY (you'll\n"
                    "get the vaulted one; the one you give up is saved too).\n"
                )
                trader.trade_starting_sequence(True, send_data=send_data)
                cart_party = trader.own_pokemon
            else:
                print(
                    "\nIn the game: Cable Club -> TRADE CENTER -> table. When asked,\n"
                    "select the Pokémon you want to GIVE AWAY (you'll get the vaulted\n"
                    "one in its place; the one you give up is saved to the vault too).\n"
                )

                trader.enter_room()
                if not trader.sit_to_table():
                    print("Didn't reach the trade table. Try again.")
                    return 1

                data, _ = trader.trade_starting_sequence(True, send_data=send_data)
                cart_party = trader.party_reader(data[1], data_mail=data[2])

            committed, given_idx = engine.local_inject_commit(trader)

        if not committed:
            print("Trade was cancelled/declined in-game. Nothing changed.")
            return 1

        print("\nTrade committed! The cartridge now has your vaulted Pokémon.")
        if given_idx is not None and 0 <= given_idx < cart_party.get_party_size():
            saved = savedata.save_party_member(
                cart_party, given_idx, vault_dir, engine.ENGINE_DIR,
                prefix="received", gen=gen
            )
            print(f"Saved the Pokémon the cartridge gave up -> {saved}")
        return 0
    finally:
        link.close()


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("record", nargs="?",
                    help="path to a .pk1/.pk3 record in the vault")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault"),
                    help="vault dir for the given-up Pokémon (default: ./vault)")
    ap.add_argument("--no-sanity", dest="sanity", action="store_false",
                    help="skip the engine's sanity cleaning of the injected mon")
    ap.add_argument("-q", "--quiet", dest="verbose", action="store_false")
    ap.add_argument("--gen", type=int, choices=(1, 2, 3), default=1,
                    help="game generation: 1 = R/B/Y, 2 = G/S/C, 3 = R/S/E/FR/LG")
    args = ap.parse_args()

    try:
        vault_dir = os.path.abspath(args.out)
        record = _pick_record(args.record, vault_dir, args.gen)
        return inject(record, vault_dir, args.verbose, args.sanity, args.gen)
    except RuntimeError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted.")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
