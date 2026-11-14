#!/bin/bash
# nikel kernel: one-shot setup + build inside WSL (Ubuntu 22.04).
#
# Usage: keep this script and nikel-kernel-overlay.tar.gz in the same folder
# (Windows or Linux side), then run it from anywhere:
#   bash nikel_setup_and_build.sh          (from inside that folder)
#   bash /path/to/folder/nikel_setup_and_build.sh
# Results (kernel image or build log) are written back to that same folder.
#
# Safe to run again: finished steps are skipped. Nothing here touches a phone.
set -e

# The folder this script sits in; the overlay is expected next to it.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OVERLAY=$HERE/nikel-kernel-overlay.tar.gz
WORK=$HOME/nikel
KERNEL=$WORK/android_kernel_shift_mt6797
TC_DIR=$WORK/toolchain
# Compiler, first one that works:
#  1. Linaro GCC 6.3.1 (2017.05) - same GCC as SHIFT's 2017.02 build.
#     releases.linaro.org is retired, so a git copy is used.
#  2. AOSP GCC 4.9 from Google - what Xiaomi used for nikel's stock kernel.
TC1_DIR=$TC_DIR/linaro-6.3.1
TC1_GIT=https://github.com/rockchip-android/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu
TC2_DIR=$TC_DIR/aosp-4.9
TC2_GIT=https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9
TC2_BRANCH=pie-release

step() { echo; echo "=== $* ==="; }
export GIT_TERMINAL_PROMPT=0   # never hang on a username prompt

mkdir -p "$WORK"

step "1/6 Build tools"
need=""
for p in git make bc bison flex gcc libssl-dev python2 xz-utils wget; do
    dpkg -s "$p" >/dev/null 2>&1 || need="$need $p"
done
if [ -n "$need" ]; then
    echo "Installing:$need  (asks for your Ubuntu password)"
    sudo apt-get update
    sudo apt-get install -y $need
else
    echo "already installed"
fi

step "2/6 Compiler"
mkdir -p "$TC_DIR"
rm -f "$TC_DIR"/*.tar.xz          # leftover from the retired Linaro URL
CROSS=""
# 1. Linaro 6.3.1
if [ ! -x "$TC1_DIR/bin/aarch64-linux-gnu-gcc" ]; then
    rm -rf "$TC1_DIR"
    echo "Trying Linaro GCC 6.3.1 ..."
    git clone --depth 1 "$TC1_GIT" "$TC1_DIR" || echo "  (not available)"
fi
if [ -x "$TC1_DIR/bin/aarch64-linux-gnu-gcc" ] && "$TC1_DIR/bin/aarch64-linux-gnu-gcc" --version >/dev/null 2>&1; then
    CROSS="$TC1_DIR/bin/aarch64-linux-gnu-"
else
    # 2. AOSP 4.9
    if [ ! -x "$TC2_DIR/bin/aarch64-linux-android-gcc" ]; then
        rm -rf "$TC2_DIR"
        echo "Falling back to AOSP GCC 4.9 ..."
        git clone --depth 1 -b "$TC2_BRANCH" "$TC2_GIT" "$TC2_DIR"
    fi
    if "$TC2_DIR/bin/aarch64-linux-android-gcc" --version >/dev/null 2>&1; then
        CROSS="$TC2_DIR/bin/aarch64-linux-android-"
    fi
fi
if [ -z "$CROSS" ]; then
    echo "No working compiler found - send the lines above."
    exit 1
fi
echo "Using: $(${CROSS}gcc --version | head -1)"

step "3/6 SHIFT kernel source (inside Linux, so aux.c is fine)"
if [ ! -d "$KERNEL/.git" ]; then
    git clone --depth 1 -b shift-8.0-mtk https://github.com/SHIFTPHONES/android_kernel_shift_mt6797 "$KERNEL"
else
    echo "already cloned"
fi

step "4/6 nikel overlay"
if [ ! -f "$OVERLAY" ]; then
    echo "Missing $OVERLAY - put nikel-kernel-overlay.tar.gz next to this script"
    exit 1
fi
OV=$WORK/overlay
rm -rf "$OV" && mkdir -p "$OV"
tar -xzf "$OVERLAY" -C "$OV"
cp -r "$OV/kernel/." "$KERNEL/"
cd "$KERNEL"
# Apply every nikel patch from the overlay (0001 panel list, 0002 CW2015 hook, ...).
# A patch that is already in the tree (earlier run) is detected and skipped.
for P in "$OV"/0*.patch; do
    [ -f "$P" ] || continue
    if git apply --check "$P" 2>/dev/null; then
        git apply "$P"
        echo "applied: $(basename "$P")"
    elif git apply -R --check "$P" 2>/dev/null; then
        echo "already in place: $(basename "$P")"
    else
        echo "PATCH DID NOT APPLY: $(basename "$P") - stop and report this."
        exit 1
    fi
done

# Host-tool fix for Ubuntu 22.04 (GCC 10+ defaults to -fno-common): the
# kernel's bundled dtc declares yylloc twice. Upstream removed the duplicate
# in later kernels (commit e33a814e772c); same one-line change here.
LEX=scripts/dtc/dtc-lexer.lex.c_shipped
if grep -q '^YYLTYPE yylloc;' "$LEX"; then
    sed -i 's/^YYLTYPE yylloc;/extern YYLTYPE yylloc;/' "$LEX"
    echo "dtc yylloc fix applied"
else
    echo "dtc yylloc fix not needed"
fi

# USB charger-type query without the battery framework.
# mt_usb.c asks battery_common.c (MTK_SMART_BATTERY, off for now: no nikel
# charger backend yet) for the charger type. The driver already has a
# fallback, BYPASS_PMIC_LINKAGE, that reports STANDARD_HOST; use it whenever
# the battery framework is off. Cable plug/unplug still comes from the PMIC
# (mu3d_hal_is_vbus_exist -> upmu_is_chr_det), unchanged.
python3 - drivers/misc/mediatek/mu3d/drv/mt_usb.c <<'PYEOF'
import sys
p = sys.argv[1]
t = open(p).read()
old = "\tCHARGER_TYPE chg_type;\n#ifdef BYPASS_PMIC_LINKAGE\n"
new = ("\tCHARGER_TYPE chg_type;\n"
       "/* nikel: no battery framework yet -> report a standard USB host */\n"
       "#if defined(BYPASS_PMIC_LINKAGE) || !defined(CONFIG_MTK_SMART_BATTERY)\n")
if new in t:
    print("mt_usb.c charger-type fallback already applied")
elif t.count(old) == 1:
    open(p, "w").write(t.replace(old, new))
    print("mt_usb.c charger-type fallback applied")
else:
    sys.exit("mt_usb.c: expected code not found - stop and report")
PYEOF

# Cable detection without the battery framework.
# upmu_is_chr_det() lives in battery_common.c (off). The driver's own
# CONFIG_POWER_EXT branch reads the PMIC charger-detect bit directly via
# upmu_get_rgs_chrdet(); use that branch when the battery framework is off,
# but only if that helper really is built (PMIC code, always on).
if git grep -qE "^(unsigned int|u32|kal_uint32|int) +upmu_get_rgs_chrdet *\(void\)" -- drivers/misc/mediatek/power/mt6797 drivers/misc/mediatek/pmic 2>/dev/null; then
python3 - drivers/misc/mediatek/mu3d/drv/mt_usb.c <<'PYEOF'
import sys
p = sys.argv[1]
t = open(p).read()
old = "\tbool vbus_exist;\n\n#ifdef BYPASS_PMIC_LINKAGE\n\tos_printk(K_INFO, \"force on\");\n\tvbus_exist = true;\n#else\n#ifdef CONFIG_POWER_EXT\n"
new = ("\tbool vbus_exist;\n\n#ifdef BYPASS_PMIC_LINKAGE\n\tos_printk(K_INFO, \"force on\");\n\tvbus_exist = true;\n#else\n"
       "/* nikel: no battery framework yet -> read the PMIC charger-detect bit */\n"
       "#if defined(CONFIG_POWER_EXT) || !defined(CONFIG_MTK_SMART_BATTERY)\n")
if new in t:
    print("mt_usb.c cable-detect fallback already applied")
elif t.count(old) == 1:
    open(p, "w").write(t.replace(old, new))
    print("mt_usb.c cable-detect fallback applied")
else:
    sys.exit("mt_usb.c: cable-detect code not as expected - stop and report")
PYEOF
else
    echo "upmu_get_rgs_chrdet() not found in the PMIC code - stop and report this line."
    git grep -n "upmu_get_rgs_chrdet\|RGS_CHRDET" -- drivers/misc/mediatek/power/mt6797 drivers/misc/mediatek/include | head -20 | tee "$HERE/chrdet-probe.txt"
    exit 1
fi

# OTG host mode off for now: its 5 V comes from the charger chip's boost
# (BQ24261 on nikel), which needs the charger driver that isn't ported yet.
# xhci-mtk-driver.c calls set_chr_enable_otg() etc. from that framework.
# Normal USB to a PC (adb/MTP, mu3d device mode) is not affected.
DEFCFG=arch/arm64/configs/nikel_defconfig
nikel_off() {   # option, reason
    if grep -q "^CONFIG_$1=y" "$DEFCFG"; then
        sed -i "s/^CONFIG_$1=y/# CONFIG_$1 is not set/" "$DEFCFG"
        echo "defconfig: $1 off ($2)"
    fi
}
nikel_off USB_XHCI_MTK "OTG host needs the charger driver"
nikel_off USB_MTK_DUALMODE "OTG host needs the charger driver"
# First test boot panicked in scp_ipi_handler: nikel's SCP firmware (its own
# scp1/scp2 partitions, 2016) and SHIFT's 2023 SCP driver disagree on the IPI
# mailbox layout (message id read as ~0x3ca4e55f). Kernel stops listening to
# the SCP until a matching driver is ported; the SCP itself keeps running.
nikel_off MTK_TINYSYS_SCP_SUPPORT "SCP IPI mailbox mismatch with nikel's SCP firmware"

# Some MediaTek build scripts call plain 'python' (Python 2); 22.04 has no
# such command by default, so provide one only for this build.
mkdir -p "$WORK/bin"
ln -sf "$(command -v python2)" "$WORK/bin/python"
export PATH="$WORK/bin:$PATH"

step "5/6 Safety checks"
echo "-- code that depends on CONFIG_SHIFT5ME_PROJECT (off for nikel):"
git grep -n "SHIFT5ME_PROJECT" -- ':!arch/arm64/configs' | tee "$WORK/shift5me_project_uses.txt" || true

export ARCH=arm64
export CROSS_COMPILE="$CROSS"
# GCC 6 added warnings that 2016-era MediaTek code trips, and MediaTek's
# Makefiles add -Werror. These only silence those warnings; generated code is
# unchanged. (First hit: mt_ocp.c, -Wmisleading-indentation.)
# Only GCC 6 options, and only on GCC >= 6: an unknown -Wno-... can itself
# become an error under -Werror (the AOSP 4.9 fallback needs none of them).
GCC_MAJOR=$(${CROSS}gcc -dumpversion | cut -d. -f1)
if [ "$GCC_MAJOR" -ge 6 ]; then
    export KCFLAGS="-Wno-misleading-indentation -Wno-tautological-compare -Wno-unused-const-variable -Wno-shift-negative-value -Wno-nonnull-compare -Wno-bool-compare"
    echo "GCC $GCC_MAJOR: silencing GCC 6 warnings ($KCFLAGS)"
fi
make O=out nikel_defconfig >/dev/null

echo "-- options that must be OFF (anything printed as =y is a problem):"
bad=0
for o in MTK_DTBO_FEATURE MTK_AB_OTA_UPDATER MTK_BQ25896_SUPPORT \
         MTK_PUMP_EXPRESS_PLUS_SUPPORT MTK_PUMP_EXPRESS_PLUS_20_SUPPORT \
         TOUCHSCREEN_MTK_FOCALTECH_TS SHIFT5ME_PROJECT USB_C_SWITCH_FUSB302 \
         MTK_TINYSYS_SCP_SUPPORT MTK_HAFG_20 \
         FT_AUTO_UPGRADE_SUPPORT CUST_FTS_APK_DEBUG; do
    v=$(grep -E "^CONFIG_$o=" out/.config || true)
    if [ -n "$v" ]; then echo "PROBLEM: $v"; bad=1; else echo "ok: $o off"; fi
done
echo "-- options that must be ON for the nikel charger and touch:"
for o in MTK_SMART_BATTERY MTK_BQ24296_SUPPORT MTK_JEITA_STANDARD_SUPPORT TOUCHSCREEN_MTK_FT5X0X; do
    if grep -q "^CONFIG_$o=y" out/.config; then echo "ok: $o on"; else echo "PROBLEM: $o is not on"; bad=1; fi
done
echo "-- touch: only the read-only nikel driver may be built (no firmware upgrade code):"
TS_MK=drivers/input/touchscreen/mediatek/ft5x0x/Makefile
if grep -q "nikel_ft5346.o" "$TS_MK" && ! grep -Eq "^[^#]*(ft5x0x_upgrade|ft5x0x_driver)\.o" "$TS_MK"; then
    echo "ok: $TS_MK builds only nikel_ft5346.o"
else
    echo "PROBLEM: $TS_MK is not the nikel version (would build SHIFT's upgrade code)"; bad=1
fi
grep -E '^CONFIG_(CUSTOM_KERNEL_LCM|ARCH_MTK_PROJECT|BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES)=' out/.config
if [ $bad -ne 0 ]; then
    echo "Stopping before the build: a dangerous option came back on (Kconfig 'select')."
    echo "Send the PROBLEM lines above."
    exit 1
fi

step "6/6 Build"
# Remove results of earlier builds first, so an old image can never be
# reported (or fastboot-booted) as the result of a failed build.
rm -f out/arch/arm64/boot/Image.gz-dtb out/arch/arm64/boot/Image.gz out/arch/arm64/boot/Image
rm -f "$HERE/nikel-Image.gz-dtb" "$HERE/nikel-test-twrp.img"
make_rc=0
rm -f "$WORK/make_rc"
{ make O=out -j"$(nproc)" 2>&1 || echo $? > "$WORK/make_rc"; } | tee "$WORK/build.log" | { grep -E "error:|Error [0-9]|undefined reference|multiple definition|warning: .*nt35596|Image.gz-dtb" || true; }
if [ -f "$WORK/make_rc" ]; then make_rc=$(cat "$WORK/make_rc"); rm -f "$WORK/make_rc"; fi
if [ "$make_rc" -eq 0 ] && [ -f out/arch/arm64/boot/Image.gz-dtb ]; then
    cp out/arch/arm64/boot/Image.gz-dtb "$HERE/nikel-Image.gz-dtb"
    echo
    echo "BUILD OK -> $HERE/nikel-Image.gz-dtb"
    if [ -f "$HERE/nikel_make_test_img.sh" ] && [ -f "$HERE/mkbootimg_v0.py" ]; then
        echo
        bash "$HERE/nikel_make_test_img.sh" || echo "(test image not packed - see message above)"
    else
        echo "Do NOT flash it. Pack it with nikel_make_test_img.sh for fastboot boot."
    fi
else
    echo
    echo "BUILD FAILED (make exit code $make_rc). No image was produced."
    echo "Full log: $WORK/build.log (also copied to $HERE)"
    cp "$WORK/build.log" "$HERE/build.log"
    exit 1
fi
