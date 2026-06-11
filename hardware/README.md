# pico-ball deck — standalone handheld trading board

A Pi-Zero-form-factor carrier board that turns pico_ball into a pocket device:
no laptop, battery powered, screen + buttons, WiFi/BT.

The trick that makes this design cheap and easy: **the board copies the
Raspberry Pi Zero's mechanical footprint (65 × 30 mm, same corner holes, same
40-pin header position, same back-side power pads)**. That means we design
*zero* display electronics and *zero* battery electronics — we inherit two
mature off-the-shelf ecosystems:

- **Top:** Waveshare 1.3" LCD HAT (240×240 IPS, ST7789) plugs onto the 40-pin
  header and brings its own 5-way joystick + 3 buttons.
- **Bottom:** PiSugar 3 (1200 mAh, Zero size) screws onto the corner holes and
  feeds 5 V through spring-loaded pogo pins into back-side pads — plus an I2C
  fuel gauge (0x57) and DS3231-compatible RTC (0x68), so the UI can show
  battery % and the vault can timestamp captures.
- **Middle (our board):** Pico 2 W (RP2350 + CYW43439 WiFi/BT), the proven
  level-shifter + GB/GBC/GBA link connector copied from the current
  `game-boy-zero-link-board`, and glue.

```
            ┌──────────────────────────────┐
            │  Waveshare 1.3" LCD HAT      │  240×240, joystick, K1-K3
            └────────┬┬┬┬ 40-pin ┬┬┬┬──────┘
 link cable ═╗       ││││        ││││
┌────────────╨───────┴┴┴┴────────┴┴┴┴──────┐
│ J1 edge   [shifter]   [ Pico 2 W ]  USB ─┼── (flash/debug/host mode)
│ tab                                      │  65 × 30 mm carrier
└──○────[PP1][PP6]──[hdr 3/5 pads]──────○──┘
   M2.5      ↑ pogo pins ↑              M2.5
            ┌──────────────────────────────┐
            │  PiSugar 3  (1200 mAh)       │  own USB-C for charging
            └──────────────────────────────┘
```

## Why each part

| Choice | Reason |
|---|---|
| Pico 2 W module (not bare RP2350) | hand-solderable castellations, onboard USB/flash/crystal/SMPS + CYW43439 WiFi/BT; existing RP2040 firmware ports with near-zero changes (same SDK, same PIO) |
| Pi Zero form factor | display + battery + cases become someone else's solved problem |
| Waveshare 1.3" LCD HAT | SPI ST7789 that RP2350 drives directly; joystick + 3 buttons included, so the carrier needs no UI hardware of its own |
| PiSugar 3 (Zero) | tool-less battery: pogo-pin power, I2C battery %, RTC, hardware safe-shutdown; charges via its own USB-C so the carrier needs **no charger circuit at all** |
| Keep GP0/1/2 for the link | matches `firmware/path-b-standalone/gb_link.c` — link engine runs unmodified |

## Power architecture

- PiSugar pogo pin → **PP1 pad** (bottom copper, Pi Zero position) → Schottky
  (SS34) → Pico 2 W **VSYS**. PP6 pad = GND.
- Pico USB VBUS already diode-ORs into VSYS inside the module, so USB and
  PiSugar can be connected simultaneously without contention.
- Header 5V pins (2/4) tie to the PiSugar 5V net. A solder jumper (JP1,
  default open) can bridge VBUS → 5V net for running 5V HATs without a
  PiSugar. The 1.3" LCD HAT itself runs entirely on 3.3 V.
- 3V3 for the HAT comes from the Pico's onboard regulator (header pins 1/17).
  Budget: CYW43 WiFi TX bursts + ST7789 + backlight ≈ 250–300 mA peak, at the
  edge of the module regulator's comfort zone → carrier includes a **DNP
  AP2112K-3.3** footprint fed from the 5V net as a relief valve if rev-A
  testing shows brownouts during WiFi TX with backlight at 100%.

## Pin map (locked to existing firmware where it matters)

| Pico GPIO | Function | Routed to |
|---|---|---|
| GP0 | LINK_SCK | level shifter → J1 (matches `gb_link.c`) |
| GP1 | LINK_SIN (cart SO → us) | level shifter → J1 |
| GP2 | LINK_SOUT (us → cart SI) | level shifter → J1 |
| GP3 | LINK_SD (spare/detect) | level shifter → J1 |
| GP4 | JOY_PRESS | header pin 33 (BCM13) |
| GP5 | JOY_UP | header pin 31 (BCM6) |
| GP6 | JOY_LEFT | header pin 29 (BCM5) |
| GP7 | KEY3 | header pin 36 (BCM16) |
| GP8 | LCD_CS | header pin 24 (BCM8) |
| GP9 | LCD_DC | header pin 22 (BCM25) |
| GP10 | LCD_SCK (SPI1) | header pin 23 (BCM11) |
| GP11 | LCD_MOSI (SPI1 TX) | header pin 19 (BCM10) |
| GP12 | LCD_RST | header pin 13 (BCM27) |
| GP13 | LCD_BL (PWM6B → dimmable) | header pin 18 (BCM24) |
| GP14 / GP15 | I2C1 SDA/SCL — PiSugar fuel gauge + RTC | header pins 3 / 5 (+4.7k pull-ups to 3V3) |
| GP16 / GP17 | UART0 TX/RX debug | header pins 8 / 10 |
| GP18–GP21 | spare (rev-B candidates: WS2812, USER button) | — |
| GP22 | KEY1 | header pin 40 (BCM21) |
| GP26 | JOY_RIGHT | header pin 37 (BCM26) |
| GP27 | KEY2 | header pin 38 (BCM20) |
| GP28 | JOY_DOWN | header pin 35 (BCM19) |
| RUN | RESET button, side-accessible (rev B) | local tact switch |

GPIO choices are routing-driven: each signal exits the Pico as close as the
RP2350 peripheral mux allows to its header pin (this is what made the board
autoroutable — the first map sent KEY3 35 mm across the board). Only GP0–3
are fixed by existing firmware.

All joystick/key inputs are active-low with internal pull-ups, same convention
as the HAT expects on the Pi.

## Mechanical

- Outline 65 × 30 mm; 3 × M2.5 holes at Pi Zero positions (58 × 23 mm grid,
  3.5 mm in from edges). PiSugar screws into these from below. The fourth
  hole position (bottom-right) is **not drilled** — it falls under the Pico
  module and the drill would cut its pads 38–40.
- **J1 link edge tab** extends ~9 mm past the **left short edge** (where the
  Pi Zero's SD slot normally protrudes — most Zero cases already have an
  opening there). Footprint copied verbatim from the working
  `game-boy-zero-link-board` J1 + shifter section.
- **Pico 2 W micro-USB faces the right short edge**, flush, for flashing and
  laptop/host mode. (The 51×21 mm module only fits lengthwise.)
- Bottom side kept flat: only the PP1/PP6 pads, header through-hole pads, and
  nothing tall in the PiSugar pogo-pin sweep area.
- BOOTSEL on the module is covered by the HAT. Normal flashing uses
  `picotool reboot -u` over USB or a firmware gesture on the USER button;
  worst-case recovery = pop the HAT off and press BOOTSEL directly.

## Firmware deltas (small, all additive)

1. Re-target build to `pico2_w`; link engine (`gb_link.c`, PIO SPI) unchanged.
2. ST7789 SPI driver + a simple menu (vault list, capture, inject, trade) on
   the joystick/keys — replaces the LED-color + BOOTSEL-gesture UI.
3. I2C client for PiSugar: battery % in the status bar, RTC timestamps on
   vault writes.
4. CYW43: serve the existing `webui/` over WiFi from the device itself
   (lwIP httpd) — phone browser replaces the laptop entirely.

## Open items — status

1. ✅ **PiSugar pogo targets.** PiSugar doesn't publish coordinates, so the
   board clones *all four* candidate pads (both 5V + both adjacent GND) at the
   coordinates from the official Zero 2 W test-pad drawing
   (`reference/datasheets/pi-zero2-test-pads.pdf`) — whichever two the pogo
   pins touch, they land on the right net. I2C contacts the underside of
   header pins 3/5, which exist anyway.
2. ✅ **Outline/holes/header** taken from the official Pi Zero mechanical
   drawing (`reference/datasheets/pi-zero-mechanical.pdf`).
3. ✅ **J1 footprint + level-shifter circuit** lifted from the open-source
   (MIT) `weimanc/game-boy-zero-link-board` KiCad project — the same design
   as the working board, including the trick of powering the shifter's HV
   side from the link cable's own VCC pin (auto 5V/3.3V). The shifter is the
   SparkFun BOB-12009 breakout mounted as a module, exactly like the proven
   board.
4. ⏳ Confirm 3V3 rail headroom under WiFi TX + full backlight on real
   hardware (decides whether the DNP LDO gets populated).

**Placement reality found during layout:** a full-size Pico module flush
against a short edge necessarily covers one corner screw hole (21 mm module
vs 16.5 mm of clear span between hole keepouts), so rev A is a **3-standoff
mount** — fine for these masses. The covered hole is omitted from the board
outright (its drill would pass through the Pico's pads 38–40). Details in
`kicad/README.md`.

## KiCad

`kicad/pico-ball-deck.kicad_pcb` — generated rev-A skeleton: outline, all
footprints placed, full netlist, unrouted. `kicad/generate_board.py`
regenerates it; `kicad/README.md` has the audit list and remaining work.

## Cost (rev A, qty 1 of 5 boards)

| Item | ~Cost |
|---|---|
| PCB 2-layer, 5 pcs (JLCPCB) | $4–8 |
| Pico 2 W | $7 |
| Shifter, Schottky, buttons, WS2812, header, passives | $4 |
| Waveshare 1.3" LCD HAT | $15 |
| PiSugar 3 1200 mAh | $40 |
| **Total (full handheld)** | **≈ $70** |

The carrier alone is ~$15 — and it still works laptop-tethered with no HAT
and no PiSugar, exactly like the current board.

## Next steps

1. Resolve open items 1–3 (one PiSugar measurement session + footprint lift).
2. KiCad project: schematic capture from the pin map above, 2-layer layout.
3. Order rev A, bring up in stages: USB-only → HAT → PiSugar → WiFi.
