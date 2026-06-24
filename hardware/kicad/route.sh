#!/usr/bin/env bash
# Full routing pipeline for pico-ball-deck (Docker, no local KiCad/Java needed):
#   generate -> export DSN -> Freerouting -> import SES + GND pours -> DRC
set -euo pipefail
cd "$(dirname "$0")"

KICAD="docker run --rm -v $PWD:/work -w /work kicad/kicad:8.0"
JAVA="docker run --rm -v $PWD:/work -w /work eclipse-temurin:21-jre"

echo "== 1/5 regenerate board =="
python3 generate_board.py

echo "== 1b/5 pre-route the hard nets =="
$KICAD python3 add_preroutes.py

echo "== 2/5 export Specctra DSN =="
$KICAD python3 export_dsn.py

echo "== 3/5 Freerouting =="
$JAVA bash -c "apt-get update -qq >/dev/null 2>&1 && \
  apt-get install -y -qq xvfb libxtst6 libxi6 libxrender1 libxext6 libxrandr2 fontconfig >/dev/null 2>&1 && \
  timeout 600 xvfb-run -a java -jar freerouting.jar \
    -de pico-ball-deck.dsn -do pico-ball-deck.ses -mp 50 -da" \
  2>&1 | tee freerouting.log | grep -E "Auto-routing|optimization|Saving|WARN|ERROR" || true
test -s pico-ball-deck.ses

echo "== 4/5 import SES + GND pours =="
$KICAD python3 import_ses.py

echo "== 5/5 DRC =="
$KICAD kicad-cli pcb drc --severity-error -o drc.rpt pico-ball-deck.kicad_pcb || true
grep -E "Found" drc.rpt

# KiCad 8 (container) writes a .kicad_prl whose visible_items IDs KiCad 9/10
# misread as "hide tracks/pads/zones" -> board opens looking empty. Delete it;
# the GUI regenerates a correct one with everything visible.
rm -f pico-ball-deck.kicad_prl
