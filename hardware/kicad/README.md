# pico-ball deck — KiCad files

`pico-ball-deck.kicad_pcb` is the **fully routed rev-A board**:
**0 DRC violations, 0 unconnected items** (KiCad 8 `kicad-cli pcb drc`).
GND pours on both layers with stitching vias. Open it in KiCad 8+
(`pcbnew pico-ball-deck.kicad_pcb`).

## Routing pipeline (reproducible, Docker only — no local KiCad/Java)

```bash
./route.sh
```

runs: `generate_board.py` (placement + netlist) → `add_preroutes.py`
(hand-routed hard nets + GND stitch vias, see below) → DSN export →
**Freerouting 1.9** (Java 21 container) → SES import + zone fill
(`import_ses.py`) → DRC. ~4 minutes end to end.

Requires `../reference/game-boy-zero-link-board/` — clone of
https://github.com/weimanc/game-boy-zero-link-board, MIT — for the J1 link
connector, BOB-12009 and Pico module footprints.

**Why pre-routes:** the autorouter reliably fails 10 specific connections on
this board (bottom-row→header climbers boxed in by its own power buses, the
0.71 mm U2/pin-20 pinch, the long PSC/PSI/PSO link bus). `add_preroutes.py`
places those by hand on the fresh board — every path hand-verified at
≥ 0.25 mm copper / ≥ 0.3 mm edge clearance — and Freerouting routes the
remaining ~20 easy nets around them.

## What's verified

- Outline/holes/header from the official Pi Zero mechanical drawing
  (65×30 mm, r3 corners, M2.5 drilled 2.75 mm at 58×23 grid, header pin 1 at
  (8.37, 4.77), even pins on the edge row).
- PiSugar pogo pads cloned from the official **Zero 2 W test-pad drawing**
  (both 5V pads + both adjacent GND pads, 2.2 mm bottom copper) — whichever
  pads the PiSugar's pins actually touch, they hit the right net. I2C pogo
  pins contact the underside of header pins 3/5 (through-hole pads).
- J1 link tab machine-diffed against the proven weimanc board's J1
  (`gb-link-socket`, the small GBC-plug tab): congruent in every dimension —
  6.5 mm wide tab, 6.5 mm protrusion, pad centers 3.5 mm from the body edge
  / 3.0 mm from the tip, 1×5 mm pads at 2 mm pitch, 3 contacts per face,
  same pad→net map (1=GBVCC 2=SO 3=SI 4=SD 5=SC 6=GND). A no-pour rule area
  keeps the GND zones off the tab (matching the reference; the plug slides
  over it). Note the weimanc board also carries a second, larger
  `dmg-link-socket` tab (2×8 mm pads at 2.5/2.6 mm pitch) for the original
  DMG plug — this board only has the small-plug tab.
- Link stays on GP0/1/2(/3) through the BOB-12009 — matches
  `firmware/path-b-standalone/gb_link.c` unchanged.
- Pad positions machine-audited (all inside outline, USB end at right edge,
  no single-pad nets).

## Known rev-A tradeoffs

- **3-point mounting:** the Pico 2 W module covers the bottom-right M2.5 hole
  (a 21 mm module can't fit between the corner keepouts). That hole is
  **omitted** — DRC showed its drill would cut through the module's pads
  38–40. PiSugar + HAT mount on the other three standoffs, which is
  mechanically fine for these masses.
- No onboard buttons/LED: the LCD HAT's joystick + 3 keys are the UI; reset =
  power-cycle; BOOTSEL via `picotool reboot -u` or pop the HAT off. A
  side-actuated reset switch + WS2812 are rev-B candidates (GP15/GP26 kept
  free for them).
- D1/C1/JP1 sit on the bottom side (clear of the pogo zone); JP1 bridges
  VBUS→5V rail only for HAT-without-PiSugar use.
- **USB recessed 1.0 mm:** the Pico's castellation pads extend 2.65 mm past
  each pin row, which pins the module between the header pad row and the r3
  board corner. It sits 1.0 mm in from the right edge; the micro-USB shell
  protrudes ~1.3 mm past the module edge, so cables still seat.
- **U1 pins 34/36/37 have no through-holes** (top SMD pads only): the
  PiSugar's GND pogo pins land exactly on the back of that stretch of pin
  row, and plated holes there would short 3V3/3V3_EN to GND. The module
  solders by castellation, so nothing is lost.
- **U1 SWD pads (41–43) are plain through-holes** (castellation extensions
  removed — they crowded U2). Debug wires can still be soldered through.

## Remaining work before fab

1. ~~Route it~~ ✅ routed, DRC clean (see pipeline above).
2. ~~Sanity-check the tab~~ ✅ machine-diffed against the weimanc board's
   small-plug tab — congruent (see above). Still worth one physical dry fit.
3. Optional: draw the schematic to match (the netlist source of truth is
   `generate_board.py`).
4. ~~Generate gerbers~~ ✅ `pico-ball-deck-rev-a-gerbers.zip` (9 layers +
   Excellon drill + map, Protel extensions — upload straight to JLCPCB).
   Regenerate with `kicad-cli pcb export gerbers/drill` in the same Docker
   image; `add_tab_keepout.py` documents the tab rule area (now also baked
   into `import_ses.py`).

Note: the copper-to-edge DRC rule is 0.25 mm (JLC's minimum for routed
edges is 0.2 mm); net clearance is 0.25 mm everywhere, power nets 0.5 mm
track / GND-VSYS-VBUS-3V3-5V-GBVCC in the Power class.
