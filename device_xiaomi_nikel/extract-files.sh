#!/bin/bash
#
# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Builds vendor/xiaomi/nikel from two sources:
#   1) SHIFT's vendor image, unpacked (e.g. ~/nikel/shift-vendor)  -> proprietary-files.txt
#   2) nikel stock MIUI /system copy (e.g. .../stock-3x64)          -> proprietary-files-nikel.txt
#
# Usage:  ./extract-files.sh <unpacked-shift-vendor-dir> <stock-3x64-dir>
#

set -e

DEVICE=nikel
VENDOR=xiaomi

MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "$MY_DIR" ]]; then MY_DIR="$PWD"; fi
MY_DIR="$(cd "$MY_DIR" && pwd)"

LINEAGE_ROOT="$MY_DIR"/../../..
HELPER="$LINEAGE_ROOT"/vendor/lineage/build/tools/extract_utils.sh
if [ ! -f "$HELPER" ]; then
    echo "Unable to find helper script at $HELPER"
    exit 1
fi
. "$HELPER"

if [ $# -ne 2 ] || [ ! -f "$1/build.prop" ] || [ ! -d "$2/system" ]; then
    echo "Usage: $0 <unpacked-shift-vendor-dir> <stock-3x64-dir>"
    echo "  first:  folder with SHIFT's vendor files (has build.prop, lib/, bin/ ...)"
    echo "  second: folder that contains system/ copied from the MIUI phone"
    exit 1
fi
SHIFT_VENDOR="$(cd "$1" && pwd)"
NIKEL_STOCK="$(cd "$2" && pwd)"

# extract_utils looks files up relative to /system, with or without a
# leading "system/" depending on the entry - provide both layouts via links.
LINKS="$(mktemp -d)"
trap 'rm -r "$LINKS"' EXIT
mkdir -p "$LINKS/shift/system" "$LINKS/nikel"
ln -s "$SHIFT_VENDOR" "$LINKS/shift/vendor"
ln -s "$SHIFT_VENDOR" "$LINKS/shift/system/vendor"
ln -s "$NIKEL_STOCK/system" "$LINKS/nikel/system"
for d in app bin etc lib lib64 vendor; do
    [ -e "$NIKEL_STOCK/system/$d" ] && ln -s "$NIKEL_STOCK/system/$d" "$LINKS/nikel/$d"
done

# Fresh vendor/xiaomi/nikel, then both extractions into it
setup_vendor "$DEVICE" "$VENDOR" "$LINEAGE_ROOT" false true

echo "== SHIFT5me ShiftOS 8.1 vendor (SoC)"
extract "$MY_DIR"/proprietary-files.txt "$LINKS/shift"
echo "== nikel stock MIUI (Trustonic)"
extract "$MY_DIR"/proprietary-files-nikel.txt "$LINKS/nikel"

echo "== nikel fix-ups"
python3 "$MY_DIR"/camera/patch_libcameracustom.py "$LINEAGE_ROOT"/vendor/"$VENDOR"/"$DEVICE"/proprietary

"$MY_DIR"/setup-makefiles.sh
