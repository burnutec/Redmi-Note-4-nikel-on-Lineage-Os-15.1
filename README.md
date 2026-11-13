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

## Status (V12)
Working:
- Magnetometer now works , needs refinement.
- Proximity sensor.

## Status (V13)
Working:
- Magnetometer (YAS537): calibrates itself after a figure-8 motion,
  reads Earth's field correctly (~50 µT)
- The pressure sensor that SHIFT's sensor program reported is now hidden;
  the Redmi Note 4 has no barometer

Note: after every reboot the compass needs one figure-8 motion before it is accurate.  

Not working yet: calls, 3G/LTE, camera, fingerprint, IR blaster

## Credits
- SHIFTPHONES: kernel and vendor base (SHIFT5me)
- LineageOS project
- Mainline Linux yamaha-yas530 driver (YAS537 support)

## Note
Proprietary vendor files are not included in this repository.

## Dev. Notes:
The lowest end of devices has been used in development, meaning the 2+16GB and 3+32GB variants, this proves how capable these devices were,
and the poor decison from Mediatek's part on not supporting this chip longer or at all, as history shows it.
Planned for later: Aquire 4+64GB high end model for testing.

## Warning
Flash at your own risk. Only boot, system (and logo) are written by this ROM.
Never flash preloader, lk, modem, tee or nvram partitions from other devices.
Back up nvram/nvdata before trying anything.
