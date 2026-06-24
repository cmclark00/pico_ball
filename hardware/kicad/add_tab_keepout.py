"""Keep the GND pours off the J1 link tab (the plug slides over it; the
proven weimanc board keeps its tab copper-free past the base). Adds a
no-copper-pour rule area over the tab on both layers and refills the zones.
Run inside the kicad/kicad container."""
import pcbnew

PCB = "pico-ball-deck.kicad_pcb"
board = pcbnew.LoadBoard(PCB)

# Tab: x 43.5..50.0 (tip..base), y 61.75..68.25. Cover it with margin past
# the tip and sides; stop exactly at the board body edge (x = 50.0).
X0, X1 = pcbnew.FromMM(42.0), pcbnew.FromMM(50.0)
Y0, Y1 = pcbnew.FromMM(61.0), pcbnew.FromMM(69.0)

z = pcbnew.ZONE(board)
z.SetIsRuleArea(True)
z.SetDoNotAllowCopperPour(True)
z.SetDoNotAllowTracks(False)
z.SetDoNotAllowVias(False)
z.SetDoNotAllowPads(False)
z.SetDoNotAllowFootprints(False)
z.SetZoneName("link_tab_no_pour")
ls = pcbnew.LSET()
ls.AddLayer(pcbnew.F_Cu)
ls.AddLayer(pcbnew.B_Cu)
z.SetLayerSet(ls)
outline = z.Outline()
outline.NewOutline()
for x, y in ((X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)):
    outline.Append(pcbnew.VECTOR2I(int(x), int(y)))
board.Add(z)

filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())

pcbnew.SaveBoard(PCB, board)
print("saved", PCB, "with link-tab pour keepout")
