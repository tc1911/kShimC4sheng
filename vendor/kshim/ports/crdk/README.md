# CrDK integration

The complete `lib/CrDK` submodule is pinned to Project-Aloha/CrDK
`b6c3335136b7f6b28271917de11e8f11982975fc`. Firmware and host tests compile a
build-local materialization, never a private selection of copied drivers.

`UPSTREAM.json` records the baseline, reference-file SHA256 values and both
patch hashes. Patch 0001 captures the required uncommitted GENI, clock and
RPMh work from the read-only CranePkg reference. Patch 0002 contains the
freestanding OSKAL, shared resource APIs, Android binding tables and adapter
test changes. The reference workspace is not required to apply either patch.

```sh
python3 ports/crdk/prepare.py --source lib/CrDK --output build/crdk-source
```

Preparation checks the submodule HEAD and patch hashes, archives the pinned
commit, checks each patch and applies them in order. Its cache key includes
the manifest and preparation script. Change the patches, not generated source
under a normal build directory. `tools/export_crdk_patch.py` is a development
helper for an isolated clone whose index contains patch 0001 and whose work
tree contains patch 0002. No commit or push is part of that workflow.

## Library and port boundaries

| Capability | Shared CrDK implementation | kShimC responsibility |
| --- | --- | --- |
| SPMI / PMIC GPIO | SpmiLib, PmicGpioLib | DT controller/bus/SID tokens and bounded read transport |
| TLMM | GpioLib, AndroidBindingsLib | Translated DT regions, pins and pinctrl conversion |
| I2C / SPI | GeniSeLib FIFO core | Transport resource context; FTM protocol consumers |
| Clock | ClockLib polling resource API | Android clock IDs, provider regions and rate request |
| RPMh / CmdDB | RpmhLib polling API, CmdDBLib bounded lookup | RSC/TCS topology, rail names and constraints |
| UART | DebugUartLib polling API | DT earlycon/stdout-path selection and byte-stream interface |
| Splash | DisplayLib read-only scanout reader | DT SDE offsets and splash reservation; framebuffer conversion |

`CR_FREESTANDING` makes CrDK OSKAL include the explicit `crdk_os.h` port
contract. `crdk_port.c` supplies allocation/string adapters, native MMIO
barriers, monotonic time, bounded polling support, locks, atomics and logging.
The interrupt entry points return unsupported: these boot paths use polling.
Injectable `CrIo` separates the same shared code from native MMIO for tests.

The GENI FIFO core is also used by the patched UEFI `I2CCrDxe`; fixed FIFO
port/pin data lives in its target resource descriptor. Repeated START,
descriptor STOP boundaries, terminal read NACK, abort recovery and SPI chip
select continuity are implemented in the shared core. The existing UEFI
protocol/GPI tests remain separate from the freestanding register models.

Clock/RPMh have explicit polling resource APIs alongside their existing
interrupt-oriented APIs. kShimC uses the polling APIs on its boot CPU, never
concurrently with another CrDK client for the same controller. Clock setup
preserves inherited votes and selects a verified undivided CXO DFS entry or
normal RCG; it does not retune an active engine or rewrite DFS tables. RPMh
claims only an idle active TCS, retains it across timeout, accepts a verified
late completion and restores its snapshot before release. Shared regulator
votes remain enabled because an inherited vote cannot be read back reliably.

UART has bounded TX/RX recovery and quiesce. Quiesce restores saved settings
and inherited RX only after the runtime's commands stop. An abort timeout
retains ownership and prevents payload handoff. Legacy interrupt-driven UART
remains available to existing CrDK consumers. UEFI INF mappings expose the
new APIs through GeniSeLib, ClockLib, RpmhLib, CmdDBLib, DebugUartLib,
QupResourceLib and SplashLib.

## DT and display policy

`src/dt` validates full blobs, ancestor status, property lengths, unique
phandles, specifier cells, register names and ranges translation. Android
SPMI peripheral tuples are decoded as peripheral offsets, never CPU MMIO.
Each arbiter has its own controller, bus ID and SID. Qualcomm PON and PMIC
GPIO keys retain Vol+ up, Vol- down and Power confirm.

The actual Andromeda/HDK8150 blobs and their hashes are in `tests/fixtures`.
The Kona fixture is a focused binding fixture derived from the pinned
OnePlus source, not a full board DT or hardware capture. Stock HDK's empty
touch selector is respected. Panel availability and the FTM property family
are checked before any resources are acquired; chip ID selects the supported
protocol at runtime. Coordinate flips and cancellation remain in the input
layer, including taps delivered in one FIFO batch.

SM8150 and SM8250 GPIO/clock tables are generated from hash-checked files
listed in `SM8150-SOURCES.json` and `SM8250-SOURCES.json`. The latter pins
OnePlus `oneplus/SM8250_Q_10.0` at
`cad09e061ef6cf689a4a1e54d27562e0f042236a`. Andromeda pins MiCode
`74347f5521fb444936f5986ff0937264307d78fe`. Generation is a maintenance step;
ordinary builds use the generated table in patch 0002 and need no kernel tree.

Splash discovery only reads SDE registers. It validates stable CTL routes,
pipe/mixer dimensions, linear RGB565 or RGB/BGR8888 format, stride and bounds
inside the reservation. Shared-framebuffer split scans require consistent
left/right mixers and contiguous pixels. Ambiguous composition, compression,
rotation and scaling are rejected. No usable splash means serial selection.
No panel, DSI or display clock initialization is performed.

MMU construction uses explicit resource descriptions and reservation/no-map
information. It maps runtime memory, the runtime DT, devices and explicit
framebuffer/CmdDB consumers, without an implicit map of all RAM. Payload DT
entries are validated against their Manifest entry sizes and do not replace
the runtime DT. Handoff cleans writable WB regions owned by the runtime,
quiesces consumers, stops workers and disables its MMU.

## Verification

```sh
cmake -S tests -B build/host -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/host -j8
ctest --test-dir build/host --output-on-failure
python3 tools/test_crdk_upstream.py --edk2 /path/to/edk2
```

The second command group tests a fresh materialization: native GENI, Clock,
RPMh and MU I2C/SPI suites, plus AArch64 UEFI syntax checks for the new APIs
and modified I2C target/driver. The optional LT9611 extension is outside this
patch set; its upstream runner option requires that separate driver extension.
EDK2 headers are an explicit test input, not a firmware-build dependency.

Host models and UEFI syntax checks cover the integration contract. QEMU and
board packaging commands are in the [runtime README](../../README.md);
Y700 display, input and boot status are recorded separately in the
[Gen 4](../../../../../lenovo-tb322fc/README.md) and
[Gen 5](../../../../README.md) device documentation.
