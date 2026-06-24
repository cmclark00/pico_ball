# pico_ball WebUI — view your Pokémon straight from the device

A single-page app that talks to the standalone vault board over **WebUSB** and
decodes the stored Pokémon **in the browser** — no copying to disk, no Python.
Plug in the board, click Connect, see your captures. Works on **desktop and
Android** Chrome/Edge.

![flow](data:,)  <!-- (no image; see below) -->

## Requirements

- The board running the **Path B firmware** (`pico_ball_vault.uf2`). It enumerates
  as a **WebUSB vendor device** ("pico_ball vault", VID 2E8A) — no CDC serial, so
  it also works on Android, where Chrome won't expose CDC devices to WebUSB.
- **Chrome or Edge** on desktop **or Android** (WebUSB isn't in Firefox/Safari).
- Served from a **secure context** — `http://localhost`, or any `https://` origin
  (the hosted copy at <https://cmclark00.github.io/pico_ball> works on a phone).

## Run it

```bash
cd webui
python3 -m http.server 8000
# open http://localhost:8000 in Chrome/Edge
```

Then:
1. Plug the vault board into USB (on a phone, use a USB-C/OTG cable).
2. Click **Connect device** and pick **"pico_ball vault"** from the WebUSB chooser
   (VID 2E8A).
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

### Gen 3 (R/S/E/FR/LG)
Gen 3 needs 4-byte SIO32 transfers and the GBA running the **Gen3-to-GenX**
homebrew (multibooted into RAM). There are two ways to do it, and the page picks
automatically:

- **Standalone vault firmware (preferred).** If you're connected with **Connect
  device**, picking **Gen 3** + **Capture now** drives the board's on-device `m`
  (multiboot) then `t` (trade) commands over the same WebUSB channel — the board
  does everything, exactly like gens 1/2. You'll be prompted to reach the GBA's
  trade screen between the two steps. Captures persist to the board's flash and
  show in the dex.
- **WebUSB against the reconfigurable firmware.** If you're *not* connected to a
  vault, the page falls back to a path that mirrors `host/extract.py --gen 3`
  entirely in the browser, driving a separate WebUSB device (the reconfigurable
  firmware, VID CAFE):

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

These use simple text commands (`a` capture, `r<n>` delete, `w!` wipe — the `w`
arms and the `!` confirms, so a stray `w` can't erase the vault) over the
WebUSB vendor channel; the firmware prints machine-readable markers
(`CAPTURE_RESULT …`, `DELETED …`, `WIPED …`) that the page waits on.

## What's in here

| File | Purpose |
|---|---|
| `index.html` | UI + WebUSB transport + rendering |
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
- **No data after Connect?** The page sends a `0x22` "connect" control transfer so
  the firmware starts mirroring its output; if you saw the chooser but no cards,
  close any other app/tab holding the device (only one process can claim a USB
  device at a time, including `host/import_standalone.py`).
- **Why WebUSB (not Web Serial)?** The vault firmware presents a single **vendor**
  interface, not CDC, so it works on Android too — Chrome there won't expose a CDC
  device to WebUSB. The same channel carries gens 1/2/3 (and `import_standalone.py`
  uses it as well). The Gen 3 *reconfigurable firmware* is a separate WebUSB device
  (`usb_link.js`), used only for the browser-side Gen 3 fallback path.
