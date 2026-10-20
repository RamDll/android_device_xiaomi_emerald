# android.hardware.health-service.mediatek (prebuilt)

The health AIDL HAL of LineageOS hardware/mediatek (`aidl/health`), built from
RamDll/android_hardware_mediatek_lineage branch clean-2 at 8d8352e, which adds three options to
it (scale a mAh charge counter, report standard USB ports as 500 mA at 5 V, report the
negotiated charger power) and links its non-LLNDK libraries statically: the stock HyperOS 3
vendor has only android.hardware.health-V4-ndk, and this HAL serves V5.

patch-images.py puts it over the stock `/vendor/bin/hw/android.hardware.health-service.example`
in vendor.img (same path, so the stock init service, label and vintf entry stay). The vendor
image is the stock one, so the binary is needed before the build starts and is kept here.

Built 2026-10-20 on the clean-2.8 build graph: siso for the objects and static libraries, then
the soong link command by hand (the recipe is in the build notes, scripts/health-hal). Health.o
compiled again from 8d8352e with the graph's command is byte-identical to the one linked here.

- sha256: 9b5752a946c2b3102fc1f18da3824e91d66a67f8584986947b2f91671f46f665
- NEEDED: libbinder_ndk.so liblog.so libc.so libm.so libdl.so (LLNDK only)
- Tested on clean-2.8 (bind mount over the stock binary): charge counter 3937000 uAh,
  PC port 0.5 A x 5 V, 67 W Xiaomi charger 67 W, another PD charger 25 W.
