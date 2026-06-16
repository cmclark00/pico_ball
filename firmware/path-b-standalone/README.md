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
| **Tap** (short press) | Capture, using the currently selected generation |
| **Hold ~0.7 s** | Switch generation, cycling **Gen 1 → Gen 2 → Gen 3 → Gen 1** |
| **Hold ~2 s** | Inject dex slot 0 into a cartridge (LED turns amber to confirm) |

**Gen 3 capture is two taps** (it multiboots the GBA, then trades): with Gen 3
selected, **tap once** to multiboot Gen3-to-GenX into the GBA (LED → amber); on
the GBA pick the Gen 3 trade option and reach the trade screen; then **tap again**
to capture (LED green on success). See "Capturing Gen 3" below.

The **idle LED color shows the mode**: dim **white** = Gen 1, dim **purple** =
Gen 2, dim **cyan** = Gen 3. When you hold to switch gen, it confirms with a
pulse: **1× green** = Gen 1, **2× blue** = Gen 2, **3× cyan** = Gen 3. (Over USB
you can also send `1`/`2`/`3`, or use the WebUI's generation dropdown.) Gen 2
records are larger (1036 B vs 625 B) — the host tools tell them apart
automatically.

### Capturing Gen 3 (R/S/E/FR/LG)
Gen 3 can't use the Cable Club — the board **multiboots Lorenzooone's Gen3-to-GenX**
program into the GBA over the link, then acts as the trade peer. No PC runs the
trade engine; the board does it all over the same link cable, just faster (4-byte
SIO32 transfers). Steps:
1. Insert the Gen 3 cartridge; turn the GBA **on at the BIOS/boot screen** (no game).
2. Select Gen 3 (hold to cycle to cyan, or send `3`).
3. **Tap** (or send `m`): the board multiboots Gen3-to-GenX (LED → amber).
4. On the GBA, choose the **Gen 3 trade option** and reach the trade screen.
5. **Tap again** (or send `t`): the board reads your party and stores it. LED
   green. Each Pokémon is a 100-byte `.pk3`, retrievable like any other capture.

The build bakes the ~248 KB multiboot image into flash. It's generated from the
(non-redistributed) homebrew, so run `./scripts/setup.sh` once before building so
`tools/gen_baked_gen3.py` can produce `baked_gen3_mb.c`.

**Capturing Crystal:** hold to switch to Gen 2 (idle LED → purple), sit at the
Trade Center table (the game will say "your friend is not ready" — that's normal
until the board is armed), then tap to capture.
4. In-game, sit at the table and select your first Pokémon. The board streams a
   throwaway partner, records your party, and **never commits** — your cartridge
   keeps everything. LED turns **green** = captured & stored to flash.
5. Press B in-game to leave the table. Capture more anytime; each is a new slot.

### Inject mode (trade a stored Pokémon back into a cartridge)

To inject a Pokémon from the dex back into a cartridge:
1. Make sure the target gen is selected (tap gen-switch if needed).
2. **Hold the button ~2 s** — LED turns **amber** to confirm the gesture fired.
   Release the button.
3. In-game: Cable Club → Trade Center → sit at the table. **Select the Pokémon
   you want to give away** (the board will give you the stored one in return).
4. LED turns **green** = trade committed. The Pokémon you gave away is
   automatically saved back to the dex (it won't be lost).

To choose which stored Pokémon to inject, connect USB and send `i <n>` (slot
number, 0-indexed from the dex list printed by `d`). The last slot set via serial
persists as the target for button presses.

### LED states
| Color | Meaning |
|---|---|
| dim white | idle / ready — **Gen 1** mode |
| dim purple | idle / ready — **Gen 2** mode |
| dim cyan | idle / ready — **Gen 3** mode |
| 1× green / 2× blue / 3× cyan pulse | generation switched to Gen 1 / 2 / 3 (on hold) |
| amber (steady) | Gen 3 multiboot done — pick the trade option, then tap to capture |
| amber (brief flash) | inject threshold crossed — release to arm |
| blue | armed, waiting for / talking to the cartridge |
| green | captured / injected & stored ✅ |
| amber | captured but the vault is full |
| red | failed / no cartridge responded / cancelled |

## Get the captures onto a PC

The firmware also exposes a USB serial console (for debug + retrieval):

```bash
source host/.venv/bin/activate
python host/import_standalone.py        # auto-detects the board, decodes to vault/dex/
```

Or talk to it manually in any serial terminal (115200):

| Command | Effect |
|---|---|
| `c` | print dex count |
| `d` | dump the dex (`MON <gen> <species> <len> <hex>` per entry) |
| `a` | arm and run a Gen 1/2 capture (same as button tap) |
| `i` or `i <n>` | inject dex slot n (default: last slot set; omit n to reuse) |
| `1` / `2` / `3` | switch to Gen 1 / Gen 2 / Gen 3 |
| `m` | Gen 3: multiboot Gen3-to-GenX into the GBA (optional `m <us>` pacing) |
| `t` | Gen 3: trade-capture the party (GBA on its trade screen; optional `t <us>`) |
| `r <n>` | delete dex slot n |
| `w` | wipe the whole vault |

`import_standalone.py` saves each dex entry as a `.pk1` + `.json` in `vault/dex/`
(same format as Path A — so you can also use `inject.py` from a PC). The WebUI
reads the same dump and shows the dex.

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
- **Storage: a living dex, keyed by (generation, species).** On capture, the
  party is split into individual Pokémon and each is **upserted by species** —
  recapturing a species overwrites just that entry, new species are added. So the
  vault accumulates toward a full Pokédex across many captures (e.g. capture
  Cyndaquil at Lv9, level it up and recapture → the Cyndaquil entry updates to
  Lv15; capturing a Totodile adds a new entry). One 128-byte slot per species per
  gen, in a reserved flash region; no LittleFS. (After flashing over an older
  build, send `w` / hit **Wipe all** once to clear old-format data.)

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
