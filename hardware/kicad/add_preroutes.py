"""Pre-route the nets Freerouting consistently fails, on the freshly
generated board BEFORE the autorouter runs. On the empty board each has a
verified single-layer path threading the 0.84 mm pad gaps; clearances were
hand-checked at >= 0.275 mm copper / >= 0.425 mm edge.

The hard set (everything else is left to the router, which handles it fine):
- KEY1/KEY2/JOY_RIGHT/JOY_DOWN: bottom-row -> header right cluster climbers.
  The router walls them off with its east-west buses if left to its own.
- UART_TX: the left-corridor crossing between U2's column and U1 pin 20
  (no gap exists at row height - U2 pad 1 and U1 pin 20 leave only 0.71 mm).
- +3V3 header pin 1 -> pin 17 link through the inter-row channel, so the
  net doesn't depend on the router finding a second crossing.

Also drops GND stitching vias in known-open spots: the F.Cu pour splits into
disjoint fragments (each holding GND pads, so island removal keeps them) and
the vias tie every fragment through the B.Cu plane. Placing them before
routing means the router avoids them.

Run inside the kicad/kicad container, after generate_board.py.
"""
import pcbnew

PCB = "pico-ball-deck.kicad_pcb"
board = pcbnew.LoadBoard(PCB)
nets = board.GetNetsByName()

W = int(0.25e6)  # Default-class track width, nm


def mm(v):
    return int(round(v * 1e6))


# net -> (layer, polyline points in mm)
ROUTES = {
    # U1 pin29 (B) -> bottom edge strip below the pogo pads, up the right
    # side with a jog around the module's two NPTH mounting holes at
    # x=109.47, through the pin2/3 gap (x=108.82) into J2 pad40 from the right.
    "KEY1": ("B.Cu", [
        (84.69, 76.44), (86.04, 77.79), (87.50, 79.25), (107.32, 79.25),
        (108.82, 77.75), (108.82, 72.10), (108.00, 71.28), (108.00, 63.82),
        (108.82, 63.00), (108.82, 52.23), (106.63, 52.23),
    ]),
    # U1 pin31 (B) -> gap lane x=91.04, east channel y=56.75 under the J2
    # pads, up the pin4/5 gap (x=103.74) into pad37.
    "JOY_RIGHT": ("B.Cu", [
        (89.77, 76.44), (91.04, 75.17), (91.04, 56.75), (103.19, 56.75),
        (103.74, 56.20), (103.74, 55.45), (104.09, 55.10), (104.09, 54.77),
    ]),
    # U1 pin32 (B) -> gap lane x=93.58, east channel y=57.35 (nests south of
    # JOY_RIGHT's), then the 37/39 + 38/40 column gaps (x=105.36) into pad38.
    "KEY2": ("B.Cu", [
        (92.31, 76.44), (93.58, 75.17), (93.58, 57.35), (104.81, 57.35),
        (105.36, 56.80), (105.36, 52.23), (104.09, 52.23),
    ]),
    # U1 pad34 is SMD-only (pogo clearance) so this stays on F.Cu: jog east
    # below the pin row, climb the pin5/6 castellation gap (x=101.20), end
    # inside pad35. (The x=98.66 gap is too far west: no F.Cu channel exists
    # between the castellation band and the J2 pads to get back east.)
    "JOY_DOWN": ("F.Cu", [
        (97.39, 77.34), (97.39, 75.40), (98.62, 74.17), (99.97, 74.17),
        (101.20, 72.94), (101.20, 54.90),
    ]),
    # U1 pin21 (B) -> west lane x=63.30 between U2's column and the SWD
    # holes; cross U2's column between its pads 1/2 (y=59.95), down between
    # J2 columns 3/4 and 5/6 (x=62.18), east along the inter-row channel
    # (y=53.50), into pad8.
    "UART_TX": ("B.Cu", [
        (64.37, 76.44), (63.30, 75.37), (63.30, 60.65), (62.60, 59.95),
        (60.60, 59.95), (60.60, 57.40), (62.18, 55.82), (62.18, 53.50),
        (65.10, 53.50), (65.99, 52.61), (65.99, 52.23),
    ]),
    # J2 pin1 -> pin17 (both +3V3) along the F.Cu inter-row channel, exiting
    # through the col0/1 and col7/8 gaps. Gives the net its second anchor.
    "+3V3": ("F.Cu", [
        (58.37, 54.77), (59.64, 53.50), (77.42, 53.50), (78.69, 54.77),
    ]),
}

# PSC/PSO: the long U1 -> U2 link-bus nets. Both run west on F.Cu just
# below the top pin row and hop to B.Cu for ~3 mm to cross JOY_DOWN's fixed
# F.Cu vertical at x=101.20. PSC ends with a dip under U1 pin 20 (the only
# way into U2 pad1: the pin20/U2 gap at row height is 0.71 mm, unroutable);
# PSO threads between the module NPTH at (67.53, 69.975) and the SWD holes.
# PSC takes the north lane (y=61.0) and PSO the south lane (y=62.3): PSO's
# southbound dive at x=69.3 then starts below PSC's lane and never crosses
# it, and the two via columns are staggered (>=0.83 mm apart).
MULTILAYER = {
    "PSO": {
        "F.Cu": [
            [(105.01, 58.66), (105.01, 60.36), (103.07, 62.30),
             (102.30, 62.30)],
            [(99.40, 62.30), (69.30, 62.30), (69.30, 68.50), (66.60, 68.50),
             (65.30, 68.86), (61.96, 68.84)],
        ],
        "B.Cu": [
            [(102.30, 62.30), (99.40, 62.30)],
        ],
        "vias": [(102.30, 62.30), (99.40, 62.30)],
    },
    "PSC": {
        "F.Cu": [
            [(112.63, 58.66), (112.63, 59.89), (111.52, 61.00),
             (106.50, 61.00)],
            [(98.60, 61.00), (66.26, 61.00), (65.31, 59.95), (62.66, 59.95),
             (61.96, 59.25), (61.96, 58.68)],
        ],
        "B.Cu": [
            [(106.50, 61.00), (98.60, 61.00)],
        ],
        "vias": [(106.50, 61.00), (98.60, 61.00)],
    },
    # PSI rides the third lane (y=60.2 - the U1 THT pads are SQUARES, so
    # their copper reaches y=59.51 across the full pad width; 60.2 keeps the
    # vias 0.34 mm clear of them and 0.325 mm clear of PSC's lane). Hop 1
    # (B) starts east of PSO's pin-4 exit stub and crosses both that stub
    # and JOY_DOWN's F vertical at x=101.2 on the back; hop 2 drops south
    # over PSC's west-end wall at x=67.3, then runs y=61.9 into U2 pad2.
    "PSI": {
        "F.Cu": [
            [(110.09, 58.66), (110.09, 59.45), (109.44, 60.20),
             (105.86, 60.20)],
            [(100.20, 60.20), (67.30, 60.20)],
            [(67.30, 61.90), (63.10, 61.90), (62.42, 61.22),
             (61.96, 61.22)],
        ],
        "B.Cu": [
            [(105.86, 60.20), (100.20, 60.20)],
            [(67.30, 60.20), (67.30, 61.90)],
        ],
        "vias": [(105.86, 60.20), (100.20, 60.20), (67.30, 60.20),
                 (67.30, 61.90)],
    },
}

# GND stitching vias (F pour fragments <-> B plane), all in verified-open
# spots >= 0.31 mm from any pad/hole/edge and clear of the fixed routes.
STITCH_VIAS = [
    (57.10, 50.72), (74.88, 50.72), (95.20, 50.72), (110.00, 50.72),
    (67.50, 73.50), (87.00, 69.00), (103.00, 64.00), (114.20, 67.00),
    (52.00, 79.00), (51.00, 53.00),
]

for netname, (layername, pts) in ROUTES.items():
    net = nets[netname]
    layer = board.GetLayerID(layername)
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        t.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        t.SetWidth(W)
        t.SetLayer(layer)
        t.SetNet(net)
        board.Add(t)
    print(f"pre-routed {netname}: {len(pts)-1} segs on {layername}")

def add_via(netname, x, y):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    v.SetDrill(int(0.35e6))
    v.SetWidth(int(0.7e6))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(nets[netname])
    board.Add(v)


for netname, spec in MULTILAYER.items():
    net = nets[netname]
    nseg = 0
    for layername in ("F.Cu", "B.Cu"):
        layer = board.GetLayerID(layername)
        for pts in spec[layername]:
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
                t.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
                t.SetWidth(W)
                t.SetLayer(layer)
                t.SetNet(net)
                board.Add(t)
                nseg += 1
    for x, y in spec["vias"]:
        add_via(netname, x, y)
    print(f"pre-routed {netname}: {nseg} segs, {len(spec['vias'])} vias")

gnd = nets["GND"]
for x, y in STITCH_VIAS:
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    v.SetDrill(int(0.35e6))
    v.SetWidth(int(0.7e6))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(gnd)
    board.Add(v)
print(f"added {len(STITCH_VIAS)} GND stitching vias")

pcbnew.SaveBoard(PCB, board)
print("saved", PCB)
