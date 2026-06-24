#!/usr/bin/env python3
"""
extract.py -- copy your Pokémon Blue party into the vault, non-destructively.

It pretends to be a trade partner so the cartridge streams its whole party over
the link cable. We read that party, save it, and then CANCEL the trade -- the
cartridge keeps every Pokémon.

Usage:
    python host/extract.py                # capture party -> vault/ (Gen 1)
    python host/extract.py --gen 2        # Gold/Silver/Crystal (Gen 2)
    python host/extract.py --gen 3        # Ruby/Sapphire/Emerald/FR/LG
                                          #   (multiboot first: host/gen3_boot.py)
    python host/extract.py --selftest     # just open the board (no Game Boy)
    python host/extract.py --no-sanity    # capture raw bytes without cleaning
    python host/extract.py --out PATH     # vault directory (default: ./vault)

Run scripts/setup.sh first.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from picovault.usb_link import UsbLink  # noqa: E402
from picovault import engine, savedata  # noqa: E402


def selftest():
    print("Self-test: opening the board (no Game Boy needed)...")
    link = UsbLink()
    link.open()
    print("  OK: board opened and data path enabled (VID 0xCAFE / PID 0x4011).")
    link.close()
    print("Self-test passed. Now connect the GBA + Pokémon Blue and run without "
          "--selftest.")


def capture(vault_dir, verbose, sanity, gen):
    print(f"Connecting to the board... (Gen {gen})")
    link = UsbLink()
    link.open()
    try:
        if gen == 3 and not link.configure(4):
            print("Gen 3 needs the reconfigurable firmware (4-byte SIO32 "
                  "transfers).\nFlash third_party/gen3/gbusb_reconfigurable.uf2 "
                  "and multiboot first:\n  python host/gen3_boot.py",
                  file=sys.stderr)
            return 1

        # The engine uses relative paths to its own data files.
        with engine.engine_cwd():
            trader = engine.build_trader(link, verbose=verbose, sanity=sanity, gen=gen)
            engine.prime_capture_session(trader)
            partner = engine.load_base_partner(trader)
            send_data = partner.create_trading_data(trader.special_sections_len)

            if gen == 3:
                print(
                    "\nOn the GBA: Gen3-to-GenX must be running (python "
                    "host/gen3_boot.py)\nwith the Gen 3 cartridge inserted. "
                    "Select its Gen 3 trade option.\n"
                )
                # No Cable Club dance: the multibooted program is the peer.
                # The exchange fills trader.own_pokemon with the cart's party.
                trader.trade_starting_sequence(True, send_data=send_data)
                cart_party = trader.own_pokemon
            else:
                print(
                    "\nIn the game: go to a Pokémon Center -> upstairs -> Cable Club\n"
                    "-> TRADE CENTER, walk to the table and interact. Then select your\n"
                    "first Pokémon when prompted. (Nothing will actually be traded.)\n"
                )

                trader.enter_room()
                if not trader.sit_to_table():
                    print("Didn't reach the trade table (you stood up, or the link "
                          "dropped). Try again.")
                    return 1

                # Buffered, fully-local exchange of the three trade sections.
                data, _data_other = trader.trade_starting_sequence(
                    True, send_data=send_data
                )
                cart_party = trader.party_reader(data[1], data_mail=data[2])

            # Save BEFORE doing anything that could commit a trade.
            written = savedata.save_party(cart_party, vault_dir, engine.ENGINE_DIR, gen=gen)

            # Cancel: signal stop_trade and let the player back out in-game.
            _cancel(trader, gen)

        ext = savedata.record_ext(gen)
        print(f"\nCaptured {len(written)} Pokémon into {vault_dir}:")
        for stem, info in written:
            lvl = info.get("level")
            nick = info.get("nickname") or info.get("species_name")
            print(f"  - {info['species_name']:<12} Lv{lvl}  '{nick}'  -> {stem}{ext}")
        print("\nYour cartridge is unchanged. Press B in-game to leave the table.")
        return 0
    finally:
        link.close()


def _cancel(trader, gen=1):
    """Back out of the trade without committing anything."""
    try:
        if gen == 3:
            # The engine's own "force the trade menu closed" sequence: keep
            # sending cancel until the device acks with stop, then send stop.
            trader.end_trade()
        else:
            for _ in range(64):
                trader.swap_byte(trader.stop_trade)
    except Exception:  # noqa: BLE001 - cancelling is best-effort
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="open the board only; no trade")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "vault"),
                    help="vault output directory (default: ./vault)")
    ap.add_argument("--no-sanity", dest="sanity", action="store_false",
                    help="capture raw bytes without the engine's sanity cleaning")
    ap.add_argument("-q", "--quiet", dest="verbose", action="store_false",
                    help="less engine logging")
    ap.add_argument("--gen", type=int, choices=(1, 2, 3), default=1,
                    help="game generation: 1 = R/B/Y, 2 = G/S/C, "
                         "3 = Ruby/Sapphire/Emerald/FR/LG (multiboot first: "
                         "python host/gen3_boot.py)")
    args = ap.parse_args()

    try:
        if args.selftest:
            selftest()
            return 0
        return capture(os.path.abspath(args.out), args.verbose, args.sanity, args.gen)
    except RuntimeError as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted.")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
