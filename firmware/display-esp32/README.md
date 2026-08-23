# Hosyond E32R32P touch display

This firmware turns the Hosyond 3.2-inch ESP32-32E display into a standalone
front panel for pico_ball. It shows the Pico's on-device vault, lets you page
through and select Pokémon, and starts a confirmed Gen 3 injection from the
touchscreen.

The vault browser includes all 788 generation-correct Red/Blue, Crystal, and
Emerald front sprites offline. Tapping a row opens a two-tab detail view with
nickname, level, gender/shiny state, types, HP and battle stats, held item,
moves/PP, OT/ID, EXP, DVs or IVs, Gen 3 EVs, nature, and ability. Sprite data is
generated from `PokeAPI/sprites` at the commit pinned in
`tools/gen_display_sprites.py`; Pokémon artwork remains property of its
respective rights holders.

Exact supported board:

- Hosyond / LCDWiki `E32R32P` (the resistive-touch model)
- ESP32-WROOM-32E, 4 MB flash
- 3.2-inch 240×320 IPS LCD, ST7789P3 over SPI
- XPT2046 resistive touch controller

The similar `E32N32P` has no touch layer and cannot provide the picker UI.
This is not the common 2.8-inch ESP32-2432S028R CYD; its pin map is different.
The source pin assignments come from the vendor's exact-revision examples:
https://www.lcdwiki.com/3.2inch_ESP32-32E_Display

## Architecture

The display board and RP2040 remain separate controllers:

- RP2040 owns the vault, flash writes, Game Boy link, multiboot, and trades.
- ESP32 owns the LCD, touch input, species labels, and workflow prompts.
- A 115200-baud 3.3 V UART carries the same `d`, `3`, `m`, and `i <slot>`
  command protocol already used by USB. USB/WebUI operation continues to work.

This keeps all trade-critical timing on the already proven Pico path.

## Wiring

Power both boards from their own USB connectors during bring-up. Connect only
these three 3.3 V signals, following the PCB labels rather than wire colors:

| E32R32P expansion pin | RP2040-Zero | Purpose |
|---|---|---|
| `IO25` | `GPIO5` | ESP32 TX → Pico UART1 RX |
| `IO32` | `GPIO4` | ESP32 RX ← Pico UART1 TX |
| `GND` | `GND` | Common signal ground |

On the E32R32P, IO25/IO32 are on the connector documented as I2C. This firmware
reassigns them to UART2. Do not connect the connector's VCC lead when both
boards are USB powered, and never connect 5 V to either UART signal.

Pico GPIO0–2 remain dedicated to the Game Boy link, and GPIO16 remains the
on-board WS2812 status LED.

## Build and flash

Install PlatformIO once, then build and upload the ESP32 firmware:

```bash
cd firmware/display-esp32
pio run
pio run -t upload
```

PlatformIO pins the ESP32 platform and TFT_eSPI versions in `platformio.ini`.
It uses Espressif's `huge_app.csv` partition layout (3 MB application, no OTA)
so all offline sprites fit on the board's 4 MB flash.
The pre-build script also replaces TFT_eSPI's generic ST7789 initializer with
the exact power/gamma sequence from Hosyond's E32R32P V1.0 package; the vendor
changed that file without changing TFT_eSPI's reported version.
The board's USB-C connector uses its onboard CH340C programmer. Disconnect the
Pico UART wires if an upload has trouble, then reconnect after flashing.

To deliberately refresh the generated lookup/sprite sources after changing the
WebUI tables or pinned sprite commit:

```bash
python3 tools/gen_display_species.py
# Pillow is required only when regenerating the normalized PNG bundle.
python3 tools/gen_display_sprites.py
```

Rebuild and flash the matching Pico firmware so it exposes UART1:

```bash
export PICO_SDK_PATH=~/pico-sdk
cmake -S firmware/path-b-standalone -B firmware/path-b-standalone/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/path-b-standalone/build --parallel
# Hold Pico BOOTSEL while connecting it, then copy:
cp firmware/path-b-standalone/build/pico_ball_vault.uf2 /media/$USER/RPI-RP2/
```

## Use

1. Power both boards and connect the three UART wires.
2. Tap Refresh if the vault does not load automatically.
3. Tap a Gen 3 Pokémon row. The selected row turns blue.
4. Put the GBA at its BIOS/boot screen with the Gen 3 cartridge inserted and the
   link cable connected.
5. Tap Send to GBA. The Pico multiboots Gen3-to-GenX into the GBA.
6. On the GBA, open the Gen 3 trade option and advance to its trade screen.
7. Tap Trade now, then select the cartridge Pokémon you want to give away.
8. The received cartridge Pokémon is automatically upserted into the vault;
   the display refreshes after a successful trade.

The display intentionally rejects Gen 1/2 entries in this workflow. Those use
the Cable Club injection path and can be added as a separate screen later.

## Touch calibration

The firmware starts with the vendor's landscape calibration:

```text
{366, 3573, 257, 3590, 3}
```

Resistive panels vary. If touches are offset, run the vendor TFT_eSPI
`Touch_calibrate` example in the E32R32P data pack, keep rotation 1, then replace
`calibration` in `src/main.cpp` with the five reported values.

## First-hardware acceptance test

1. Boot the display by itself: confirm the landscape UI fills all 320×240 pixels.
2. Connect UART: Refresh must change “Reading vault...” to a count and rows.
3. Tap every screen corner and each bottom button; recalibrate if targets drift.
4. Select a Gen 3 row and confirm its slot/name/level match `pb list` or WebUI.
5. With the GBA at BIOS, tap Send to GBA and confirm the GBA program boots.
6. Reach the trade screen, tap Trade now, complete one trade, and confirm the
   display reports success and refreshes the swapped-in species.

The display, touch navigation, complete 21-record vault refresh, generation
sprites, and detail views were validated on a physical E32R32P. Gen 3 injection
still requires the GBA-side steps above for each cartridge/trade session.
