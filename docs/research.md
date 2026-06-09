# Research notes (everything the design is grounded in)

## 1. The board: `weimanc/game-boy-zero-link-board`

- An **open‑source USB ↔ Game Boy Link‑Cable adapter** built around the
  **RP2040‑Zero** microcontroller.
- Uses a **bidirectional logic‑level converter** (SparkFun BOB‑12009 / clone) to
  translate between the Game Boy's link voltage and the RP2040's 3.3 V logic. It
  powers the converter's HV side from **GBVCC (pin 1 of the link cable)**, which
  auto‑adapts to DMG (5 V) vs GBC/GBA (3.3 V) levels.
- Connectors: GBA/GBC PCB edge connector (J1), DMG (J2), and a combined footprint
  (J3). Repo also ships **KiCad sources** and **ready‑to‑order gerbers**.
- The README explicitly lists it as compatible with existing firmware/tools:
  `stacksmashing/gb-link-firmware`, the Game Boy Printer emulators, and
  **`PokemonGB_Online_Trades`** — i.e. the exact pieces this project uses.

Source: <https://github.com/weimanc/game-boy-zero-link-board>,
<https://hackaday.io/project/195920-game-boy-zero-link>

## 2. The firmware: `stacksmashing/gb-link-firmware`

Read directly from source (`main.c`, `usb_descriptors.c`, `pio/spi.pio`):

- USB device, **VID `0xCAFE`**, **PID `0x4011`** (TinyUSB CDC+VENDOR auto‑PID).
- Exposes a **WebUSB/vendor interface (interface #2, endpoints OUT `0x03` / IN
  `0x83`)**. CDC is present but its task has a copy‑paste bug, so the **vendor
  interface is the real data path**.
- Host enables the data path with a vendor control transfer
  `bmRequestType=0x41, bRequest=0x22, wValue=1, wIndex=2` (it reuses the CDC
  "set control line state" trick). After that:
  **write 1 byte (bulk OUT) → firmware clocks the link → read 1 byte (bulk IN).**
- The RP2040 acts as **SPI master** via PIO (`pio_spi_init(..., PIN_SCK=0,
  PIN_SOUT=2, PIN_SIN=1)`), clocking at ~4 kHz. So **the host drives the pace** —
  ideal for getting Path A correct in software without per‑byte timing battles.

This is exactly the transport `PokemonGB_Online_Trades/usb_trading.py` expects:
it finds `VID 0xcafe / PID 0x4011`, opens interface `(2,0)`, sends the same
`ctrl_transfer(bmRequestType=1, bRequest=0x22, wIndex=2, wValue=0x01)`, then does
1‑byte OUT / 1‑byte IN. **The board + this firmware + that engine already fit
together.**

Source: <https://github.com/stacksmashing/gb-link-firmware>

## 3. Pokémon Gen 1 (RBY) link‑trade protocol

The trade is a documented state machine. High level (see
[gen1-trade-protocol.md](gen1-trade-protocol.md) for the byte‑level version):

1. **Handshake / enter room.** Magic bytes select master/slave and which Cable
   Club room: `0x60` (in‑room filler), `0xD0` = Colosseum (battle), `0xD4` =
   Trade Center. Our device answers to land in the Trade Center.
2. **Random seed** section (RNG sync), framed by `0xFD` preamble bytes.
3. **Trade block** — **415 bytes** containing the player's **entire party** (party
   count, species list, then each Pokémon's 44‑byte structure, OT names,
   nicknames). Both sides send theirs.
4. **Patch lists** — two 190‑byte lists marking where `0xFE` bytes were swapped to
   avoid the `0xFE`/`0xFD` control bytes inside the data; terminated by `0xFF`.
5. **Selection** — choosing a Pokémon sends `0x60 + party_index`. Confirm/decline,
   then the swap commits.

**Why copy‑without‑loss works:** step 3 transmits the whole party *before* any
swap is committed in steps 4–5. Read the block, save it, then back out at the
selection/confirmation step → the cartridge is untouched.

Generation I Pokémon structure is **44 bytes** (`0x2C`); a trade block's three
sections have lengths `[0xA, 0x1A2, 0xC5]` in the engine (random, party,
patches). Confirmed against the engine's `RBYTrading` constants.

Sources:
- Adan Scotney's trade spec (referenced by every implementation).
- Project Pokémon forum, "Generation 1 Link Protocol".
- GBPlay blog, "Emulating a Pokémon Trade with Generated Link Cable Data":
  <https://blog.gbplay.io/2021/05/11/Emulating-a-Pokemon-Trade-with-Generated-Link-Cable-Data.html>
- nitwhiz, "Spoofing A Pokemon Trade": <https://blog.nitwhiz.dev/posts/002-pokemon-red-trade/>
- Bulbapedia, "Pokémon data structure (Generation I)".

## 4. Reference implementations we lean on

- **`Lorenzooone/PokemonGB_Online_Trades` (MIT, pinned `0267d9a`).** A mature,
  sanity‑checked engine that already trades RBY/GSC/RSE over this exact USB
  adapter. We reuse it for Path A rather than re‑derive a byte‑perfect FSM.
  - `usb_trading.py` — USB transport (matches our firmware exactly).
  - `utilities/rby_trading.py` — Gen 1 FSM (`enter_room`, `trade_starting_sequence`).
  - `utilities/rby_trading_data_utils.py` — Gen 1 party/Pokémon parsing.
  - `utilities/high_level_listener.py` — the `connection.hll` interface we stub
    out for local, no‑server operation.
  - Ships `useful_data/rby/` (names, stats, base party `base.bin`, checks maps).
- **`kbembedded/Flipper-Zero-Game-Boy-Pokemon-Trading` (MIT).** A *standalone*
  device that trades with a real Game Boy with no PC and keeps the received
  Pokémon in memory — the existence proof and architecture reference for Path B.
  Uses the **slave** approach (Game Boy supplies the 8 kHz clock; device responds
  within ~120 µs), which is the robust model for on‑device timing.

## 5. GBA + Pokémon Blue specifics

- The original GBA / GBA SP plays Game Boy and Game Boy Color carts via the cart
  slot, running the CPU in GB‑compatibility mode. In that mode the serial link is
  the classic GB link, so the trade protocol above applies unchanged.
- Use a real GB/GBA link cable into the board's **GBC/GBA edge connector**.
- The GBA's link connector is the small 6‑pin one; the signals we use are
  GND, SCK (clock), SI (serial in), SO (serial out), and GBVCC for level
  detection — the board handles the mapping.

## 6. Decisions that fell out of the research

- **Path A transport:** vendor/WebUSB via `pyusb` (not CDC), matching the firmware.
- **Path A FSM:** reuse the upstream engine; write only a thin *local null
  connection* + *vault save* layer. Less new code = fewer byte‑level bugs.
- **Copy is non‑destructive:** read trade block, save, cancel.
- **Path B clocking:** **slave** (like the Flipper), not master — the cartridge
  paces us, so we can't overrun it; PIO does the bit timing in hardware.
