# vendor.lineage.health-service.default (prebuilt)

The charging control service of LineageOS (`hardware/lineage/interfaces`, `health/aidl/default`
at a0f3ada) and its interface library `vendor.lineage.health-V2-ndk`, unchanged sources. The service
is built with the `lineage_health` soong config of BoardConfig.mk: it switches
`/sys/class/power_supply/battery/input_suspend` (0 = charging, 1 = charger input suspended; toggle
mode only, no bypass). With the path given it uses that node directly; without it, it would probe
its default list in a loop until one is accessible, and system_server waits for this declared HAL
at boot, so a missing permission would hang the boot instead of only failing the switch.
Settings > Battery > Charging control needs the service: the LineageHealth service publishes its
binder only when this HAL is declared, and the stock vendor has none.

patch-images.py adds both to vendor.img, with the .rc and the vintf fragment of hardware/lineage
as they are and the domain of `device/lineage/sepolicy/common/vendor/hal_lineage_health_default.te`
in vendor_sepolicy.cil. The vendor image is the stock one, so the binaries are needed before the
build starts and are kept here.

Built 2026-10-27 on the clean-2.10 build graph (vendor arm64 armv8-2a variants). The library and,
without the config, the service came out of siso; with the config, the service was compiled and
linked by hand with the graph's own commands (scripts/lineage-health in the build notes: the same
commands with the graph's flags give back the siso-built service byte for byte), the cflags as
soong writes them for that config. Only ChargingControl.o differs.

- vendor.lineage.health-service.default: sha256
  cca12c0ec3eedfa83bd636e2915b2aeb8a76feda5dfeb20da4f1f8a2342c1e6b;
  NEEDED libbase libbinder_ndk vendor.lineage.health-V2-ndk libc++ libc libm libdl
- vendor.lineage.health-V2-ndk.so: sha256
  5bac69723a4956048f80eb9956e257f6f1627353638af187800e1a0dafe9705d;
  NEEDED libbinder_ndk libc++ libc libm libdl
- Every undefined symbol of both is in the phone's /vendor/lib64 libbase and libc++,
  libbinder_ndk and bionic (clean-2.10).
- input_suspend by hand on clean-2.10: 1 -> status Unknown, USB input 500 -> 15 mA, the phone
  runs on the battery, USB stays online; 0 -> charging again.
