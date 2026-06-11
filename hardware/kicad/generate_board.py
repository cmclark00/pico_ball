#!/usr/bin/env python3
"""Generate pico-ball-deck.kicad_pcb (rev A placement skeleton, unrouted).

Reuses proven footprints from hardware/reference/game-boy-zero-link-board
(MIT, (c) 2023 agtbaskara): the GBC/GBA link edge connector (J1) and the
SparkFun BOB-12009 level-shifter breakout, plus the full-size Pico module
footprint (Pico 2 W shares it). Everything else (40-pin Pi header, PiSugar
pogo pads, mounting holes, passives) is generated here.

Board coordinate origin: Pi Zero top-left corner is at (50, 50) in the sheet.
All Pi-Zero-derived coordinates below are top-view; back-side test-pad
coordinates from the official Zero 2 W drawing (origin = back-view bottom-left)
convert as: x_top = 65 - x_back, y_top = 30 - y_back.
"""
import re
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
REF = HERE.parent / "reference" / "game-boy-zero-link-board"
OUT = HERE / "pico-ball-deck.kicad_pcb"

OX, OY = 50.0, 50.0  # sheet offset of Pi-Zero origin (top-left corner)


# ---------------- s-expression plumbing ----------------

def tokenize(s):
    toks, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in " \t\r\n":
            i += 1
        elif c in "()":
            toks.append(c)
            i += 1
        elif c == '"':
            j = i + 1
            while j < n:
                if s[j] == "\\":
                    j += 2
                elif s[j] == '"':
                    break
                else:
                    j += 1
            toks.append(s[i:j + 1])
            i = j + 1
        else:
            j = i
            while j < n and s[j] not in ' \t\r\n()"':
                j += 1
            toks.append(s[i:j])
            i = j
    return toks


def parse(toks, pos=0):
    assert toks[pos] == "("
    out, pos = [], pos + 1
    while toks[pos] != ")":
        if toks[pos] == "(":
            child, pos = parse(toks, pos)
            out.append(child)
        else:
            out.append(toks[pos])
            pos += 1
    return out, pos + 1


def parse_all(src):
    return parse(tokenize(src))[0]


def ser(node, indent=0):
    if isinstance(node, str):
        return node
    if all(isinstance(c, str) for c in node):
        return "(" + " ".join(node) + ")"
    i = 0
    head = []
    while i < len(node) and isinstance(node[i], str):
        head.append(node[i])
        i += 1
    s = "(" + " ".join(head)
    for c in node[i:]:
        s += "\n" + "  " * (indent + 1) + ser(c, indent + 1)
    return s + "\n" + "  " * indent + ")"


def q(s):
    return '"' + s + '"'


def f(v):
    return ("%.6f" % float(v)).rstrip("0").rstrip(".")


def find(node, key):
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def findall(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def ts():
    return ["tstamp", str(uuid.uuid4())]


# ---------------- net table ----------------

NET_NAMES = [
    "GND", "+3V3", "+5V_SUGAR", "VSYS", "VBUS",
    "GBVCC", "SC", "SI", "SO", "SD",
    "PSC", "PSI", "PSO", "PSD",
    "I2C_SDA", "I2C_SCL", "UART_TX", "UART_RX",
    "LCD_DC", "LCD_CS", "LCD_SCK", "LCD_MOSI", "LCD_RST", "LCD_BL",
    "JOY_UP", "JOY_DOWN", "JOY_LEFT", "JOY_RIGHT", "JOY_PRESS",
    "KEY1", "KEY2", "KEY3",
]
NETS = {name: i + 1 for i, name in enumerate(NET_NAMES)}


def net_node(name):
    return ["net", str(NETS[name]), q(name)]


# ---------------- footprint transforms ----------------

def _is_num(tok):
    try:
        float(tok)
        return True
    except ValueError:
        return False


def set_reference(fp, ref):
    for t in findall(fp, "fp_text"):
        if t[1] == "reference":
            t[2] = q(ref)


def retarget(fp, x, y, rot):
    """Move a footprint instance to (x, y, rot), fixing pad/text angles."""
    at = find(fp, "at")
    old_rot = float(at[3]) if at and len(at) > 3 else 0.0
    delta = rot - old_rot
    if at:
        fp.remove(at)
    layer_i = fp.index(find(fp, "layer"))
    new_at = ["at", f(x), f(y)] + ([f(rot % 360)] if rot % 360 else [])
    fp.insert(layer_i + 1, new_at)
    for item in findall(fp, "pad") + findall(fp, "fp_text"):
        a = find(item, "at")
        if not a:
            continue
        extras = [t for t in a[3:] if not _is_num(t)]
        nums = [t for t in a[3:] if _is_num(t)]
        pa = float(nums[0]) if nums else 0.0
        na = (pa + delta) % 360
        del a[3:]
        if na:
            a.append(f(na))
        a.extend(extras)


def set_nets(fp, mapping):
    """mapping: pad name -> net name (or None to clear)."""
    for pad in findall(fp, "pad"):
        name = pad[1].strip('"')
        old = find(pad, "net")
        if old:
            pad.remove(old)
        tgt = mapping.get(name)
        if tgt:
            t = find(pad, "tstamp")
            idx = pad.index(t) if t else len(pad)
            pad.insert(idx, net_node(tgt))


def strip_sch_links(fp):
    # "model" entries point at 3D shape files (.wrl/.STEP) that live in the
    # reference repo (one is even an absolute path on the original author's
    # machine) - drop them so KiCad doesn't warn about missing models.
    for key in ("path", "property", "model"):
        for n in findall(fp, key):
            fp.remove(n)


# ---------------- generated footprints ----------------

def smd_pad(name, x, y, w, h, side="F", shape="rect", net=None):
    layers = ["layers", q(f"{side}.Cu"), q(f"{side}.Paste"), q(f"{side}.Mask")]
    pad = ["pad", q(name), "smd", shape, ["at", f(x), f(y)],
           ["size", f(w), f(h)], layers]
    if net:
        pad.append(net_node(net))
    pad.append(ts())
    return pad


def fp_base(name, x, y, rot=0, side="F", attr="smd"):
    fp = ["footprint", q("pico-ball-deck:" + name),
          ["layer", q(f"{side}.Cu")],
          ts(),
          ["at", f(x), f(y)] + ([f(rot % 360)] if rot % 360 else []),
          ["attr", attr]]
    return fp


def fp_text(kind, text, x, y, layer, size=1.0, mirror=False):
    eff = ["effects", ["font", ["size", f(size), f(size)],
                       ["thickness", f(size * 0.15)]]]
    if mirror:
        eff.append(["justify", "mirror"])
    return ["fp_text", kind, q(text), ["at", f(x), f(y)], ["layer", q(layer)],
            eff, ts()]


def make_header(ref, x, y):
    """2x20 Pi header, pin 1 at (x, y), even row 2.54 toward -y (board edge)."""
    hdr_nets = {
        1: "+3V3", 17: "+3V3", 2: "+5V_SUGAR", 4: "+5V_SUGAR",
        3: "I2C_SDA", 5: "I2C_SCL", 8: "UART_TX", 10: "UART_RX",
        13: "LCD_RST", 18: "LCD_BL", 19: "LCD_MOSI", 22: "LCD_DC",
        23: "LCD_SCK", 24: "LCD_CS",
        29: "JOY_LEFT", 31: "JOY_UP", 33: "JOY_PRESS", 35: "JOY_DOWN",
        36: "KEY3", 37: "JOY_RIGHT", 38: "KEY2", 40: "KEY1",
        6: "GND", 9: "GND", 14: "GND", 20: "GND", 25: "GND", 30: "GND",
        34: "GND", 39: "GND",
    }
    fp = fp_base("PinHeader_2x20_P2.54mm_PiZero", x, y, attr="through_hole")
    fp.append(fp_text("reference", ref, 24.13, 2.2, "F.SilkS"))
    fp.append(fp_text("value", "Pi HAT header", 24.13, -4.8, "F.Fab"))
    for n in range(1, 41):
        col = (n - 1) // 2
        px = col * 2.54
        py = 0 if n % 2 else -2.54
        shape = "rect" if n == 1 else "oval"
        pad = ["pad", q(str(n)), "thru_hole", shape,
               ["at", f(px), f(py)], ["size", "1.7", "1.7"],
               ["drill", "1"], ["layers", q("*.Cu"), q("*.Mask")]]
        if n in hdr_nets:
            pad.append(net_node(hdr_nets[n]))
        pad.append(ts())
        fp.append(pad)
    return fp


def make_mount_hole(ref, x, y):
    fp = fp_base("MountingHole_2.75mm_M2.5", x, y, attr="through_hole")
    fp.append(["attr", "exclude_from_pos_files", "exclude_from_bom"])
    fp.append(fp_text("reference", ref, 0, -3.6, "F.SilkS", 0.8))
    fp.append(fp_text("value", "M2.5", 0, 3.6, "F.Fab", 0.8))
    fp.append(["pad", q(""), "np_thru_hole", "circle", ["at", "0", "0"],
               ["size", "2.75", "2.75"], ["drill", "2.75"],
               ["layers", q("*.Cu"), q("*.Mask")], ts()])
    fp.append(["fp_circle", ["center", "0", "0"], ["end", "2.75", "0"],
               ["stroke", ["width", "0.12"], ["type", "solid"]],
               ["fill", "none"], ["layer", q("F.SilkS")], ts()])
    return fp


def make_pogo_pad(ref, x, y, net, label):
    """Bottom-side bare copper target for a PiSugar pogo pin."""
    fp = fp_base("PiSugar_PogoPad_D2.2mm", x, y, side="B")
    fp.append(fp_text("reference", ref, 0, -2.2, "B.SilkS", 0.7, mirror=True))
    fp.append(fp_text("value", label, 0, 2.2, "B.SilkS", 0.7, mirror=True))
    fp.append(smd_pad("1", 0, 0, 2.2, 2.2, side="B", shape="circle", net=net))
    return fp


def make_two_pad(name, ref, value, x, y, pitch, pw, ph, net1, net2, side="F"):
    fp = fp_base(name, x, y, side=side)
    sl = ("B" if side == "B" else "F") + ".SilkS"
    fl = ("B" if side == "B" else "F") + ".Fab"
    fp.append(fp_text("reference", ref, 0, -ph / 2 - 1.1, sl, 0.8,
                      mirror=(side == "B")))
    fp.append(fp_text("value", value, 0, ph / 2 + 1.1, fl, 0.8,
                      mirror=(side == "B")))
    fp.append(smd_pad("1", -pitch / 2, 0, pw, ph, side=side, net=net1))
    fp.append(smd_pad("2", pitch / 2, 0, pw, ph, side=side, net=net2))
    return fp


# ---------------- board graphics ----------------

def gr_line(x1, y1, x2, y2, layer="Edge.Cuts", width=0.12):
    return ["gr_line", ["start", f(x1), f(y1)], ["end", f(x2), f(y2)],
            ["stroke", ["width", f(width)], ["type", "solid"]],
            ["layer", q(layer)], ts()]


def gr_arc(x1, y1, mx, my, x2, y2, layer="Edge.Cuts", width=0.12):
    return ["gr_arc", ["start", f(x1), f(y1)], ["mid", f(mx), f(my)],
            ["end", f(x2), f(y2)],
            ["stroke", ["width", f(width)], ["type", "solid"]],
            ["layer", q(layer)], ts()]


def gr_text(text, x, y, layer, size=1.0, mirror=False):
    eff = ["effects", ["font", ["size", f(size), f(size)],
                       ["thickness", f(size * 0.15)]]]
    if mirror:
        eff.append(["justify", "mirror"])
    return ["gr_text", q(text), ["at", f(x), f(y)], ["layer", q(layer)],
            eff, ts()]


# ---------------- build ----------------

def main():
    ref_pcb = parse_all((REF / "game-boy-zero-link-board.kicad_pcb").read_text())
    ref_fps = {}
    for c in findall(ref_pcb, "footprint"):
        ref_fps.setdefault(c[1].strip('"'), c)

    j1 = ref_fps["gb-link-socket:gb-link-socket"]
    bob = ref_fps["BOB-12009:BOB-12009"]
    pico_mod = parse_all((REF / "libraries" / "RP-Pico Libraries" /
                          "MCU_RaspberryPi_and_Boards.pretty" /
                          "RPi_Pico_SMD_TH_1.kicad_mod").read_text())

    # --- J1: GBC/GBA link edge connector, tab on left edge ---
    strip_sch_links(j1)
    set_reference(j1, "J1")
    retarget(j1, OX - 3.5, OY + 15.0, 270)  # ref board: 3.5mm out, was rot 90
    set_nets(j1, {"1": "GBVCC", "2": "SO", "3": "SI",
                  "4": "SD", "5": "SC", "6": "GND"})

    # --- U2: BOB-12009 level shifter breakout, HV side toward J1 ---
    strip_sch_links(bob)
    set_reference(bob, "U2")
    retarget(bob, OX + 6.5, OY + 15.0, 180)
    set_nets(bob, {"1": "PSC", "2": "PSI", "3": "+3V3", "4": "GND",
                   "5": "PSO", "6": "PSD", "7": "SC", "8": "SI",
                   "9": "GBVCC", "10": "GND", "11": "SO", "12": "SD"})

    # --- U1: Pico 2 W, USB flush at right board edge ---
    pico_mod[1] = q("pico-ball-deck:RPi_Pico2W_SMD_TH")
    for key in ("version", "generator"):
        n = find(pico_mod, key)
        if n:
            pico_mod.remove(n)
    strip_sch_links(pico_mod)
    set_reference(pico_mod, "U1")
    # footprint local -y = USB end; rot 270 points USB toward +x (right edge).
    # Position is constrained on three sides: the castellation pads extend
    # 2.65mm outward from each pin row, so the module sits 0.5mm lower than
    # the flush ideal (clears the J2 pad row by 0.39mm) and 1.0mm in from the
    # right edge (keeps pad 40's castellation 0.3mm off the r3 corner arc).
    # USB ends up recessed 1.0mm; the connector protrudes ~1.3mm past the
    # module edge, so it still clears the board edge.
    retarget(pico_mod, OX + 38.5, OY + 17.55, 270)
    # Pad surgery on our copy of the footprint:
    # - pins 34/36/37 lose their through-holes: the PiSugar GND pogo targets
    #   (PB3/PB4) land exactly on the back of this stretch of the bottom pin
    #   row, and a plated hole there would short 3V3/3V3_EN to the pogo pin.
    #   The module solders by castellation; the holes are only optional-header
    #   convenience. Their top-side SMD pads stay (3V3 routes from the top).
    # - pins 41-43 (SWD) lose their castellation extension pads, which crowd
    #   U2's pad column; the plain through-holes stay for debug wires.
    drop_tht = {"34", "36", "37"}
    drop_smd = {"41", "42", "43"}
    for pad in list(findall(pico_mod, "pad")):
        pname = pad[1].strip('"')
        if pname in drop_tht and pad[2] == "thru_hole":
            pico_mod.remove(pad)
        elif pname in drop_smd and pad[2] == "smd":
            pico_mod.remove(pad)
    # GPIO assignment chosen for routability: only GP0-3 (link) are fixed by
    # firmware; everything else lands as close to its J2 header pin as the
    # peripheral mux allows (I2C1 on GP14/15, UART0 on GP16/17, SPI1 on
    # GP10/11). GP18-21 (pins 24-27) are the spares / rev-B candidates.
    set_nets(pico_mod, {
        "1": "PSC", "2": "PSI", "4": "PSO", "5": "PSD",      # GP0-3 link
        "6": "JOY_PRESS", "7": "JOY_UP",                     # GP4 / GP5
        "9": "JOY_LEFT", "10": "KEY3",                       # GP6 / GP7
        "11": "LCD_CS", "12": "LCD_DC",                      # GP8 / GP9
        "14": "LCD_SCK", "15": "LCD_MOSI",                   # GP10 / GP11 SPI1
        "16": "LCD_RST", "17": "LCD_BL",                     # GP12 / GP13 (PWM)
        "19": "I2C_SDA", "20": "I2C_SCL",                    # GP14 / GP15 I2C1
        "21": "UART_TX", "22": "UART_RX",                    # GP16 / GP17 UART0
        "29": "KEY1",                                        # GP22
        "31": "JOY_RIGHT", "32": "KEY2", "34": "JOY_DOWN",   # GP26 / GP27 / GP28
        "3": "GND", "8": "GND", "13": "GND", "18": "GND", "23": "GND",
        "28": "GND", "33": "GND", "38": "GND",
        "36": "+3V3", "39": "VSYS", "40": "VBUS",
    })

    footprints = [j1, bob, pico_mod]

    # --- J2: 40-pin Pi HAT header, pin 1 at Pi position (8.37, 4.77) ---
    footprints.append(make_header("J2", OX + 8.37, OY + 4.77))

    # --- mounting holes (Pi Zero corners) ---
    # H4 (61.5, 26.5) is omitted entirely: it falls under U1 and its drill
    # would cut through the module's pads 38-40 -> 3-point mount.
    for i, (hx, hy) in enumerate(
            [(3.5, 3.5), (61.5, 3.5), (3.5, 26.5)], 1):
        footprints.append(make_mount_hole(f"H{i}", OX + hx, OY + hy))

    # --- PiSugar pogo pads (bottom copper; official Zero 2 W pad XY) ---
    for ref, bx, by, net, lbl in [
        ("PB1", 8.75, 11.05, "+5V_SUGAR", "5V"),
        ("PB2", 11.21, 6.30, "+5V_SUGAR", "5V"),
        ("PB3", 10.90, 3.69, "GND", "GND"),
        ("PB4", 17.29, 2.41, "GND", "GND"),
    ]:
        footprints.append(make_pogo_pad(ref, OX + 65 - bx, OY + 30 - by,
                                        net, lbl))

    # --- discretes ---
    footprints.append(make_two_pad("D_SMA", "D1", "SS34", OX + 25, OY + 20,
                                   4.0, 2.6, 1.6, "VSYS", "+5V_SUGAR",
                                   side="B"))
    footprints.append(make_two_pad("C_0805", "C1", "10uF", OX + 25, OY + 23.5,
                                   2.0, 1.3, 1.45, "VSYS", "GND", side="B"))
    footprints.append(make_two_pad("SolderJumper", "JP1", "VBUS->5V",
                                   OX + 31, OY + 20, 2.0, 1.6, 2.0,
                                   "VBUS", "+5V_SUGAR", side="B"))
    footprints.append(make_two_pad("R_0603", "R1", "4k7", OX + 8, OY + 26.5,
                                   1.65, 0.9, 0.95, "I2C_SDA", "+3V3"))
    footprints.append(make_two_pad("R_0603", "R2", "4k7", OX + 8, OY + 28.4,
                                   1.65, 0.9, 0.95, "I2C_SCL", "+3V3"))

    # --- outline: 65x30 r3 rounded rect + 6.5x6.5 link tab on left edge ---
    R, c = 3.0, 3.0 * (1 - 0.5 ** 0.5)  # corner radius, 45deg sagitta offset
    x0, y0, x1, y1 = OX, OY, OX + 65, OY + 30
    ty0, ty1, tx = OY + 15 - 3.25, OY + 15 + 3.25, OX - 6.5
    edges = [
        gr_line(x0 + R, y0, x1 - R, y0),                       # top
        gr_arc(x1 - R, y0, x1 - c, y0 + c, x1, y0 + R),        # TR
        gr_line(x1, y0 + R, x1, y1 - R),                       # right
        gr_arc(x1, y1 - R, x1 - c, y1 - c, x1 - R, y1),        # BR
        gr_line(x1 - R, y1, x0 + R, y1),                       # bottom
        gr_arc(x0 + R, y1, x0 + c, y1 - c, x0, y1 - R),        # BL
        gr_line(x0, y1 - R, x0, ty1),                          # left lower
        gr_line(x0, ty1, tx, ty1),                             # tab bottom
        gr_line(tx, ty1, tx, ty0),                             # tab tip
        gr_line(tx, ty0, x0, ty0),                             # tab top
        gr_line(x0, ty0, x0, y0 + R),                          # left upper
        gr_arc(x0, y0 + R, x0 + c, y0 + c, x0 + R, y0),        # TL
    ]

    texts = [
        gr_text("pico-ball deck rev A", OX + 32.5, OY + 15, "B.SilkS",
                1.2, mirror=True),
        gr_text("GB LINK", OX + 1.5, OY + 10.5, "F.SilkS", 0.8),
        gr_text("PiSugar pogo", OX + 47, OY + 23.5, "B.SilkS", 0.7,
                mirror=True),
    ]

    # --- assemble ---
    pcb = ["kicad_pcb", ["version", "20221018"], ["generator", "pcbnew"],
           ["general", ["thickness", "1.6"]],
           ["paper", q("A4")],
           find(ref_pcb, "layers"),
           ["setup", ["pad_to_mask_clearance", "0"],
            ["grid_origin", f(OX), f(OY)]],
           ["net", "0", q("")]]
    for name in NET_NAMES:
        pcb.append(net_node(name))
    pcb += footprints + edges + texts

    OUT.write_text(ser(pcb) + "\n")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, "
          f"{len(footprints)} footprints, {len(NET_NAMES)} nets)")


if __name__ == "__main__":
    main()
