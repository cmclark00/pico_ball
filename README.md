# pico_ball — carry your Pokémon out of a real cartridge

[![Build firmware](https://github.com/cmclark00/pico_ball/actions/workflows/firmware.yml/badge.svg)](https://github.com/cmclark00/pico_ball/actions/workflows/firmware.yml)

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

# 4. Copy your party into the vault (non-destructive). The ./pb launcher runs
#    the host tools in the project venv, so there's nothing to activate:
./pb extract              # saves vault/*.pk1 + vault/*.json, then cancels
./pb extract --gen 2      #   ...or Gold/Silver/Crystal (Gen 2 -> *.pk2)

# Gen 3 (Ruby/Sapphire/Emerald/FireRed/LeafGreen): flash the reconfigurable
# firmware (third_party/gen3/gbusb_reconfigurable.uf2), then multiboot the
# GBA-side trade program and capture:
./pb gen3-boot            # upload Gen3-to-GenX into the GBA's RAM
./pb extract --gen 3      # saves vault/*.pk3 (PKHeX party format)

# 5. See what you've captured (no device needed):
./pb list                 # decoded table: gen, species, level, OT, nickname

# 6. (Optional) Trade a vaulted Pokémon back INTO a cartridge:
./pb inject               # pick from a decoded list; the cart's given-up mon is
                          # auto-saved to the vault, so nothing is lost

# 7. (Optional) Export your captures as a .sav (opens in PKHeX/emulators):
./pb export-sav           # Gen 1 -> vault/pico_ball.sav  (--gen 2 for G/S/C)
```

> Prefer the raw scripts? Every `./pb <cmd>` maps to `python host/<cmd>.py`
> after `source host/.venv/bin/activate` — the launcher just saves the typing.

See **[docs/PLAN.md](docs/PLAN.md)** for the full phased plan, and
**[docs/research.md](docs/research.md)** for everything the design is based on
(with sources).

## Gen 1/2 → Gen 3 "transfer" (Pal-Park-style recreation)

The Game Boy and GBA games have **no native link** — Nintendo's first official
forward-migration was Gen 3 → Gen 4 (Pal Park). So a literal transfer is
impossible, but we can *recreate* a captured Gen 1/2 Pokémon as a legal Gen 3
`.pk3`, exactly like **[Poké Transporter GB](https://github.com/GearsProgress/Poke_Transporter_GB)**
does, using its **[Pokémon Community Conversion Standard](https://github.com/GearsProgress/Pokemon-Community-Conversion-Standard)**
(PCCS, MIT). Species, level, moves, nickname, and OT are kept; PID / IVs / nature
/ ability are generated to match.

There are two ways to do it:

**A. Onto a real Gen 3 cartridge — no flashcart, no PC** (standalone board). The
board **multiboots Poke Transporter GB** into a GBA over the link cable, then
offers a stored Gen 1/2 mon to it; PTGB converts it and writes it to the Gen 3
cartridge's save. You only need the board, a GBA, a Gen 3 cart, and the cable —
hand someone a flashed board and they're set. See
[firmware/path-b-standalone/README.md](firmware/path-b-standalone/README.md)
("Transfer Gen 1/2 → Gen 3"). From the cartridge, the mon rides the official Pal
Park → … → HOME chain on real hardware.

**B. As a file, for PKHeX/emulators.** `scripts/setup.sh` also builds a host
converter (PCCS), so pulling captures off the board auto-writes a Gen 3 box `.pk3`
for each Gen 1/2 mon:

```bash
./pb import                         # vault/dex/*.pk1 + vault/dex/gen3/*.pk3
```

The `.pk3` files are 80-byte box records — drag them straight into **PKHeX** (or a
Gen 3 save). Run `python tools/test_pccs.py` to self-check the conversion.

Both use the same engine (PCCS), so they produce the same conversion. The endgame
is **Pokémon HOME**: a Gen 1/2 mon → Gen 3 → (Pal Park) Gen 4 → 5 → 6/7 → Bank →
HOME, legal at each hop. The board does the one step with no official path (Gen
1/2 → Gen 3); the rest is the standard forward-migration chain.

## View Pokémon straight from the device (WebUI)

With the standalone (Path B) firmware flashed, you don't need to copy anything to
disk to look at your captures:

```bash
./pb webui                # serves webui/ — open http://localhost:8000 in Chrome/Edge
```

Click **Connect device**, pick **"pico_ball vault"** from the WebUSB chooser, and
the page reads the stored records over **WebUSB** and decodes the Pokémon **in the
browser** — sprite, level, HP, moves, OT. Works on desktop and Android Chrome. See
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
- [`GearsProgress/Poke_Transporter_GB`](https://github.com/GearsProgress/Poke_Transporter_GB) + [`Pokemon-Community-Conversion-Standard`](https://github.com/GearsProgress/Pokemon-Community-Conversion-Standard) — the Gen 1/2 → Gen 3 conversion we vendor + build for the "transfer" feature (MIT).
- [`kbembedded/Flipper-Zero-Game-Boy-Pokemon-Trading`](https://github.com/kbembedded/Flipper-Zero-Game-Boy-Pokemon-Trading) — proof + reference that the standalone Path B is feasible (MIT).

## License

[MIT](LICENSE) for this project's own code. Vendored third‑party files (the Pico
SDK PIO helpers under `firmware/path-b-standalone/`) retain their own BSD‑3‑Clause
headers; the reused trade engine and bridge firmware are MIT and fetched/built at
setup time rather than redistributed here. See [LICENSE](LICENSE) for details.

## Continuous integration

`.github/workflows/firmware.yml` builds the standalone firmware on every change to
`firmware/path-b-standalone/` (Pico SDK pinned to a fixed version) and uploads the
`pico_ball_vault.uf2` as a workflow artifact. Trigger it manually from the Actions
tab via *Run workflow*.

## Legal / ethics

This copies *your own* Pokémon from *your own* cartridge to *your own* storage for
personal backup and portability. It does not modify or distribute Nintendo's game
code. Don't use it to clone Pokémon into trades with other people without telling
them.
