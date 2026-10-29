# android.hardware.health-service.mediatek (prebuilt)

The health AIDL HAL of LineageOS hardware/mediatek (`aidl/health`), built from
RamDll/android_hardware_mediatek_lineage branch clean-2 at f47c175, which adds four options to
it (scale a mAh charge counter, report standard USB ports as 500 mA at 5 V, report the
negotiated charger power, read the state of health from a given path) and links its non-LLNDK
libraries statically: the stock HyperOS 3 vendor has only android.hardware.health-V4-ndk, and
this HAL serves V5.

patch-images.py puts it over the stock `/vendor/bin/hw/android.hardware.health-service.example`
in vendor.img (same path, so the stock init service, label and vintf entry stay). The vendor
image is the stock one, so the binary is needed before the build starts and is kept here.

Built 2026-10-29 on the clean-2.14 build graph: siso for the objects and static libraries, then
the soong link command by hand (the recipe is in the build notes, scripts/health-hal). Health.o
was compiled from the f47c175 source with the graph's command.

- sha256: 0697e445da189c60aac3801279fef0d5ac9d0dabb7d659da148671a83f11ef84
- NEEDED: libbinder_ndk.so liblog.so libc.so libm.so libdl.so (LLNDK only)
- Tested on clean-2.8 (bind mount over the stock binary): charge counter 3937000 uAh,
  PC port 0.5 A x 5 V, 67 W Xiaomi charger 67 W, another PD charger 25 W.
- Tested on clean-2.14 (bind mount, with the state of health path set to bms/soh): state of
  health 83, Settings > Device diagnostics shows 83 % and 811 cycles; dynamic symbols it needs
  are the same 203 as of the 2.8 build.
