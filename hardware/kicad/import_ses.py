"""Import the Freerouting session back into the board, add GND pours on both
copper layers, fill zones, save as pico-ball-deck.kicad_pcb (in place).
Run inside the kicad/kicad container."""
import pcbnew

PCB = "pico-ball-deck.kicad_pcb"
board = pcbnew.LoadBoard(PCB)

ok = pcbnew.ImportSpecctraSES(board, "pico-ball-deck.ses")
print("SES import:", "OK" if ok else "FAILED")
if not ok:
    raise SystemExit(1)

# GND pour on both layers, covering the whole outline (zone clips to Edge.Cuts
# minus edge clearance automatically at fill time via the board's outline).
gnd = board.GetNetsByName()["GND"]
bbox = board.GetBoardEdgesBoundingBox()
pts = [
    (bbox.GetLeft(), bbox.GetTop()),
    (bbox.GetRight(), bbox.GetTop()),
    (bbox.GetRight(), bbox.GetBottom()),
    (bbox.GetLeft(), bbox.GetBottom()),
]
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(gnd)
    z.SetZoneName("GND_" + pcbnew.LayerName(layer))
    outline = z.Outline()
    outline.NewOutline()
    for x, y in pts:
        outline.Append(pcbnew.VECTOR2I(int(x), int(y)))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetLocalClearance(pcbnew.FromMM(0.25))
    z.SetMinThickness(pcbnew.FromMM(0.25))
    z.SetThermalReliefGap(pcbnew.FromMM(0.4))
    z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.4))
    # isolated copper islands are unconnected by definition -> drop them,
    # otherwise DRC flags zone-to-zone "missing connection" on the orphans
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(z)

filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())

pcbnew.SaveBoard(PCB, board)
print("saved", PCB)
