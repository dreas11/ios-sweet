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
reached Home/touch but timed out with IRQ_ENABLE masked. The corrected test
reached Home/touch and completed all three reads with a fresh IRQ. A second boot
with firmware validation repeated the same successful reads. Register offsets follow Linux's
[ARM GIC definitions](https://github.com/torvalds/linux/blob/master/include/linux/irqchip/arm-gic.h)
and the board's live DT.

Patch 0086 adds opt-in `sweetgpu=4`: after the same 90-second delay, obtain
fresh ARC reads, validate the returned indexes, require CX >=64, MX >=192 and
GFX=0, then perform a bounded CX-domain cycle. Require configuration AHB on,
CX/GX initially collapsed with no override/hardware control, and GMU branch
explicitly disabled/off. Set CX CLK_DIS_WAIT=8, clear CX SW_COLLAPSE, poll its
hardware status at 0x1540 for up to 1000 us, record status, then reassert collapse
and restore original controls after the downstream 100-us disable delay. The
live CX DT has no clock/reset/bus references beyond its parent supply and hardware
status register. No RPMh vote, clock, GX, core or GMU register write occurs.
Control restoration is checked; it is not claimed to prove physical OFF because
the board explicitly specifies no OFF-status poll. Host tests exercise guards,
success, timeout, readback failures and restoration failures. Full CI build
passed and mode 4 is now device-verified: Home/touch stayed functional, CX
hardware status changed from 0x5ff4 to 0x80001fe0 and back, AHB clock status
changed to 0x1, and CX/GX/GMU controls were restored. No rendering occurred.

Patch 0087 adds `sweetgpu=5`, a GMU **root-only** clock test inside the proven
CX cycle. It requires a locked, running fixed 600-MHz GPLL0 (L31, fraction0x4000),
both GMU child branches disabled/off, and an idle root initially on XO or GPLL0/2.
It votes only the GPU-specific GPLL0/2 gate, configures parent6/HID2 (nominal
200 MHz), checks the forced root runs, then restores the root configuration and
its original parent-gate bit. It never retunes a PLL or enables a GMU/GX branch.
Unexpected clock rollback leaves CX and the parent powered and stops all further
GPU operations, rather than removing power from uncertain clock state. Host
models cover malformed/unavailable registers, active consumers, unrelated parent
votes, update/enable/disable/restore failures, and the CX keep-powered failure
path. The first device test preserved Home/touch and restored CX but rejected the
root-clock cycle before any clock write: GPLL0 MODE=0xd0100005 has FSM+ACTIVE+
LOCK set while software OPMODE is zero. Root enable remains unverified.

Patch 0088 corrects the guard using the downstream
[Fabia FSM/direct distinction](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/758bb7ef50af360e728662a1ed3b3a1b977a2f13/drivers/clk/qcom/clk-alpha-pll.c):
FSM mode requires ACTIVE, direct mode requires OPMODE RUN. Both retain the
LOCK/output/reset and fixed-frequency checks; no PLL writes are introduced.
The running-root check repeats this validation and rejects a control-mode change.
Host tests cover the observed FSM snapshot, inactive/unlocked FSM, direct standby,
and loss of readiness during the cycle. The corrected mode-5 test is now
verified on the Redmi: root CMD changed from 0x80000000 to 0x2 with CFG0x602,
then returned to CMD0x80000000/CFG0; the original parent gate and CX controls
were restored. Home/touch stayed functional. The 200-MHz value is nominal from
PLL/divider configuration, not an independent frequency measurement. Both child
branches remained disabled and no firmware or rendering command executed.

This hardware test used a locally compiled incremental kernel; the independent
full CI build of the same source also passed. Local linking uses the standalone
kernel flags, not the kernel-cache-only LASTDATA_CONST rename/export filtering;
segment permissions, constructor targets, symbols and payload were checked before
boot. The permanent boot remains the original working image.

The live DT links GPUCC CX/MX/GFX to those three ARC resources. CX's parent
relationship includes MX; the existing `ldoc11` helper only implements a PMIC
LDO protocol and must not be reused for ARC. Auxiliary ARC data maps indexes to
abstract levels; Xiaomi adds one only for the regulator consumer API. The
command payload is the index, not that consumer value. Tests compile the exact
patch's parser/transaction code against a fake RSC and cover malformed maps,
stale completion status, timeout quarantine, occupied slots, IRQ preservation,
read-only messages and touch initialization ordering.

Modes 1–3 do not write clocks, regulators, GPU or GMU registers. Mode 4 writes
only the CX GDSCR controls described above. It does not map the
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
Patch 0085 adds an optional kernel reader (`sweetgpufw=1` alongside a nonzero
`sweetgpu` mode). It accepts only a page-aligned region entirely inside
boot-reserved RAM, uses the existing normal-memory mapping, validates every
entry, SHA256 digest and firmware format, and reports lengths. It publishes no
accelerator, performs no upload and interprets no ELF address as a CPU physical
address. Default-off, independent from the RPMh mode. Host tests compare the
exact C parser with the packer across truncations, byte mutations, overflowing
lengths and malformed inner images with recomputed hashes. The kernel reader has now validated all three private firmware files on the
phone, with Home/touch working. Checksum validation is not authentication.

Bundle layout: 32-byte `<8sII16s>` header (magic, version 1, count 3, zero reserved
bytes), then three 56-byte `<IIQQ32s>` records (kind, zero flags, offset, length,
SHA256). Kinds 1/2/3 are GMU/SQE/ZAP, in that order. Records point to canonical
64-byte-aligned payloads with zero padding; total size is bounded to 1 MiB.
The SQE file keeps its initial header word; a future GPU loader must skip it.
Checksums detect corruption, not vendor authentication. ELF load addresses
must never be used directly as host physical RAM addresses.

## ARC read semantics reference

Qualcomm's October 2025 upstream discussion clarifies that an RPMh read returns
the HLOS/DRV2 vote for ARC/BCM/VRM, not the aggregate across hardware voters:
[developer discussion](https://lkml.iu.edu/2510.3/03634.html).
This explains why obtaining a fresh read is useful before changing an inherited
bootloader vote. On this board, validate each returned value against its command
DB table before treating it as a saved index. Do not reduce an inherited vote
merely because the GPU requires a lower level; preserve other users of the rail.
The current probe still reports raw responses and makes no power request.

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

## First display integration target

`iokit/Kernel/arm/SweetFramebuffer.cpp`, `AppleCLCD::present`, currently looks up
retained `SweetSurface` instances and copies each clipped four-byte-pixel layer
row into the panel framebuffer with CPU `memcpy`. This is a concrete future
integration point for a GPU copy after command submission works, without first
advertising a Metal device. It is not implemented by the diagnostic patches.

An initial GPU-copy path must pin source/destination memory descriptors, map
physical segments into the GPU SMMU (CPU virtual addresses are not GPU addresses),
retain surfaces until a completion fence, handle cache visibility, and preserve
clipping/stride/bounds behavior. Any fallback after an uncertain submission must
first stop GPU writes before the CPU touches the destination. Test a private
scratch copy with a CPU comparison before allowing writes to scanout memory.
Copy offload alone does not accelerate application rendering; measure its actual
benefit rather than equating a successful DMA copy with fluent iOS graphics.

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
