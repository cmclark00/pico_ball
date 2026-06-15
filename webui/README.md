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

### Gen 3 (R/S/E/FR/LG) — WebUSB, not Web Serial
Gen 3 can't go through the on-device vault firmware (it needs 4-byte SIO32
transfers and the GBA must run the **Gen3-to-GenX** homebrew, multibooted into
RAM). So selecting **Gen 3** in the dropdown switches the page to a **WebUSB**
path that mirrors `host/extract.py --gen 3` entirely in the browser:

1. Flash the board with the **reconfigurable firmware**
   (`third_party/gen3/gbusb_reconfigurable.uf2`, fetched by `scripts/setup.sh`).
2. Power the GBA **on at the BIOS/boot screen** (no game running), cartridge
   inserted, link cable connected.
3. Pick **Gen 3** and click **Capture now** → the page asks for the USB device,
   configures the SIO32 link, and **multiboots** the homebrew into the GBA.
4. When prompted, on the GBA open Gen3-to-GenX → the Gen 3 trade option and
   advance to the trade screen, then click **OK**. The page runs the trade-partner
   exchange, reads your party, and shows the Pokémon (and `.pk3` downloads).

The page loads the ~248 KB multiboot image (`pokemon_gen3_to_genx_mb.gba`) from
`webui/` (copied there by `scripts/setup.sh`); if missing it falls back to
`third_party/gen3/…` or a file picker.

> **How it works:** mirrors `host/extract.py --gen 3` entirely in the browser —
> `usb_link.js` (WebUSB), `multiboot.js`, then `gen3_trade.js` (a port of the
> capture path of `utilities/rse_sp_trading.py`: the buffered 896-byte section
> exchange). The captured 100-byte structs are the same `.pk3` records the Python
> tool writes, decoded by `pokedata_gen3.js`.

These use simple serial commands (`a` capture, `r<n>` delete, `w` wipe); the
firmware prints machine-readable markers (`CAPTURE_RESULT …`, `DELETED …`,
`WIPED …`) that the page waits on.

## What's in here

| File | Purpose |
|---|---|
| `index.html` | UI + Web Serial transport + rendering |
| `decode.js` | Gen 1 record decoder (shared with the Node test) |
| `pokedata.js` | species + move name tables (generated) |
| `usb_link.js` | WebUSB transport for the Gen 3 reconfigurable firmware |
| `multiboot.js` | GBA multiboot (port of `multiboot.py`) for Gen 3 |
| `gen3_trade.js` | Gen 3 trade-partner capture (port of `rse_sp_trading.py`) |
| `gen3_partner.js` | Baked throwaway partner party (gen `tools/gen_gen3_partner.py`) |
| `gen3.js` | Gen 3 flow: WebUSB → configure(4) → multiboot → capture |

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
- **Web Serial vs WebUSB.** Gens 1/2 use the vault firmware's **CDC (Web Serial)**
  — simplest, and it already does the on-device capture. **Gen 3** uses the
  reconfigurable firmware's vendor interface over **WebUSB** (`usb_link.js`),
  because it needs SIO32 + multiboot. The two paths coexist; the dropdown picks.
