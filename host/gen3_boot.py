#!/usr/bin/env python3
"""
gen3_boot.py -- multiboot the Gen3-to-GenX program into a GBA over the link cable.

Gen 3 games (Ruby/Sapphire/Emerald/FireRed/LeafGreen) can't be driven through
the Cable Club like gens 1/2: instead, Lorenzooone's Gen3-to-GenX program is
uploaded into the GBA's RAM (multiboot) and *it* talks to the cartridge and to
us. This script performs that upload. Requirements:

  * the board must run the RECONFIGURABLE firmware
    (third_party/gen3/gbusb_reconfigurable.uf2, fetched by scripts/setup.sh) --
    the stock gb-link firmware can't do the GBA's 4-byte SIO32 transfers;
  * the GBA must be powered ON with NO game booted: turn it on and leave it on
    the boot screen (or hold Start+Select during boot to stay in the BIOS) so
    the BIOS is listening for multiboot on the link port. The Gen 3 cartridge
    can be in the slot the whole time -- Gen3-to-GenX reads it after boot.

After the upload, use the program's menu on the GBA to reach the Gen 3 trade
screen, then run extract.py / inject.py with --gen 3.

Usage:
    python host/gen3_boot.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from picovault import engine  # noqa: E402
from picovault.usb_link import UsbLink  # noqa: E402


def main():
    if not os.path.isfile(engine.GEN3_MULTIBOOT_GBA):
        print(f"Multiboot image not found: {engine.GEN3_MULTIBOOT_GBA}\n"
              "Run ./scripts/setup.sh to fetch it.", file=sys.stderr)
        return 2

    link = UsbLink()
    link.open()
    try:
        if not link.configure(4):
            print(
                "The board's firmware did not acknowledge the config packet.\n"
                "Gen 3 needs the reconfigurable firmware. Flash\n"
                "  third_party/gen3/gbusb_reconfigurable.uf2\n"
                "(hold BOOTSEL while plugging in, copy the .uf2 over).",
                file=sys.stderr)
            return 1

        print("Reconfigurable firmware OK (4-byte SIO32 mode).")
        print("Make sure the GBA is on and sitting at the BIOS/boot screen "
              "(no game running), with the link cable connected.")
        input("Press Enter to start the multiboot upload...")

        with engine.engine_cwd():
            import multiboot
            multiboot.multiboot(
                link.receive_byte_raw,   # raw receiver
                link.send_byte,          # sender(value, num_bytes)
                link.send_list,          # list_sender(data, chunk_size=)
                engine.GEN3_MULTIBOOT_GBA,
            )
        print("\nIf the handshake succeeded, Gen3-to-GenX is now running on "
              "the GBA.\nNavigate its menu to the Gen 3 trade option, then run:"
              "\n  python host/extract.py --gen 3")
        return 0
    finally:
        link.close()


if __name__ == "__main__":
    raise SystemExit(main())
