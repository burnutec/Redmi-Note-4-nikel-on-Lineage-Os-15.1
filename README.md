# Redmi-Note-4-nikel-on-Lineage-Os-15.1
This project aims at resurrecting a long forgotten phone with a very capable SOC.

# LineageOS 15.1 for Redmi Note 4 MTK (nikel)

Unofficial port of LineageOS 15.1 (Android 8.1) to the Xiaomi Redmi Note 4
MediaTek version (codename nikel, MT6797 Helio X20).

## Goal
A daily-usable Android 8.1 for nikel, built on SHIFTPHONES' open
MT6797 Oreo kernel, with nikel's own drivers ported on top.

## Status (V11)
Working:
- Display, touch, capacitive keys, notification LED
- Sound (speaker, headphones), microphone
- Wi-Fi, Bluetooth, charging, MTP, GPU
- SIM, network registration, 2G data, IMEI/baseband
- Rotation (BMI160), light sensor (LTR-579)
- Custom boot logo and boot animation

In progress: proximity, compass (YAS537)

## Status (V12)
Magnetometer now works , needs refinement.



Not working yet: calls, 3G/LTE, camera, fingerprint, IR blaster

## Credits
- SHIFTPHONES: kernel and vendor base (SHIFT5me)
- LineageOS project
- Mainline Linux yamaha-yas530 driver (YAS537 support)

## Note
Proprietary vendor files are not included in this repository.

## Warning
Flash at your own risk. Only boot, system (and logo) are written by this ROM.
Never flash preloader, lk, modem, tee or nvram partitions from other devices.
Back up nvram/nvdata before trying anything.
