# sheng stock image measurements (2026-10-23)

Dumps from a stock, slot-A Xiaomi Pad 6S Pro (SM8550, `xiaomi,sheng`):

| partition | size | notes |
|---|---|---|
| `boot_a` | 201,326,592 B (192 MiB) | header v4, page 4096, kernel 40,417,792 B, ramdisk 0, no DTB |
| `boot_b` | 201,326,592 B | byte-identical to `boot_a` (stock, slot B untouched) |
| `dtbo_a` | 25,165,824 B | 32 DTBO entries, page 4096 |
| `init_boot_a` | 8,388,608 B | GKI ramdisk |

The kernel starts with a PE/COFF stub (`MZ`) and carries the ARM64 Image
magic `ARM\x64` at offset 0x38, so it is accepted as a raw Image.

`dtbo_a` is a Qualcomm DTBO table (magic `0xd7b7ab1e`). Most board entries
contain a `qcom,sde-kms` node with everything kShimC's splash path needs:

- `reg-names = "mdp_phys", "vbif_phys", "regdma_phys", "ipcc_reg"`
- `reg = <0x0ae00000 0x84000 ...>`
- `/reserved-memory/cont_splash_region`
- `qcom,sde-sspp-off` = 10 pipes (4 VIG + 6 DMA)
- `qcom,sde-ctl-off` = 6 CTLs
- `qcom,sde-mixer-off` = 8 slots, 6 real + 2 `0xf0f` dummies
- no `qcom,sde-hw-version` property

The base compatible strings are generic `qcom,kalama-*`; the Linux DTB uses
`xiaomi,sheng` + `qcom,sm8550`. Device identification must therefore accept
`qcom,sm8550` as the SoC marker (implemented in `1cac9fe`).

## Splash changes

`vendor/kshim/src/dt/splash.c` originally accepted at most 8 SSPP offsets and
6 mixer slots for non-Sun/Canoe SoCs, which rejects the 10-pipe, 8-slot
SM8550 tree. The local patch:

- reads up to 10 `qcom,sde-sspp-off` entries;
- reads up to 8 `qcom,sde-mixer-off` entries and keeps the leading real
  mixers (drops `0` / `0xf0f`), still limited to 6;
- allows up to 6 DMA SSPP types for the legacy path.

This keeps the DPU9-era legacy CTL/SSPP register decoder, which is the only
path SM8550 can use (no `qcom,sde-hw-version`, no Sun/Canoe markers).

## Current device state

`linux` partition does not exist; the device is stock dual-slot Android.
The first `fastboot boot` test therefore uses an Android default entry and
an embedded Linux payload that only becomes bootable after the dual-boot
partition layout is recreated.
