#!/bin/bash
#
# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

set -e

DEVICE=nikel
VENDOR=xiaomi
INITIAL_COPYRIGHT_YEAR=2026

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

setup_vendor "$DEVICE" "$VENDOR" "$LINEAGE_ROOT"

write_headers
write_makefiles "$MY_DIR"/proprietary-files.txt
write_makefiles "$MY_DIR"/proprietary-files-nikel.txt
write_footers
