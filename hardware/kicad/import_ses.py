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

# Keep pours off the J1 link tab (the plug slides over it; the proven weimanc
# board keeps its tab copper-free past the base). Tab: x 43.5..50, y 61.75..68.25.
ko = pcbnew.ZONE(board)
ko.SetIsRuleArea(True)
ko.SetDoNotAllowCopperPour(True)
ko.SetDoNotAllowTracks(False)
ko.SetDoNotAllowVias(False)
ko.SetDoNotAllowPads(False)
ko.SetDoNotAllowFootprints(False)
ko.SetZoneName("link_tab_no_pour")
ls = pcbnew.LSET()
ls.AddLayer(pcbnew.F_Cu)
ls.AddLayer(pcbnew.B_Cu)
ko.SetLayerSet(ls)
ko_outline = ko.Outline()
ko_outline.NewOutline()
for x, y in ((42.0, 61.0), (50.0, 61.0), (50.0, 69.0), (42.0, 69.0)):
    ko_outline.Append(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
board.Add(ko)

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

# Footprints copied from the reference repo embed 3D model paths that don't
# exist here (one is an absolute Windows path) -> KiCad warns on every open.
for fp in board.GetFootprints():
    fp.Models().clear()

pcbnew.SaveBoard(PCB, board)
print("saved", PCB)
