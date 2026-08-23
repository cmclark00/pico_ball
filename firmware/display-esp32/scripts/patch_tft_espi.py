"""Install the exact Hosyond E32R32P panel init into TFT_eSPI."""

from pathlib import Path
import shutil
import subprocess
import sys

Import("env")  # type: ignore[name-defined]  # supplied by PlatformIO/SCons
pio_env = globals()["env"]

project = Path(pio_env.subst("$PROJECT_DIR"))
source = project / "patches" / "ST7789_Init.h"
target = (Path(pio_env.subst("$PROJECT_LIBDEPS_DIR")) / pio_env.subst("$PIOENV") /
          "TFT_eSPI" / "TFT_Drivers" / "ST7789_Init.h")

if not source.is_file():
    raise RuntimeError(f"missing E32R32P panel init: {source}")
if not target.is_file():
    # PlatformIO evaluates pre-scripts before its normal dependency install.
    # Install the pinned package now so a cold checkout behaves like a warm one.
    subprocess.run([
        sys.executable, "-m", "platformio", "pkg", "install",
        "--library", "bodmer/TFT_eSPI@2.5.43",
        "--project-dir", str(project),
    ], check=True)
if not target.is_file():
    raise RuntimeError(f"TFT_eSPI layout changed; expected {target}")
if target.read_bytes() != source.read_bytes():
    shutil.copyfile(source, target)
    print("Patched TFT_eSPI with the Hosyond E32R32P ST7789P3 init sequence")
