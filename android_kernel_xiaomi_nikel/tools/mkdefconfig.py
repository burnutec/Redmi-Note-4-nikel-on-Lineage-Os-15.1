"""Derive nikel_defconfig from SHIFT5me_defconfig with an explicit, commented edit list.

Usage: python3 mkdefconfig.py SHIFT5me_defconfig nikel_defconfig
Every change is listed in EDITS with the reason; anything not listed is SHIFT's value.
"""
import re
import sys

# (option, new value or None to disable, reason)
EDITS = [
    # --- identity / board selection -------------------------------------
    ("LOCALVERSION", '"-nikel"', "kernel version suffix"),
    ("SHIFT5ME_PROJECT", None, "SHIFT5me-only code paths; check git grep before re-enabling anything"),
    ("ARCH_MTK_PROJECT", '"nikel"', "project name (no nikel.dws exists, so DrvGen is skipped)"),
    ("BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES", '"nikel"', "builds arch/arm64/boot/dts/nikel.dts and appends it"),
    ("BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES", None, "nikel's LK has no odmdtbo partition"),
    ("BUILD_ARM64_DTB_OVERLAY_IMAGE", None, "no overlay image"),
    ("MTK_DTBO_FEATURE", None, "no overlay: single merged DTB like nikel stock"),
    ("MTK_AB_OTA_UPDATER", None, "nikel has no A/B partitions (also drops slotselect from DT fstab)"),
    # --- display ----------------------------------------------------------
    ("CUSTOM_KERNEL_LCM", '"nt35596_fhd_tianma_phantom"', "this phone's panel (lcm=1-nt35596_fhd_tianma_phantom)"),
    ("LCM_HEIGHT", '"1920"', "panel"),
    ("LCM_WIDTH", '"1080"', "panel"),
    ("MTK_LCM_DEVICE_TREE_SUPPORT", None, "keeps lcm_i2c.c out (it would not conflict, but is unused)"),
    # --- DANGEROUS on nikel: same I2C addresses, different chips -----------
    ("MTK_BQ25896_SUPPORT", None, "DANGER: SHIFT charger driver @0x6b; nikel's BQ24261 is also @0x6b with a different register map"),
    ("MTK_PUMP_EXPRESS_PLUS_SUPPORT", None, "DANGER: fast-charge up to 12 V; nikel never had it"),
    ("MTK_PUMP_EXPRESS_PLUS_20_SUPPORT", None, "DANGER: as above"),
    ("TOUCHSCREEN_MTK_FOCALTECH_TS", None, "DANGER: FT8716 driver @0x38 may auto-flash its firmware into nikel's FT5346"),
    # --- touch: nikel FT5346 @ i2c4 0x38 -------------------------------------
    ("TOUCHSCREEN_MTK_FT5X0X", "y", "enables drivers/.../ft5x0x/, whose Makefile the overlay replaces to build ONLY nikel_ft5346.c (read-only, no firmware upgrade)"),
    ("FT_AUTO_UPGRADE_SUPPORT", None, "never flash touch firmware (SHIFT's ft5x0x_upgrade.c carries FT5436 firmware)"),
    ("CUST_FTS_APK_DEBUG", None, "no user-space register/flash access channel"),
    ("TPD_HAVE_CALIBRATION", None, "not used by nikel"),
    # --- charging: nikel = TI BQ24296 @0x6b (stock driver "bq24296"), classic gauge
    ("MTK_BQ24296_SUPPORT", "y", "nikel's charger (stock binds bq24296 @0-006b); SHIFT MT6735 driver + Longcheer deltas"),
    ("MTK_HAFG_20", None, "nikel stock uses the classic gauge (no fg_20 in stock kernel); nikel DT tables are classic"),
    ("MTK_KERNEL_POWER_OFF_CHARGING", None, "needs a charging animation in the boot ramdisk; later"),
    # MTK_SMART_BATTERY and MTK_JEITA_STANDARD_SUPPORT stay on (SHIFT values; stock nikel uses JEITA too)
    # --- hardware nikel does not have --------------------------------------
    ("USB_C_SWITCH_FUSB302", None, "nikel is micro-USB"),
    ("USB_POWER_DELIVERY", None, "nikel is micro-USB"),
    ("MTK_HDMI_SUPPORT", None, "MediaTek reference leftover, no HDMI chip"),
    ("MTK_HDMI_HDCP_SUPPORT", None, "as above"),
    ("CUSTOM_KERNEL_HDMI", None, "as above"),
    ("CUSTOM_KERNEL_BAROMETER", None, "nikel has no barometer"),
    ("MTK_LPS25HB", None, "SHIFT5me barometer driver"),
    # --- sensors: SHIFT5me chips, nikel has different ones -----------------
    ("MTK_ICM206XX_A", None, "SHIFT5me accel; nikel uses BMI160/LSM6 (later)"),
    ("MTK_ICM206XX_G", None, "SHIFT5me gyro; as above"),
    ("MTK_STK3X1X_NEW", None, "SHIFT5me light/prox; nikel uses LTR5xx/APDS (later)"),
    ("CUSTOM_KERNEL_ACCELEROMETER", None, "no nikel driver selected yet"),
    ("CUSTOM_KERNEL_GYROSCOPE", None, "no nikel driver selected yet"),
    ("CUSTOM_KERNEL_ALSPS", None, "no nikel driver selected yet"),
    # MTK_AKM09911 stays: nikel has an AKM09911 (mag_akm09911 node in stock DT)
    # --- cameras: off for the first boot ------------------------------------
    ("MTK_IMGSENSOR", None, "first boot: camera power code is per-board, could drive SHIFT GPIOs"),
    ("CUSTOM_KERNEL_IMGSENSOR", None, "as above"),
    ("MTK_CAM_CAL", None, "as above"),
    ("CUSTOM_KERNEL_CAM_CAL_DRV", None, "as above"),
    ("MTK_LENS", None, "as above"),
    ("MTK_FLASHLIGHT", None, "first boot: flash driver differs (nikel LM3643/KTD2)"),
    ("CUSTOM_KERNEL_FLASHLIGHT", None, "as above"),
    # --- nikel extras --------------------------------------------------------
    ("MTK_IRTX_SUPPORT", "y", "nikel IR blaster (in Ruben's nikel defconfig)"),
]

# options whose dependents must also go (Kconfig "lens" sub-options etc.)
DROP_PREFIXES = ("MTK_LENS_",)


def main(src, dst):
    lines = open(src).read().splitlines()
    names = {o for o, _, _ in EDITS}
    out, seen = [], set()
    for line in lines:
        m = re.match(r"^(?:#\s*)?CONFIG_([A-Za-z0-9_]+)(?:=| is not set)", line)
        opt = m.group(1) if m else None
        if opt and opt.startswith(DROP_PREFIXES):
            continue
        if opt in names:
            if opt in seen:
                continue
            seen.add(opt)
            _, val, _ = next(e for e in EDITS if e[0] == opt)
            out.append(f"CONFIG_{opt}={val}" if val else f"# CONFIG_{opt} is not set")
            continue
        out.append(line)
    added = [e for e in EDITS if e[0] not in seen]
    if added:
        out.append("")
        out.append("# ---- nikel additions ----")
        for opt, val, _ in added:
            out.append(f"CONFIG_{opt}={val}" if val else f"# CONFIG_{opt} is not set")
    open(dst, "w").write("\n".join(out) + "\n")

    with open(dst + ".changes.md", "w") as f:
        f.write("# nikel_defconfig vs SHIFT5me_defconfig\n\n")
        f.write("Everything not listed here is SHIFT's value.\n\n| Option | SHIFT5me | nikel | Why |\n|---|---|---|---|\n")
        src_vals = {}
        for line in lines:
            m = re.match(r"^CONFIG_([A-Za-z0-9_]+)=(.*)$", line)
            if m:
                src_vals[m.group(1)] = m.group(2)
        for opt, val, why in EDITS:
            f.write(f"| `{opt}` | {src_vals.get(opt, '(unset)')} | {val or 'off'} | {why} |\n")
        f.write(f"\nAlso dropped: every `CONFIG_{DROP_PREFIXES[0]}*` lens sub-option (with MTK_LENS).\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
