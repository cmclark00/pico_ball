# Gen 4 GTS server — standalone on a Pico 2 W (Route B)

This firmware turns a **bare Pico 2 W** into a self-contained Nintendo Wi-Fi
Connection + Generation IV **GTS** server. A Nintendo DS running a Gen 4 game
(Diamond / Pearl / Platinum / HeartGold / SoulSilver) connects to the board's
own WiFi, and you can **deposit a Pokémon into the in-game GTS — the board
captures it** and dumps it over USB.

**No link-cable hardware is involved.** Gen 4 trading-over-GTS is pure WiFi, so
none of the level-shifter / GB edge connector / pico-ball-deck is needed — the
CYW43439 radio in the Pico 2 W is the entire hardware requirement. A bare module
on USB power is enough.

> Why this works at all: Gen 4 GTS is plain HTTP, and DS games in WFC mode
> associate to **open / WEP** APs (no WPA). The reference tool **IR-GTS** proves
> GTS needs only **DNS + HTTP** — no GameSpy TCP servers — which is exactly what
> a lone Pico 2 W can host. DS-to-DS *local wireless* trading is a different,
> proprietary radio protocol the CYW43439 cannot speak; that is out of scope.

## Status

| Piece | State |
|---|---|
| Open SoftAP + DHCP + catch-all DNS | ✅ built |
| WFC connection test (`conntest.nintendowifi.net`) | ✅ built (AltWFC-grounded) |
| NAS login / acctcreate (`/ac`) | ✅ built (AltWFC-grounded) |
| GTS `info` / `setProfile` / `post` / `search` / `delete` | ✅ built (IR-GTS-grounded) |
| **Capture** (deposit → decrypt → USB dump) | ✅ built; cipher **byte-exact** vs IR-GTS reference (host test) |
| **Inject** (`result.asp` → cart) | ⚠️ implemented but **UNVALIDATED** — validate against DeSmuME before a real cart |

The two risks are deliberately separated. The capture (deposit) direction is the
solid, primary path. The inject (withdraw) direction needs an emulator pass: the
trailing GTS metadata block and the inbound 16-bit checksum check are not yet
confirmed. See `gts.c` for the flagged spots.

## Build

The ARM toolchain + Pico SDK 2.2.0 are the same ones the rest of the repo uses.
You need the `lwip` and `cyw43-driver` SDK submodules (the GB-link builds don't):

```bash
git -C "$PICO_SDK_PATH" submodule update --init lib/lwip lib/cyw43-driver
export PICO_SDK_PATH=~/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/pico_ball_gts.uf2   (target chip RP2350, board pico2_w)
```

A prebuilt image is at `../prebuilt/pico_ball_gts.uf2`.

## Flash

Hold BOOTSEL, plug the Pico 2 W into USB, drag `pico_ball_gts.uf2` onto the
`RP2350` drive (or `picotool load -x build/pico_ball_gts.uf2`). Open the USB
serial port at any baud to watch the logs.

## Live test (Phase 1 — the connection milestone)

This needs only the bare Pico 2 W + your DS + a Gen 4 game. No emulator.

1. Power the board. Serial prints `SoftAP up: SSID 'pico-ball-gts' (open)` and
   `[HTTP] listening on :80`.
2. On the DS, open **Nintendo Wi-Fi Connection Settings** (in-game, via the GTS
   or the title's Wi-Fi setup).
3. Configure a connection → **Search for an Access Point** → pick the open
   network **`pico-ball-gts`** → save. DHCP hands the DS an address plus DNS =
   `192.168.4.1`, so no manual DNS entry is needed.
4. Run **Test Connection**. You should see the board log a request for `/`
   (the connection test) and then `POST /ac` (NAS login) — and the DS should
   report **success**. That's the whole WiFi + DNS + NAS front door proven.

## Live test (Phase 2 — capture a Pokémon)

1. In-game, go to the **Global Terminal / GTS** and connect.
2. **Deposit** a Pokémon. The board logs:
   ```
   [GTS] info
   GTS-CAPTURE #1 species=25 len=236
   PK4:<472 hex chars>
   ```
   That `PK4:` line is the decrypted 236-byte Gen 4 party Pokémon. Pipe the
   serial output to a file and split out the hex to get a `.pk4` you can open in
   PKHeX. (Your cartridge keeps its copy until a "trade" completes, so deposit
   is non-destructive — same principle as the Gen 1/2/3 capture paths.)

Inject (handing a vaulted Pokémon back to the cart via `result.asp`) is wired
but **off by default and unvalidated** — do the DeSmuME pass first.

## Sources

- Connection test + NAS: [dwc_network_server_emulator (AltWFC)](https://github.com/barronwaffles/dwc_network_server_emulator)
  — `nas_server.py`, `other/utils.py` (the `=`→`*` base64 + `NODE: wifiappe1`),
  and the `X-Organization: Nintendo` conntest requirement.
- GTS endpoints + cipher: [IR-GTS](https://github.com/JamieJQuinn/IR-GTS)
  — endpoint responses and `decrypt_pokemon` / `decrypt_sce_data`
  (`state=(state*0x45+0x1111)&0x7fffffff`, seed `checksum|(checksum<<16)`,
  header `^0x4a3b2c1d`).
- Protocol overview: [Project Pokémon Wi-Fi Protocol Structure](https://projectpokemon.org/home/docs/other/wi-fi-protocol-structure-r88/).
- DHCP/DNS servers: vendored from
  [pico-examples `access_point`](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/access_point)
  (BSD-3 / MIT, headers retained).
