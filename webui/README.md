# pico_ball WebUI — view your Pokémon straight from the device

A single-page app that talks to the standalone vault board over the **Web Serial
API** and decodes the stored Gen 1 Pokémon **in the browser** — no copying to
disk, no Python. Plug in the board, click Connect, see your captures.

![flow](data:,)  <!-- (no image; see below) -->

## Requirements

- The board running the **Path B firmware** (`pico_ball_vault.uf2`), which exposes
  the `d` (dump) command over USB serial.
- **Chrome or Edge on desktop** (Web Serial isn't in Firefox/Safari).
- Served from a **secure context** — easiest is `http://localhost`.

## Run it

```bash
cd webui
python3 -m http.server 8000
# open http://localhost:8000 in Chrome/Edge
```

Then:
1. Plug the vault board into USB.
2. Click **Connect device** and pick the serial port (it shows up as a USB
   serial / "Board CDC" device, VID 2E8A).
3. The page sends `d`, reads the stored records, decodes them, and renders a card
   per Pokémon: sprite, species, nickname, level, HP bar, moves (+PP), OT.
4. Capture more on the board anytime, then click **Refresh**.

Nothing is written to your computer — it's read live from the board's flash.

### Rich cards + export
Each card shows the sprite, **types**, level, HP, **in-game stats**, **DVs (IVs)**,
moves (+PP), OT, and **nicknamed / trade-evolves** flags.
- **Download .pk1** (per card) — saves a 66-byte record *byte-identical* to the
  Python tools, so you can `host/inject.py` it into a cartridge.
- **Download .sav** (per capture) — a 32 KB Gen 1 save with your Pokémon in the
  party and a valid checksum. Open it in **PKHeX** (then drag the Pokémon into any
  save) or load it in a **Game Boy emulator**.
- **Download capture JSON** — the whole capture, decoded, for archiving.

### Control the board from the browser
With the latest firmware flashed, the header/​capture controls drive the device:
- **Generation dropdown + Capture now** — pick **Gen 1 (R/B/Y)** or **Gen 2
  (G/S/C)**, then arm a capture (sit at the Trade Center table and pick a
  Pokémon); the page waits and refreshes when done. Captures are decoded for the
  right generation automatically (Gen 2 cards show 6 stats; `.sav` export is
  Gen 1 only).
- **Delete** (per capture) — erases that record's flash slot.
- **Wipe all** — erases the whole vault (with a confirm).

These use simple serial commands (`a` capture, `r<n>` delete, `w` wipe); the
firmware prints machine-readable markers (`CAPTURE_RESULT …`, `DELETED …`,
`WIPED …`) that the page waits on.

## What's in here

| File | Purpose |
|---|---|
| `index.html` | UI + Web Serial transport + rendering |
| `decode.js` | Gen 1 record decoder (shared with the Node test) |
| `pokedata.js` | species + move name tables (generated) |

Regenerate the data tables (only if the engine's name files change):

```bash
python tools/gen_webui_data.py
```

## How the decode is verified

`decode.js` is the *same* code the browser runs, and it's unit-tested against a
real capture (your Path A party) under Node:

```bash
./tools/test_webui.sh
# rebuilds a 625-byte record from vault/*.pk1 and decodes it with decode.js
# -> PASS Pikachu:7,Pidgey:3,Squirtle:13,Metapod:6
```

## Notes / troubleshooting

- **Sprites** come from Pokémon DB's Red/Blue set, by species name. They need
  internet; if offline or a name doesn't map, the card shows a `?` placeholder and
  all the decoded data still renders.
- **No data after Connect?** The firmware only emits once DTR is asserted; the page
  does this automatically (`setSignals`). If your terminal app grabbed the port,
  close it first — only one app can hold the serial port.
- **Want WebUSB instead of Web Serial?** Not needed here — the firmware's CDC is
  simpler and already does the job. (A WebUSB build would require adding a vendor
  interface to the firmware, like the Path A bridge.)
