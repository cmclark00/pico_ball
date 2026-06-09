# The plan

Goal: connect the `game-boy-zero-link-board` to a real Pokémon Blue cartridge
(played on a GBA) and copy a Pokémon off the cartridge into portable storage,
ultimately on the board itself so it can be carried with no PC.

This is split into two paths. **Do Path A first** — it validates your hardware,
your soldering, the link wiring, and the trade protocol end‑to‑end with the least
possible new code. Then Path B reuses that knowledge to go standalone.

---

## Path A — PC‑tethered extraction (implemented)

The RP2040 runs an existing, proven firmware that turns the board into a USB
link‑cable bridge. A Python program on your laptop runs the trade state machine,
reads the cartridge's party, and saves it.

### A0. Hardware bring‑up ✅ (you confirmed the board is assembled & flashable)
- Board enumerates over USB; you can drag‑drop a UF2 in BOOTSEL mode.

### A1. Flash the bridge firmware
- Flash `stacksmashing/gb-link-firmware` (USB VID `0xCAFE`, PID `0x4011`).
- It exposes a WebUSB/vendor interface: host writes one byte → RP2040 clocks one
  byte over the link as SPI master → returns the byte the Game Boy sent.
- Details + exact steps: [firmware/README.md](../firmware/README.md).

### A2. Host environment
- `./scripts/setup.sh` creates `host/.venv`, installs `pyusb` + `pyserial`, and
  clones the pinned trade engine into `third_party/`.
- Linux: a udev rule (printed by the script) lets you access the device without
  sudo.

### A3. Prove the link works — do a real trade once
- Easiest proof of life: run the upstream engine and do a **Pool trade** (against
  its public server) or a **2‑player trade**. If a Pokémon moves, your wiring +
  firmware + cartridge all work. See [docs/hardware-and-flashing.md](hardware-and-flashing.md).

### A4. Non‑destructive copy into the vault (the bespoke feature)
- `python host/extract.py`:
  1. Performs the Gen 1 handshake and the trade‑starting sequence.
  2. Reads the cartridge's trade block → all 6 party Pokémon.
  3. Writes each to `vault/` as raw `.pk1` (44‑byte Gen 1 structure) **and**
     decoded `.json` (species, nickname, level, moves, OT, IVs…).
  4. **Cancels** the trade so the cartridge is unchanged.
- This realizes "carry a copy of my Pokémon" with the laptop as the carrier.

### A5. Inject back / restore ✅ (implemented: `host/inject.py`)
- Trades a vaulted Pokémon *into* a cartridge. A trade is a swap, so the cart
  gives up whichever Pokémon you pick on the Game Boy — and `inject.py`
  **auto‑captures that given‑up Pokémon into the vault**, so nothing is ever
  lost. Build the outgoing 1‑mon party from the vault record, run the trade
  sections, then drive the selection/confirm/success handshake to commit
  (`engine.local_inject_commit`). Validated offline to byte‑perfect round‑trip;
  needs a hardware run to confirm the live commit.

**Exit criteria for Path A:** `extract.py` reliably dumps your Blue party to
`vault/` and the cartridge still has every Pokémon afterward.

---

## Path B — Standalone pocket vault (BUILT; awaiting hardware bring-up)

> Implemented and compiling: `firmware/path-b-standalone/` →
> `firmware/prebuilt/pico_ball_vault.uf2`. Capture FSM, flash storage, LED/button
> UI, and PC retrieval (`host/import_standalone.py`) all done; revised to SPI
> **master** (single-core, reuses Path A's proven link). See
> [firmware/path-b-standalone/README.md](../firmware/path-b-standalone/README.md)
> for the staged bring-up plan. Original design notes below.


Move the entire trade onto the RP2040 so no computer is involved. The board
becomes the thing you carry.

### B1. Firmware skeleton
- Pico SDK C project. PIO state machine implements the link as **slave** (the
  Game Boy supplies the ~8 kHz clock; we respond within ~120 µs/byte). This is the
  Flipper approach and is timing‑robust because the cartridge sets the pace.
- See [firmware/path-b-standalone/DESIGN.md](../firmware/path-b-standalone/DESIGN.md).

### B2. On‑device trade FSM
- Port the Gen 1 trade state machine (constants/transitions are documented in
  [docs/gen1-trade-protocol.md](gen1-trade-protocol.md) and proven in the upstream
  engine + the Flipper project) into C running on core1.

### B3. On‑device storage
- Store captured Pokémon in the RP2040‑Zero's 2 MB QSPI flash via LittleFS (a
  reserved region above the firmware). Each mon is 44 bytes — thousands fit.

### B4. UI without a screen
- The board has a button (mapped to a D‑pad/button in the case) and the RP2040‑Zero
  has an on‑board WS2812 RGB LED. Use button‑to‑arm + LED colors for state
  (idle / waiting / trading / captured / error). A tiny SSD1306 OLED is an easy
  optional upgrade if you want names.

### B5. Trade back out
- Reverse direction: arm a stored Pokémon and trade it into a cartridge.

**Exit criteria for Path B:** with no PC attached, press the button, sit at the
Cable Club table, and a Pokémon is copied into the board's flash; LED confirms.

---

## Risk register

| Risk | Mitigation |
|---|---|
| Link wiring / solder bridge wrong | Path A3 proves the physical link before any custom code. |
| Master‑clock pacing (Path A) outruns the cartridge | We reuse the upstream engine's tuned `swap_byte` timing + sanity checks. |
| GBA‑in‑GB‑mode link quirks | Use a genuine GB/GBA link cable into the board's GBC/GBA connector; if flaky, test with a DMG/GBC first. |
| Path B slave timing on RP2040 | PIO handles the bit timing in hardware; core1 only needs to keep the next byte loaded. |
| Cartridge data safety | Copy‑then‑cancel never writes to the cartridge; sanity checks reject malformed bytes. |

## Where we are now

- [x] Research complete (board, firmware, protocol, references) → [docs/research.md](research.md)
- [x] Path A host extractor implemented → `host/extract.py`
- [x] Firmware + flashing instructions → [firmware/README.md](../firmware/README.md)
- [x] Path B design → [firmware/path-b-standalone/DESIGN.md](../firmware/path-b-standalone/DESIGN.md)
- [ ] **Next:** you flash the firmware and run `extract.py` on the real cartridge; report results so we tune timing if needed.
