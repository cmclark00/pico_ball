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


def _pick_record(path_arg, vault_dir):
    if path_arg:
        if not os.path.isfile(path_arg):
            raise RuntimeError(f"No such file: {path_arg}")
        return path_arg
    pk1s = sorted(glob.glob(os.path.join(vault_dir, "*.pk1")))
    if not pk1s:
        raise RuntimeError(
            f"No .pk1 records in {vault_dir}. Capture some first with extract.py."
        )
    print("Vaulted Pokémon:")
    for i, p in enumerate(pk1s):
        print(f"  [{i}] {os.path.basename(p)}")
    sel = input("Inject which number? ").strip()
    if not sel.isdigit() or int(sel) >= len(pk1s):
        raise RuntimeError("Invalid selection.")
    return pk1s[int(sel)]


def inject(record_path, vault_dir, verbose, sanity, gen):
    with open(record_path, "rb") as fh:
        record = fh.read()

    print(f"Injecting {os.path.basename(record_path)} (Gen {gen})...")
    link = UsbLink()
    link.open()
    try:
        with engine.engine_cwd():
            trader = engine.build_trader(link, verbose=verbose, sanity=sanity, gen=gen)
            engine.prime_capture_session(trader)
            party = engine.build_inject_party(trader, record)
            send_data = party.create_trading_data(trader.special_sections_len)

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
    ap.add_argument("record", nargs="?", help="path to a .pk1 in the vault")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault"),
                    help="vault dir for the given-up Pokémon (default: ./vault)")
    ap.add_argument("--no-sanity", dest="sanity", action="store_false",
                    help="skip the engine's sanity cleaning of the injected mon")
    ap.add_argument("-q", "--quiet", dest="verbose", action="store_false")
    ap.add_argument("--gen", type=int, choices=(1, 2), default=1,
                    help="game generation: 1 = R/B/Y, 2 = G/S/C")
    args = ap.parse_args()

    try:
        vault_dir = os.path.abspath(args.out)
        record = _pick_record(args.record, vault_dir)
        return inject(record, vault_dir, args.verbose, args.sanity, args.gen)
    except RuntimeError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted.")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
