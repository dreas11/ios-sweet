# Adreno 618 bring-up

Current scope is GPU only. The restored phone reaches Home with the original
permanent boot. No GPU command has been submitted and rendering still uses
the CPU. Enabling eight CPU cores in an earlier experiment did not enable GPU
acceleration.

## Implemented first stage

Patch 0082 adds an optional kernel probe after UFS/input initialization:

- Default / `sweetgpu=0`: no action.
- `sweetgpu=1`: read four GPU-related GCC clock status registers only.
- `sweetgpu=2`: additionally read GPUCC clock/power status if the configuration
  bus clock reports explicitly enabled and not off.

Patch 0083 adds `sweetgpu=3`: the same register observations, followed by a
worker delayed 90 seconds. It requires successful touch-rail initialization,
then looks up `cx.lvl`, `mx.lvl`, `gfx.lvl` in the firmware command DB and sends
acknowledged RPMh **read** requests through an empty ACTIVE TCS. It uses the
existing RPMh lock; preserves other TCS/IRQ state; requires a fresh completion
IRQ; quarantines uncertain transactions instead of retrying. It sends no rail
votes. Raw responses are not assumed to be millivolts or saved HLOS votes.

Patch 0084 fixes the completion latch: keep the selected RSC IRQ source enabled
while polling, after checking GIC INTID 37 is neither enabled nor active. The
GIC is only read. On success restore the original source-enable bit; on any
uncertain transaction mask that source and quarantine the slot. Tests model
IRQ_ENABLE gating and verify both initially enabled/disabled source restoration,
GIC rejection, missing mappings and stale status. The first mode-3 device test
reached Home/touch but timed out with IRQ_ENABLE masked; validation of the fix
on hardware is pending. Register offsets follow Linux's
[ARM GIC definitions](https://github.com/torvalds/linux/blob/master/include/linux/irqchip/arm-gic.h)
and the board's live DT.

The live DT links GPUCC CX/MX/GFX to those three ARC resources. CX's parent
relationship includes MX; the existing `ldoc11` helper only implements a PMIC
LDO protocol and must not be reused for ARC. Auxiliary ARC data maps indexes to
abstract levels; Xiaomi adds one only for the regulator consumer API. The
command payload is the index, not that consumer value. Tests compile the exact
patch's parser/transaction code against a fake RSC and cover malformed maps,
stale completion status, timeout quarantine, occupied slots, IRQ preservation,
read-only messages and touch initialization ordering.

It does not write clocks, regulators, GPU or GMU registers. It does not map the
GPU core or SMMU, allocate DMA, load firmware, publish an accelerator or change
the framebuffer. GPUCC mode requires its own hardware validation after the
GCC-only mode. Build and device results are tracked in the active NOTES handoff.

Register map from the sweet device tree and Xiaomi clock sources:

| Block | Physical address | Purpose |
| --- | --- | --- |
| GCC | `0x00100000` | GPU configuration, DDR, MemNoC and SNoC clock gates |
| GPUCC | `0x05090000` | CX/GX power domains and GPU/GMU clocks |
| Adreno core | `0x05000000` | Rendering/command processor; not accessed by probe |
| GMU | `0x0506a000` | GPU management microcontroller; not accessed by probe |
| GPU SMMU | `0x05040000` | GPU address translation; not accessed by probe |

## Firmware preparation

`tools/adreno-firmware-check.py` inspects the two locally extracted firmware
files without executing them. Its bounds/vector tests are in
`tests/test_adreno_firmware.py`.

- `a618_gmu.bin`: 32,768-byte **legacy flat image**. Xiaomi's
  `adreno_is_a615_family()` includes A618 and selects `_load_legacy_gmu_fw()`;
  parsing it as the newer GMU block-header format would be wrong.
- `a630_sqe.fw`: 32,136 bytes. The vendor loader skips the first 32-bit word
  before exposing command-processor microcode to the GPU.
- `a615_zap.mdt/.b00/.b01/.b02/.elf`: listed in the original vendor partition,
  copied from the phone's vendor partition mounted read-only. Remote SHA256 and
  local byte count/hash agree for all five files. Structural checks pass for
  both the MDT/split form and complete ELF; their 1,984-byte loadable payloads
  match. Secure GPU transition support/PIL/SCM remains absent from this XNU port.
  Do not treat the ELF's addresses as host physical RAM destinations.

Hashes are local inventory evidence, not Qualcomm signature authentication.
The tools do not establish that the firmware can execute under XNU.

`tools/gpu_firmware.py` builds a private `SWGPUFW1` bundle from GMU, SQE and the
complete ZAP ELF. `tools/mkpayload.py --gpu-firmware BUNDLE` adds a reserved
segment after the kernel/optional RAM disk and puts `<physical address, byte
length>` in `/chosen/memory-map/GPUFirmware` (two little-endian u64s). It advances
`kernel_end`, so the existing shim keeps the segment below `topOfKernelData`.
No firmware is executed and current kernels do not consume this property yet.

Bundle layout: 32-byte `<8sII16s>` header (magic, version 1, count 3, zero reserved
bytes), then three 56-byte `<IIQQ32s>` records (kind, zero flags, offset, length,
SHA256). Kinds 1/2/3 are GMU/SQE/ZAP, in that order. Records point to canonical
64-byte-aligned payloads with zero padding; total size is bounded to 1 MiB.
The SQE file keeps its initial header word; a future GPU loader must skip it.
Checksums detect corruption, not vendor authentication. ELF load addresses
must never be used directly as host physical RAM addresses.

## Recovery reference for power bring-up

The live command DB has these raw level tables (zeros after the listed values
are padding):

| Resource | RPMh address | Index-to-level table |
| --- | --- | --- |
| `cx.lvl` | `0x30000` | 0, 16, 64, 128, 192, 256, 384 |
| `mx.lvl` | `0x30010` | 0, 16, 192, 320, 384 |
| `gfx.lvl` | `0x30050` | 0, 16, 64, 128, 192, 256, 320, 384, 416 |

A brief KGSL device open/close in recovery successfully initialized the GPU:
the trace records CX enable, GMU at 200 MHz, AWARE/ACTIVE/SLUMBER states, and
the kernel reports loading `a615_zap` and releasing it from reset. This verifies
the existing Linux driver/firmware startup, not XNU or a rendered workload.
Vendor was read-only, the private tracing instance was removed and debugfs
unmounted afterward. Force-power/clock flags were not changed.

The reference driver did not send an ARC write during the recorded startup;
shared/cached votes may already have satisfied it. Do not replay BCM trace
values or treat regulator framework cached levels as physical measurements.
The live DT's CX hardware status register is `0x05091540`; GX additionally
requires domain `0x05091508`, reset `0x05091008`, its root clock and AON reset
sequencing. Current mode 3 is intended to establish RPMh response behavior
before implementing parent votes and domain power changes.

## Device validation, 2026-11-15

CI run 43746952944 successfully compiled and linked patch 0082 with Xcode 16.0.
The first temporary boot (`sweetgpu=1`, six CPUs) reached Home and working touch,
confirmed by the user. The fresh persistent log contains:

| GCC register | Raw value | Observed clock-off bit |
| --- | --- | --- |
| GPU CFG AHB | `0x00000001` | Clear; enable bit also set |
| DDRSS GPU AXI | `0x00000000` | Clear |
| GPU MemNoC | `0x80000220` | Set |
| GPU SNoC DVM | `0x80000001` | Set, despite enable bit being set |

These are boot-time observations, not proof of powered/usable GPU cores. The
configuration bus meets the probe's condition for a separate `sweetgpu=2` test.
The user reports unchanged low fluency, as expected without acceleration.
Second temporary test (`sweetgpu=2`, 19:54:38 CET) also reached Home and working
touch. Its fresh persistent log reports:

| GPUCC register | Raw value | Interpretation |
| --- | --- | --- |
| CX GDSCR | `0x00222001` | PWR_ON (bit 31) clear; SW_COLLAPSE (bit 0) set |
| GX GDSCR | `0x00222001` | PWR_ON clear; SW_COLLAPSE set |
| AHB CBCR | `0x80000001` | Clock-off bit set |
| CX GMU CBCR | `0x80004220` | Clock-off bit set |

Power-bit definitions are from the same pinned Xiaomi revision's
`drivers/clk/qcom/gdsc-regulator.c`. Both domains report collapsed at the time of
the probe. It is not safe to infer that clearing SW_COLLAPSE alone would create
a usable GPU: parent rails, clock sources, reset/clamp ordering, firmware and
memory isolation still need their corresponding driver implementations.

The full permanent boot partition SHA256 remains
`5362ef155e7f073d72100316c4ffe46336edb0319325b89f89f37f56424d29d3`.
Normal boot was requested at 19:58:16 CET and the user confirmed the phone
returned to Home. The probes were temporary only.

## Work still needed for rendering

First establish power/clock state, then implement the required power votes,
reset/firmware sequence, GPU-visible memory and address translation, command
submission, completion interrupts/fences, and fault recovery. A successful
command-processor test would still not accelerate SpringBoard by itself.

The actual iOS cache contains AGXMetalA11, Metal, IOGPU, IOAccelerator and
OpenGLES. That userspace implementation targets Apple's A11 GPU. Mesa
Freedreno/Turnip is a useful Adreno 6xx reference, but its kernel interface and
graphics API integration are not an iOS driver. Rendering therefore also needs
an iOS graphics/IOSurface bridge and appropriate shader compilation. Do not
publish a fake IOAccelerator service simply to make applications select a
hardware path that cannot execute.

References:

- [Xiaomi A6xx driver](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/758bb7ef50af360e728662a1ed3b3a1b977a2f13/drivers/gpu/msm/adreno_a6xx.c)
- [Xiaomi GMU loader](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/758bb7ef50af360e728662a1ed3b3a1b977a2f13/drivers/gpu/msm/adreno_a6xx_gmu.c)
- [Freedreno/Turnip documentation](https://docs.mesa3d.org/drivers/freedreno.html)

Keep probes in separate temporary boot images. Preserve the Home-tested
permanent image until a replacement has been verified on this phone.
