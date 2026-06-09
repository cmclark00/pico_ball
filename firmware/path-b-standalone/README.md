# Path B — standalone vault firmware (build, flash, use)

No PC needed during a capture: arm the board with the button, sit at the Cable
Club Trade Center, and your party is copied into the board's own flash. The LED
reports state. Later, plug into USB and pull the captures off with
`host/import_standalone.py`.

This is the **as-built** firmware. It revises the original
[DESIGN.md](DESIGN.md) in one important way (see "Design choices" below).

## Flash it

A prebuilt UF2 is at `firmware/prebuilt/pico_ball_vault.uf2`.

```bash
# Hold BOOTSEL, plug in USB -> RPI-RP2 mounts
cp firmware/prebuilt/pico_ball_vault.uf2 /media/$USER/RPI-RP2/
```

> This **replaces** the Path A bridge firmware on the board. To go back to
> PC-tethered extraction, just reflash `gb-link-firmware.uf2`.

## Use it (standalone)

1. Connect the board to the GBA link port (same cabling as Path A). USB power
   only — no computer required (a USB power bank works).
2. Boot Pokémon Blue → Pokémon Center → Cable Club → **Trade Center**, walk to
   the table.
3. **Tap the button** on the board (BOOTSEL) to capture. LED turns **blue** (armed).

### Button gestures (fully standalone — no PC needed)
| Gesture | Action |
|---|---|
| **Single tap** | Capture, using the currently selected generation |
| **Double tap** | Switch generation: Gen 1 (R/B/Y) ⇄ Gen 2 (G/S/C) |

The **idle LED color shows the mode**: dim **white** = Gen 1, dim **purple** =
Gen 2. After a double tap it confirms with a pulse: **1× green** = Gen 1,
**2× blue** = Gen 2. (Over USB you can also send `1`/`2`, or use the WebUI's
generation dropdown.) Gen 2 records are larger (1036 B vs 625 B) — the host tools
tell them apart automatically.
4. In-game, sit at the table and select your first Pokémon. The board streams a
   throwaway partner, records your party, and **never commits** — your cartridge
   keeps everything. LED turns **green** = captured & stored to flash.
5. Press B in-game to leave the table. Capture more anytime; each is a new slot.

### LED states
| Color | Meaning |
|---|---|
| dim white | idle / ready — **Gen 1** mode |
| dim purple | idle / ready — **Gen 2** mode |
| 1× green / 2× blue pulse | generation switched to Gen 1 / Gen 2 |
| blue | armed, waiting for / talking to the cartridge |
| green | captured & stored ✅ |
| amber | captured but the vault is full |
| red | failed / no cartridge responded |

## Get the captures onto a PC

The firmware also exposes a USB serial console (for debug + retrieval):

```bash
source host/.venv/bin/activate
python host/import_standalone.py        # auto-detects the board, decodes to vault/
```

Or talk to it manually in any serial terminal (115200): send `c` for the stored
count, `d` to dump all records as hex. `import_standalone.py` parses that dump,
applies the patch list, and decodes each party into `vault/standalone_<n>/`
(same `.pk1` + `.json` format as Path A — so you can then `inject.py` them back
into a cartridge).

## Build from source

```bash
export PICO_SDK_PATH=~/pico-sdk     # same SDK used for Path A
# regenerate the baked partner party (only needed if base.bin changes):
python tools/gen_baked_party.py
cd firmware/path-b-standalone && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j
# -> pico_ball_vault.uf2
```

## Design choices (as built vs DESIGN.md)

- **Clocking: SPI master, not slave.** The original design proposed a PIO SPI
  *slave* (like the Flipper). But Path A proved the RP2040-as-**master** +
  cartridge-as-slave works on this exact board, and master lets us reuse the
  proven PIO program and the exact engine FSM timing. It also means **single
  core, no 120 µs response window** — much lower risk. We pace bytes at ~20 ms
  (the proven Path A value; tune `GB_LINK_PACING_US`).
- **Capture-and-store raw, decode later.** The vault's job is to *faithfully
  capture*, not to decode on-device (no screen needs names). We store the raw
  trade-block bytes and decode/inject on a PC with the existing host code. This
  removes a huge amount of on-device parsing/patch logic.
- **Fixed baked partner.** Our outgoing party is constant, so it's precomputed
  by the engine into `baked_party.h`; the FSM just replays it. Empty fillers and
  zero trailing drop-bytes (English RBY) make the buffered exchange exact.
- **Storage:** one record per 4 KB flash sector in a reserved 128 KB region
  (32 captures), instead of LittleFS — simpler and dependency-free.

## Incremental bring-up (recommended before trusting a real trade)

The link layer + flashing are reused/proven, but the on-device FSM has not yet
run on hardware. Validate in stages using the USB console:

1. **LED + button:** flash it; confirm idle white, and that pressing the button
   turns it blue then (after timeout with no game) red. Confirms UI + button.
2. **Serial:** open the console; `c` prints the count (0). Confirms USB + flash
   read.
3. **First capture:** do a real capture at the Trade Center. Watch the console —
   it logs each step. Green = success.
4. **Retrieve:** `python host/import_standalone.py` and confirm the decoded
   party matches what `extract.py` got in Path A.
5. **`d` dump sanity:** the raw record should be 625 bytes; section 1 (party)
   should decode to your real Pokémon.

If a capture fails (red / console "code N"), the result code tells you where:
`1` = never reached the table (arming/cabling/timing), `3` = a section failed to
sync (pacing — try a larger `GB_LINK_PACING_US`). Capture the console log and we
tune from there.
