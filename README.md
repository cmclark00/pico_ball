# pico_ball — carry your Pokémon out of a real cartridge

Turn a [`weimanc/game-boy-zero-link-board`](https://github.com/weimanc/game-boy-zero-link-board)
(an RP2040‑Zero USB↔Game Boy Link‑Cable adapter) into a tool that talks to your
**physical Pokémon Blue** cartridge over the link port and **copies a Pokémon off
it into a file you can carry around** — without losing it from the cartridge.

> **Status:** Path A (PC‑tethered extract + inject) is **working on real
> hardware** — both directions, non‑destructive. Path B (fully standalone, no‑PC
> pocket vault) is **built and compiling** (`firmware/prebuilt/pico_ball_vault.uf2`);
> its on‑device trade FSM awaits a hardware bring‑up pass — see
> [firmware/path-b-standalone/README.md](firmware/path-b-standalone/README.md).

## The one important mental‑model correction

Your board is **not** where the Pokémon "lives." It is a *bridge*: it speaks the
Game Boy link‑cable serial protocol on one side and USB on the other. The board
pretends to be a second Game Boy so your cartridge will trade with it. Where the
Pokémon actually gets **stored** is a software choice:

- **Path A — tethered:** the storage is a file on your laptop (`vault/`). You carry
  the laptop/phone. Proven, low‑risk, working today.
- **Path B — standalone:** the storage is the RP2040's own on‑board flash. Press a
  button, it does the trade by itself, the Pokémon lives on the board. You carry
  the board. This is the real dream — and it has been proven possible on similar
  hardware (the Flipper Zero Pokémon‑trading app does exactly this).

## How a Game Boy trade actually works (the part that makes this possible)

- Pokémon Blue is a Game Boy (Gen 1) game. A **GBA plays it in GB‑compatibility
  mode** and the link port behaves as the classic GB serial link, so your board's
  GBC/GBA edge connector works for it.
- A trade is a well‑documented **state machine** (Adan Scotney's spec): handshake →
  random‑seed exchange → two patch lists → a 415‑byte "trade block" containing your
  whole party → pick a Pokémon (`0x60 + index`) → swap.
- **Key trick for *copying without losing*:** the cartridge sends its *entire party*
  (the trade block) *before* either side confirms the swap. We read that block,
  save every Pokémon in it, and then **cancel** the trade. The cartridge keeps
  everything; we keep a copy. Non‑destructive.

## Quick start (Path A)

```bash
# 1. One-time setup: Python deps + the proven trade engine we build on
./scripts/setup.sh

# 2. Flash the board (see firmware/README.md) — stacksmashing gb-link-firmware

# 3. Plug board -> laptop (USB) and board -> GBA link port. Boot Pokémon Blue,
#    go to a Pokémon Center -> Cable Club -> Trade Center, sit at the table.

# 4. Copy your party into the vault (non-destructive):
source host/.venv/bin/activate
python host/extract.py          # saves vault/*.pk1 + vault/*.json, then cancels
python host/extract.py --gen 2  #   ...or Gold/Silver/Crystal (Gen 2)

# 5. (Optional) Trade a vaulted Pokémon back INTO a cartridge:
python host/inject.py           # pick a vault mon; the cart's given-up mon is
                                # auto-saved to the vault, so nothing is lost

# 6. (Optional) Export your captures as a Gen 1 .sav (opens in PKHeX/emulators):
python host/export_sav.py       # -> vault/pico_ball.sav
```

See **[docs/PLAN.md](docs/PLAN.md)** for the full phased plan, and
**[docs/research.md](docs/research.md)** for everything the design is based on
(with sources).

## View Pokémon straight from the device (WebUI)

With the standalone (Path B) firmware flashed, you don't need to copy anything to
disk to look at your captures:

```bash
cd webui && python3 -m http.server 8000     # then open http://localhost:8000 in Chrome/Edge
```

Click **Connect device**, pick the board's serial port, and the page reads the
stored records over the Web Serial API and decodes the Gen 1 Pokémon **in the
browser** — sprite, level, HP, moves, OT. See
[webui/README.md](webui/README.md).

## Repository layout

```
docs/                      Plan, research, protocol, hardware + flashing guide
firmware/                  Which firmware to flash (Path A) + Path B design
host/                      Path A: the PC-side extractor (Python)
  picovault/               Our code: local no-server connection, save logic
  extract.py               Main entrypoint: copy party -> vault -> cancel
scripts/setup.sh           Clones the pinned trade engine + sets up a venv
vault/                     Where copied Pokémon land (.pk1 raw + .json decoded)
third_party/               (created by setup.sh) the MIT trade engine we reuse
```

## Credits / things we build on

- [`weimanc/game-boy-zero-link-board`](https://github.com/weimanc/game-boy-zero-link-board) — the hardware (MIT).
- [`stacksmashing/gb-link-firmware`](https://github.com/stacksmashing/gb-link-firmware) — RP2040 link‑cable firmware (MIT).
- [`Lorenzooone/PokemonGB_Online_Trades`](https://github.com/Lorenzooone/PokemonGB_Online_Trades) — the proven, sanity‑checked Gen 1/2/3 trade engine we reuse for Path A (MIT).
- [`kbembedded/Flipper-Zero-Game-Boy-Pokemon-Trading`](https://github.com/kbembedded/Flipper-Zero-Game-Boy-Pokemon-Trading) — proof + reference that the standalone Path B is feasible (MIT).

## Legal / ethics

This copies *your own* Pokémon from *your own* cartridge to *your own* storage for
personal backup and portability. It does not modify or distribute Nintendo's game
code. Don't use it to clone Pokémon into trades with other people without telling
them.
