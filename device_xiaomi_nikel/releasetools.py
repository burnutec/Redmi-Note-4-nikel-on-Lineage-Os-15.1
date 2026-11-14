# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# nikel: also flash the boot logo (MTK "logo" partition) from the ROM zip.
# Only the logo is written here; no other firmware partition is touched.

import common

LOGO_DEV = "/dev/block/platform/mtk-msdc.0/11230000.msdc0/by-name/logo"


def FullOTA_InstallEnd(info):
  AddLogo(info, info.input_zip)


def IncrementalOTA_InstallEnd(info):
  AddLogo(info, info.target_zip)


def AddLogo(info, input_zip):
  try:
    data = input_zip.read("RADIO/logo.bin")
  except KeyError:
    print("nikel releasetools: no RADIO/logo.bin, logo not added")
    return
  common.ZipWriteStr(info.output_zip, "firmware-update/logo.bin", data)
  info.script.Print("Flashing boot logo...")
  info.script.AppendExtra('package_extract_file("firmware-update/logo.bin", "%s");' % LOGO_DEV)
