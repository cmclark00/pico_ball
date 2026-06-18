#!/usr/bin/env bash
# Build the host pccs_convert binary from the vendored PCCS library
# (third_party/PCCS, cloned by scripts/setup.sh). PCCS is GBA-first; this builds
# its host (ON_GBA=false) path and links tools/pccs_convert.cpp against it.
#
# Output: third_party/PCCS/pccs_convert  (gitignored; host/picovault/pccs.py runs it)
#
# We don't use PCCS's own `make lib` because its shared-library step uses the GNU
# linker flag -soname, which Apple's ld rejects. Instead we reuse only its table
# generator + bundled bin2s, then compile/link the host objects ourselves. On
# macOS (Mach-O) C symbols carry a leading underscore that devkitPro's bin2s does
# not emit, so we rewrite the data-table labels there.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PCCS_DIR="$REPO_ROOT/third_party/PCCS"
CLI_SRC="$REPO_ROOT/tools/pccs_convert.cpp"
OUT="$PCCS_DIR/pccs_convert"

if [ ! -f "$PCCS_DIR/include/PokeBox.h" ]; then
  echo "PCCS not found at $PCCS_DIR — run scripts/setup.sh first." >&2
  exit 1
fi

CXX="${CXX:-c++}"
CC="${CC:-cc}"
CXXFLAGS="-O2 -std=c++17"

cd "$PCCS_DIR"
echo "==> Generating PCCS data tables + bin2s"
make generate_tables bin2s_tool >/dev/null
mkdir -p "$PCCS_DIR/build"

HB="$PCCS_DIR/hostbuild"
rm -rf "$HB"; mkdir -p "$HB"
INC="-I$PCCS_DIR/include -I$PCCS_DIR/build"
underscore=""
[ "$(uname)" = "Darwin" ] && underscore="yes"
objs=()

echo "==> Embedding data tables"
BIN2S="$PCCS_DIR/tools/bin2s/bin2s"
for b in "$PCCS_DIR"/data/*.bin; do
  base="$(basename "${b%.bin}")"
  s="$HB/${base}.bin.s"
  # -H writes the extern header into build/ (included by the sources via -Ibuild);
  # this must happen before the sources are compiled.
  ( cd "$PCCS_DIR/build" && "$BIN2S" -a 4 -H "${base}_bin.h" "$b" >/dev/null )
  if [ -n "$underscore" ]; then
    "$BIN2S" -a 4 "$b" \
      | sed -E 's/^([[:space:]]*)\.global[[:space:]]+([A-Za-z_])/\1.global _\2/; s/^([A-Za-z_][A-Za-z0-9_]*):/_\1:/' > "$s"
  else
    "$BIN2S" -a 4 "$b" > "$s"
  fi
  o="$HB/${base}.bin.o"
  "$CC" -x assembler-with-cpp -c "$s" -o "$o"
  objs+=("$o")
done

echo "==> Compiling PCCS sources (host, ON_GBA=false)"
for f in source/*.cpp; do
  o="$HB/$(basename "${f%.cpp}").o"
  "$CXX" $CXXFLAGS $INC -c "$f" -o "$o"
  objs+=("$o")
done

echo "==> Linking pccs_convert"
"$CXX" $CXXFLAGS $INC "$CLI_SRC" "${objs[@]}" -o "$OUT"
echo "    built $OUT"
