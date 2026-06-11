"""Export Specctra DSN from the placed board (run inside the kicad/kicad container)."""
import pcbnew

board = pcbnew.LoadBoard("pico-ball-deck.kicad_pcb")
ok = pcbnew.ExportSpecctraDSN(board, "pico-ball-deck.dsn")
print("DSN export:", "OK" if ok else "FAILED")
raise SystemExit(0 if ok else 1)
