#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds build/dinput8.dll + build/TWAUWidescreen.ini with clang-cl + lld-link (LLVM): no C runtime, no
# Windows SDK. Requirements: clang, lld, llvm (llvm-dlltool), python3. Linux or Windows.
#
# Build type follows the version in src/twauws.c (override with --dev / --release):
#   X.Y.Z-dev.N -> dev build: speed + screenshot hotkeys on, detailed log
#   X.Y.Z       -> release build: those off, normal log (what players get)
set -euo pipefail
cd "$(dirname "$0")"
VERSION=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' src/twauws.c)
DEV=0
[[ "$VERSION" == *-dev* ]] && DEV=1
[ "${1:-}" = "--dev" ] && DEV=1
[ "${1:-}" = "--release" ] && DEV=0
mkdir -p build
llvm-dlltool -m i386:x86-64 -d src/kernel32.def -l build/kernel32.lib
clang-cl --target=x86_64-pc-windows-msvc /nologo /O2 /GS- /W3 /clang:-ffreestanding /clang:-fno-builtin \
  /DWO_DEV=$DEV /c src/twauws.c /Fobuild/twauws.obj
lld-link /nologo /dll /entry:DllMain /nodefaultlib /subsystem:windows /machine:x64 /opt:ref \
  /out:build/dinput8.dll build/twauws.obj build/kernel32.lib

# the ini in this folder holds the release defaults; a dev build gets the dev values
python3 - TWAUWidescreen.ini build/TWAUWidescreen.ini "$DEV" <<'PY'
import sys
src, dst, dev = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
dev_values = {("General", "Log"): "2", ("General", "LogKeep"): "30",
              ("Speed", "Enabled"): "1", ("Screenshots", "Enabled"): "1"}
out, sec = [], ""
for line in open(src, newline=""):
    s = line.strip()
    if s.startswith("[") and s.endswith("]"):
        sec = s[1:-1]
    elif dev and "=" in s and not s.startswith(";"):
        key = s.split("=", 1)[0].strip()
        if (sec, key) in dev_values:
            line = f"{key}={dev_values[(sec, key)]}\r\n" if line.endswith("\r\n") else f"{key}={dev_values[(sec, key)]}\n"
    out.append(line)
open(dst, "w", newline="").write("".join(out))
PY
echo "$VERSION $([ $DEV = 1 ] && echo dev || echo release) build: build/dinput8.dll + build/TWAUWidescreen.ini"
