# nikel panel driver: NT35596 Tianma (`nt35596_fhd_tianma_phantom`)

Your phone reports `lcm=1-nt35596_fhd_tianma_phantom`. This is that panel's
driver, ported to SHIFT's MT6797 3.18 kernel (`shift-8.0-mtk`).

## Where every part comes from

| Part | Source | Checked how |
|---|---|---|
| Init table (498 rows) | Xiaomi `hennessy-l-oss` (Redmi Note 2/3) | compiled, byte-identical to nikel stock kernel |
| CABC init table (498 rows) | same | byte-identical |
| Suspend table | read from nikel stock kernel | byte-identical (differs from Note 2: 100 ms, no backlight-off row) |
| Brightness table | read from nikel stock kernel | byte-identical (Note 2 version had 2 extra page-select rows) |
| `TE_ENABLE_FUNCTION` | deduced | stock tables only match **with** it defined |
| Pins: RST GPIO180, ENP GPIO87, ENN GPIO108, BL_EN GPIO50 | nikel stock DT pin states | decoded from the stock DTB |
| Timings, PLL 475, 4 lanes, burst video | Xiaomi source | not verifiable from the binary; same driver family |
| Driver structure (own pin driver) | modeled on SHIFT's `ft8716_fhd_dsi_cmd_shift5me/lcm_dispsys_gpio.c` | type-checked against SHIFT's real `lcm_drv.h`, `-Wall -Werror` |

## Why the driver drives its own pins

SHIFT's MT6797 display driver differs from Xiaomi's/Ruben's:

- `lcm_util.set_reset_pin` writes `DISPSYS_CONFIG_BASE + 0x150`, which drives
  the reset line only when the pin is in its dedicated LCM_RST function.
  nikel keeps GPIO180 in **GPIO mode**, so that write would do nothing.
- SHIFT has one bias helper (`set_gpio_lcd_enp_bias`) that flips both bias
  pins together; nikel switches ENP and ENN separately with delays between.
- SHIFT's `LCM_DRIVER` has no `enable_cabc_cmdq`, so CABC is left off.

So the file contains a small platform driver (`xiaomi,nikel-lcm-gpio`) that
takes its pin states from a node in `nikel.dts`, exactly like SHIFT's own
panel does. Nothing in SHIFT's display driver is modified.

The TPS65132 bias chip is bound by this driver through nikel's DT node
(`compatible = "mediatek,i2c_lcd_bias"`, i2c0 @ 0x3e). SHIFT's shared
`lcm_i2c.c` does not compete: it is compiled only with
`MTK_LCM_DEVICE_TREE_SUPPORT` and matches `"mediatek,I2C_LCD_BIAS"`.

## Install into SHIFT's kernel

1. Copy `drivers/misc/mediatek/lcm/nt35596_fhd_tianma_phantom/` into the tree.
2. Apply `0001-lcm-register-nt35596-tianma-nikel.patch` (adds the extern and
   list entry to `mt65xx_lcm_list.c`, guarded by `NT35596_FHD_TIANMA_PHANTOM`,
   which SHIFT's lcm Makefile generates from `CONFIG_CUSTOM_KERNEL_LCM`).
3. Use the `nikel.dts` from this package (it contains the `nikel_lcm_gpio` node).
4. In the nikel defconfig:

   ```
   CONFIG_CUSTOM_KERNEL_LCM="nt35596_fhd_tianma_phantom"
   CONFIG_LCM_HEIGHT="1920"
   CONFIG_LCM_WIDTH="1080"
   # CONFIG_MTK_LCM_DEVICE_TREE_SUPPORT is not set
   ```

## What to look for in the first boot log

```
[lcm nt35596 tianma] pins ready (0 missing)
[lcm nt35596 tianma] tps65132 probe, addr 0x3e
```

`missing` above 0, or `tps65132 not bound`, means the panel will not come
back after the first screen-off. Boot itself should still show a picture,
because LK has already initialized the panel by then.

## Known gaps (on purpose, first boot)

- **Brightness below 32 is clamped to 32.** Stock drives low levels through a
  KTD3116 backlight chip by bit-banging BL_EN (GPIO50) with ~10 µs pulses.
  Pin-state switching can't guarantee that timing; this needs direct GPIO
  access and comes later.
- **CABC stays off** (no hook in SHIFT's `LCM_DRIVER`).
- **`compare_id`** checks the NT35596 chip ID (0x96) only; LK has already
  chosen the panel when the kernel runs.
