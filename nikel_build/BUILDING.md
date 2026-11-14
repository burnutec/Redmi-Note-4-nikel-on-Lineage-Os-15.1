# Building LineageOS 15.1 for the Redmi Note 4 MTK (nikel)

This repository holds the two helper scripts that build everything:

| Script | Builds |
|---|---|
| `nikel_setup_and_build.sh` | the kernel (`nikel-Image.gz-dtb`) from SHIFTPHONES' MT6797 kernel plus the nikel kernel overlay |
| `nikel_lineage_build.sh` | the LineageOS 15.1 ROM zip, or only `boot.img` after a kernel change |

Nothing in these scripts touches the phone. Flashing is always a separate, manual step.

## What you need

**A Linux build machine.** The scripts are written for **Ubuntu 22.04**: a normal install, or WSL2 on Windows.

- On WSL, the Android source must live inside WSL's own Linux disk (`~/...`), never on a Windows drive (`/mnt/c`, `/mnt/d`, ...).
- At least 16 GB of RAM, plus swap: the first ROM build is memory hungry.
- **Disk space:** the LineageOS source is about 60–80 GB, and the build output another 30–40 GB.

**A project folder** holding the scripts and the files they work with:

| File / folder | What it is | Where it comes from |
|---|---|---|
| `nikel_setup_and_build.sh` | kernel build script | this repository |
| `nikel_lineage_build.sh` | ROM build script | this repository |
| `nikel-kernel-overlay.tar.gz` | nikel kernel patches and drivers | the kernel overlay repository, packed with its files at the top level |
| `nikel-lineage-trees.tar.gz` | `device/xiaomi/nikel` | the device tree repository, packed as `device/xiaomi/nikel/...` |
| `stock-3x64/` | a copy of `/system` from nikel's stock MIUI (`V10.2.2.0.MBFCNXM`) | your own phone, while it still runs MIUI |
| `vendor.img`, or an unpacked copy in `~/nikel/shift-vendor` | SHIFT5me ShiftOS 8.1 vendor image | SHIFTPHONES' firmware |

The proprietary files (`stock-3x64` and SHIFT's vendor image) are **not** part of any of the repositories and must not be uploaded anywhere. `proprietary-files.txt` and `proprietary-files-nikel.txt` in the device tree list exactly which files are taken from each.

## 1. Kernel

Put `nikel_setup_and_build.sh` and `nikel-kernel-overlay.tar.gz` in the project folder, then run:

```
bash nikel_setup_and_build.sh
```

It works in six stages, and finished stages are skipped on later runs:

1. Installs the build tools (asks for your password once).
2. Gets a compiler: Linaro GCC 6.3.1, or Google's GCC 4.9 as a fallback.
3. Clones SHIFT's kernel source (`shift-8.0-mtk`) into `~/nikel/android_kernel_shift_mt6797`.
4. Unpacks the overlay and applies every patch in it, `0001-…` upward. A patch that is already in place is detected and skipped. If it stops with `PATCH DID NOT APPLY`, stop there and report it.
5. Safety checks: options that must stay off for nikel (firmware upgrade code, SHIFT-only parts) are verified before anything is built.
6. Builds.

A good run ends with:

```
BUILD OK -> <project folder>/nikel-Image.gz-dtb
```

If it ends any other way, the full log is in `~/nikel/build.log`.

## 2. ROM

The stages run in this order, and each one can be repeated:

```
bash nikel_lineage_build.sh setup     # build packages, repo tool, Java 8      (minutes, once)
bash nikel_lineage_build.sh sync      # download LineageOS 15.1 into ~/lineage (hours, once)
bash nikel_lineage_build.sh extract   # device tree, source patches, kernel, proprietary files
bash nikel_lineage_build.sh build     # the ROM zip                            (hours the first time)
```

What `extract` does:
- Unpacks `nikel-lineage-trees.tar.gz` into `~/lineage/device/xiaomi/nikel`.
- Applies the small framework patches from `device/xiaomi/nikel/patches/`.
- Copies `nikel-Image.gz-dtb` in as the prebuilt kernel.
- Unpacks SHIFT's `vendor.img` to `~/nikel/shift-vendor` if that isn't there yet.
- Runs the device tree's `extract-files.sh`. That copies the listed proprietary files into `~/lineage/vendor/xiaomi/nikel`, then applies the nikel fix-ups to SHIFT's camera libraries. Each one should print `camera blob patch: applied: …`. If it says `UNEXPECTED BYTES`, the vendor image is not the expected one: stop.
- At the end it prints `files not found: N`. This should be 0; the details are in `extract.log`.

A good `build` ends with `BUILD OK`, and the ROM zip `lineage-15.1-<date>-UNOFFICIAL-nikel.zip` is copied to the project folder.

### After a kernel-only change

There's no need to rebuild the whole ROM:

```
bash nikel_setup_and_build.sh
bash nikel_lineage_build.sh boot
```

This writes `nikel-lineage-boot.img` to the project folder.

### When files were removed from the device tree

An incremental build can leave stale files in the output. Before `build`, run once:

```
cd ~/lineage && source build/envsetup.sh && lunch lineage_nikel-userdebug && make installclean
```

## 3. Flashing

- Use TWRP for the ROM zip.
- `boot.img` can be flashed with TWRP or fastboot.
- The ROM zip writes only `boot`, `system` and the boot `logo`.

**Never flash** `preloader`, `lk`, `md1img`/`md1dsp`/`md1arm7`/`md3img`, `tee1`/`tee2`, `scp1`/`scp2`, `nvram`, `nvdata`, `nvcfg`, `proinfo` or `protect1`/`protect2`, and never take them from another device. Keep a backup of nvram/nvdata before trying anything.

## Disk space housekeeping

Every build leaves large files behind in `~/lineage/out/target/product/nikel/`. These can safely be deleted:
- Older `lineage-15.1-*.zip` files.
- `lineage_nikel-ota-*.zip`, the over-the-air package (not used).
- `obj/PACKAGING/target_files_intermediates/`, which is regenerated on every build.

Keep the downloaded source (`~/lineage/.repo`), `~/nikel/shift-vendor` and your stock copy.
