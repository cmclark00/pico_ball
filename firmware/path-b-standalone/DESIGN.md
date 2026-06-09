# Path B — standalone pocket‑vault firmware (design)

> **Built.** The firmware now exists and compiles — see **[README.md](README.md)**
> for build/flash/use. The implementation **revised two decisions** below: it
> uses SPI **master** (not slave) and a simple per‑sector flash store (not
> LittleFS), because Path A proved master clocking on this exact board and it
> makes the firmware single‑core and far lower‑risk. The README's "Design
> choices" section explains why. The original design is kept here for context.

Goal: no PC. Press the button, sit at the Cable Club table, the board copies a
Pokémon into its own flash. The board is the thing you carry.

This is the design + build order. It is intentionally *not* started in code until
Path A has validated your hardware and the protocol on the real cartridge — every
unknown we can retire on the laptop first is one we don't debug blind on the MCU.

## Why this is feasible

`kbembedded/Flipper-Zero-Game-Boy-Pokemon-Trading` already does exactly this on a
different MCU: it speaks the Gen 1/2 trade protocol as a **slave**, keeps the
received Pokémon in memory, and needs no PC. We port the same idea to the RP2040.

## Architecture

```
core0: app/UI         button (arm), WS2812 LED state machine, flash storage
core1: link engine    PIO SPI *slave* + Gen 1 trade FSM (timing-critical)
PIO:   1 state machine shifting 8 bits on the Game Boy's clock
flash: LittleFS region above the firmware image holds captured .pk1 records
```

### Link layer — PIO SPI **slave**

- The Game Boy supplies SCK. A PIO program shifts SO out / SI in on the clock
  edges (CPHA per GB link). Autopush/autopull at 8‑bit threshold.
- core1 keeps the **response byte preloaded** in the TX FIFO before each transfer.
  The whole trade is a fixed choreography, so the "next byte to send" is always
  known from the FSM state — we never have to compute it inside the ~120 µs window.
- Pins follow the board routing used by Path A firmware (SCK=GPIO0, SIN=GPIO1,
  SOUT=GPIO2); reuse so the same PCB works without rewiring.

### Trade FSM

Port the state machine from [docs/gen1-trade-protocol.md](../../docs/gen1-trade-protocol.md):
`enter_room → start_trading → section0 random → section1 party → section2 patches
→ selection`. For **copy‑and‑keep**: capture section 1, parse the 6 mons, then send
`stop_trade` (`0x6F`) so the cartridge keeps everything. Implement as a table‑driven
FSM mirroring the upstream engine's transitions (already proven byte‑for‑byte).

### Storage — LittleFS on QSPI flash

- RP2040‑Zero has 2 MB flash. Reserve a region above the firmware (linker script /
  `PICO_FLASH_SIZE`) for LittleFS.
- Record format: 44‑byte raw `.pk1` + 11‑byte nickname + 11‑byte OT name + a small
  header (species, level, captured‑timestamp). ~70 bytes/mon → thousands fit.
- Wear: writes are rare (one per capture); no leveling concerns in practice.

### UI without a screen

RP2040‑Zero has an on‑board **WS2812 (NeoPixel)** and the case exposes a button:

| LED | State |
|---|---|
| dim white | idle |
| pulsing blue | armed, waiting for a Game Boy |
| green sweep | trade in progress |
| solid green (2 s) | Pokémon captured to flash |
| red | error / link lost |

Button: short press = arm capture; long press = (later) arm "trade out" of the
most‑recent stored mon.

**Optional upgrade:** an I²C SSD1306 OLED (2 wires) to show species/nickname and a
stored‑count. Drop‑in; the case has room.

## Build order

1. `blink` + WS2812 sanity on the RP2040‑Zero.
2. PIO SPI **slave** loopback: verify it shifts bytes on an external clock (bench
   test against the Path A board acting as master, or a logic analyzer).
3. Echo trade bytes to USB CDC while still PC‑assisted — confirm the slave sees the
   real cartridge's handshake bytes matching the protocol doc.
4. Port `enter_room` + section exchange; capture section 1 to RAM; dump over USB.
5. Move capture into LittleFS; remove USB dependency; drive UI from the FSM.
6. (Stretch) "trade out": push a stored mon back into a cartridge.

## Toolchain

Same as building Path A firmware (Pico SDK + `arm-none-eabi-gcc`). A `CMakeLists.txt`
and `src/` skeleton will be added here when we start step 1.

## Open questions to resolve on hardware (cheaply, during Path A)

- Exact CPHA/CPOL the GBA‑in‑GB‑mode expects on our connector (confirm with a
  capture during a Path A trade).
- Whether the level shifter's direction auto‑sensing behaves with us as slave
  (driving SO) the same as it did with us as master.
