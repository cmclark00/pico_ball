# Firmware (Path A): flashing the link‑cable bridge

For Path A the RP2040 just needs to be a **USB ↔ link‑cable bridge**. We use
[`stacksmashing/gb-link-firmware`](https://github.com/stacksmashing/gb-link-firmware),
which the board's own README lists as compatible.

What it does: presents USB **VID `0xCAFE` / PID `0x4011`** with a WebUSB/vendor
interface. The host writes one byte, the RP2040 clocks one byte over the Game Boy
link (as SPI master on PIO), and returns the byte the Game Boy sent. That 1‑in /
1‑out exchange is what `host/extract.py` drives.

## Option 1 — flash the prebuilt UF2 (already built for you)

A ready‑to‑flash UF2 is in this repo at **`firmware/prebuilt/gb-link-firmware.uf2`**
(built from `stacksmashing/gb-link-firmware` against Pico SDK 2.x for the RP2040).

1. Hold **BOOTSEL** on the RP2040‑Zero and plug it into USB. It mounts as a mass‑
   storage drive named `RPI-RP2`.
2. Copy the UF2 onto that drive. The board reboots running it:
   ```bash
   cp firmware/prebuilt/gb-link-firmware.uf2 /media/$USER/RPI-RP2/
   # (or just drag-and-drop in your file manager)
   ```
3. Verify enumeration (Linux):
   ```bash
   lsusb | grep -i cafe        # expect ID cafe:4011
   ```

> Upstream ships no release `.uf2`, so this one was built locally (Option 2).

## Option 2 — build from source (what produced the prebuilt UF2)

```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
git clone --depth 1 https://github.com/raspberrypi/pico-sdk ~/pico-sdk
export PICO_SDK_PATH=~/pico-sdk
git -C ~/pico-sdk submodule update --init --depth 1 lib/tinyusb   # USB stack

git clone https://github.com/stacksmashing/gb-link-firmware
cd gb-link-firmware && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..     # default 'pico' board is fine for RP2040
make -j
# result: gbusb.uf2  -> BOOTSEL + copy onto RPI-RP2
```

### Required patch for modern Pico SDK (SDK 2.x / TinyUSB ≥ 0.15)

The upstream firmware predates a TinyUSB API change: the separate
`tud_vendor_control_request_cb` + `tud_vendor_control_complete_cb` callbacks were
merged into one `tud_vendor_control_xfer_cb(rhport, stage, request)`, and the old
names are now compile‑time "poisoned." In `main.c`:

- Rename `tud_vendor_control_request_cb(uint8_t rhport, ... request)` to
  `tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, ... request)` and add
  `if (stage != CONTROL_STAGE_SETUP) return true;` as its first line.
- Delete the now‑unused `tud_vendor_control_complete_cb` function.

Behavior is identical (it did real work only at the SETUP stage).

A second change is needed for the same reason: newer TinyUSB **no longer
auto-flushes the vendor TX FIFO**, so the firmware's reply byte would sit in the
buffer forever and host reads time out. In `echo_all()`, add `tud_vendor_write_flush();`
right after `tud_vendor_write(buf, count);`.

The prebuilt UF2 already includes both patches; the patched source is under
`firmware/path-a-build/gb-link-firmware/` in this repo.

### About the on‑board LED

This build uses the generic `pico` board, so the firmware's `board_led_write()`
blink targets the default Pico LED pin, which isn't wired on the RP2040‑Zero (it
has a WS2812 RGB LED instead). That only affects the status blink — USB and the
link‑cable trade work regardless. Not worth chasing for Path A.

## Pin mapping (from the firmware source)

The firmware drives the link on these RP2040 GPIOs:

| Signal | Link cable pin | RP2040 GPIO |
|---|---|---|
| SCK (clock) | 5 | GPIO 0 |
| SI  (Game Boy serial in)  | 3 | GPIO 1 (firmware `SIN`) |
| SO  (Game Boy serial out) | 2 | GPIO 2 (firmware `SOUT`) |
| GND | 6 | GND |
| GBVCC | 1 | → HV side of level shifter |

The board's PCB already routes the edge connector to these pins through the level
shifter; you don't wire this by hand. This table is here so you can debug with a
scope/logic analyzer if a trade stalls.

## Alternative firmware

`Lorenzooone/gb-link-firmware-reconfigurable` is a fork that the trade engine
recommends for **Gen 3** games and that avoids altering the byte stream. For
**Pokémon Blue (Gen 1)** the plain `stacksmashing` firmware is fine. If you later
want one board to also do Gen 2/3, prefer the reconfigurable firmware.

## Sanity check before trading

After flashing, with the board plugged into USB only (no Game Boy yet):

```bash
source ../host/.venv/bin/activate     # after scripts/setup.sh
python ../host/extract.py --selftest  # opens the device, toggles the 0x22 enable
```

`--selftest` only confirms the host can open the USB device and enable the data
path; it does not need a Game Boy attached.
