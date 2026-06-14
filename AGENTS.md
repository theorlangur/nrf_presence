# presence_multi_v2 — Project Reference

## Overview

Nordic nRF54L15 presence sensor application using the **NRF-Connect SDK** and **Zephyr RTOS**.
Multi-sensor device with mmWave radar, PIR, and Zigbee connectivity. MCUBoot bootloader with sysbuild.

**Target board**: `orlangur_ezurio_nrf54l15/nrf54l15/cpuapp`
**Alternate board**: `nrf52840` (controlled by `USE_NRF52` env var)

---

## File layout

```
CMakeLists.txt          # Top-level CMake (Zephyr app, links submodules)
prj.conf                # Main Kconfig fragment
sysbuild.conf           # MCUBoot single-app sysbuild config
dts.overlay             # Device tree overlay (GPIOs, UARTs, I2C sensors, buttons, LEDs)
config/                 # Kconfig fragments:
#   cpp.conf            # Enable C++ support
#   cpp_lcxx.conf       # Enable C++ with custom libc++
#   nrf54l15.conf       # nRF54L15-specific config
#   nrf52.conf          # nRF52-specific config
#   zb.conf             # Zigbee config
#   zb_legacy.conf      # Legacy Zigbee config
#   log.conf            # Enable logging
#   no_log.conf         # Disable logging
src/                    # Application sources:
#   main.cpp
#   ld2412_task.cpp / ld2412_task.hpp  (mmWave radar LD2412)
#   zb/                  (Zigbee-related code)
submodules/             # 4 Git submodules (see .gitmodules):
#   esp_generic_lib     # Common utilities
#   nrf_zb_cpp          # Zigbee C++ wrapper
#   nrf_general         # General nRF utilities
#   nrf_uart            # UART helpers
cmake/CustomLibcxx.cmake # Custom libc++ setup for Zephyr build
```

---

## Submodules

Managed via `.gitmodules`. After cloning a fresh repo, run:
```bash
git submodule init && git submodule update
```
Or use the helper:
```bash
./git_init_submodules.sh
```

## Sibling directories expected

The build uses `-DBOARD_ROOT=~/myapps/cpp/nrf` which points to a parent NRF workspace containing:
- `zephyr/` — Zephyr RTOS
- `nrf/` — Nordic nRF modules
- `bootloader/mcuboot/` — MCUBoot bootloader
- `toolchains/` — SDK toolchains

## Sensors / hardware

- **MMWave radar 1**: LD2412 on UART20 (115200 baud)
- **MMWave radar 2**: on UART22 (115200 baud)
- **PIR sensor**: GPIO1_11 (active-high, pull-down)
- **I2C bus**: SDA=1_6, SCL=1_5
  - ENS160 (eCO2 sensor) at 0x53
  - AHT20/DHT20 (humidity/temp) at 0x38
- **LED**: Blue LED on GPIO2_9
- **Buttons**: Boot (GPIO0_0), Zigbee (GPIO0_1)
- **Watchdog**: WDT31

---

## Building

### Build scripts (wrapper + build commands)

| Script | Purpose |
|---|---|
| `llvm.sh` | Sources LLVM/M33 toolchain env, then runs `$*` |
| `lcxx.sh` | Sets `USE_CUSTOM_LIBCXX=1`, sources LLVM M33 libc++ env, then runs `$*` |
| `e.sh` | Sources LLVM/M33 env (GCC variant), then runs `$*` |

### Build shell scripts

| Script | Toolchain | Config | Build dir |
|---|---|---|---|
| `build-ezurio-llvm.sh` | LLVM | cpp, nrf54l15, zb, no_log | `build_ezurio_llvm` |
| `build-ezurio-llvm-log.sh` | LLVM | cpp, nrf54l15, zb, log | `build_ezurio_llvm` |
| `build-ezurio-llvm-log-lcxx.sh` | LLVM + libc++ | cpp_lcxx, nrf54l15, zb, log | `build_ezurio_llvm` |

All scripts use `-DCONFIG_LLVM_USE_LLD=y -DCONFIG_COMPILER_RT_RTLIB=y`.

### Typical build commands

```bash
# Build from scratch (LLVM, no logging)
./e.sh ./build-ezurio-llvm.sh

# Build from scratch (LLVM + logging)
./e.sh ./build-ezurio-llvm-log.sh

# Build from scratch (LLVM + libc++ + logging)
./lcxx.sh ./build-ezurio-llvm-log-lcxx.sh

# Incremental build (after cmake configure)
./e.sh cmake --build build_ezurio_llvm
./lcxx.sh cmake --build build_ezurio_llvm
```

### Flashing

```bash
# Flash over J-Link Edu
./e.sh west flash -d build_ezurio_llvm --dev-id 802005000

# Flash with erase
./e.sh west flash -d build_ezurio_llvm --dev-id 802005000 --erase
```

### MCUmgr OTA update

```bash
# Update firmware over UART using mcumgr client
./mcumgr_update.sh          # (if it exists)
# or
./mcumgr-client ...         # Rust-based mcumgr CLI (4K MTU)
```

---

## Build output

- `build_ezurio_llvm/mcuboot/zephyr/zephyr.elf` — Final binary (MCUBoot application image)
- `build_ezurio_llvm/dfu_application.zip` — DFU update package
- `build_ezurio_llvm/merged.hex` — Combined HEX file

### Typical resource usage

- **FLASH**: ~40 KB / 62 KB (~64%)
- **RAM**: ~93 KB / 188 KB (~49%)

---

## VS Code tasks

Defined in `.vscode/tasks.json`:

| Task | Command |
|---|---|
| Build Ezurio LLVM | `./e.sh cmake --build build_ezurio_llvm` |
| Build Ezurio LLVM lcxx | `./lcxx.sh cmake --build build_ezurio_llvm` |
| West Build Ezurio LLVM | `./e.sh ./build-ezurio-llvm-log.sh` |
| West Build Ezurio LLVM (no log) | `./e.sh ./build-ezurio-llvm.sh` |
| West Build Ezurio LLVM lcxx | `./lcxx.sh ./build-ezurio-llvm.sh` |
| West Build Ezurio LLVM Log with libc++ | `./lcxx.sh ./build-ezurio-llvm-log-lcxx.sh` |
| Flash Ezurio LLVM over JLINK-EDU | `./e.sh west flash -d build_ezurio_llvm --dev-id 802005000` |
| Core: extract/info/clear/run/stop | coredumps/scripts/* |

---

## Flash partition layout

Memory partition configuration from `pm.yml`:
- `coredump_partition`: 256 KB, placed after `zboss_product_config`, aligned to 0x1000

---

## Tool paths (local machine)

- SDK toolchain: `/home/theorlangur/ncs/toolchains/7cbc0036f4/`
- Boards root: `~/myapps/cpp/nrf`
- Env scripts: `toolchains/7cbc0036f4/env_llvm_m33.sh`, `env_llvm_m33_lcxx.sh`
- Toolchain CMake: `toolchains/7cbc0036f4/usr/local/bin/cmake`

---

## Key technologies

- **RTOS**: Zephyr OS (via nRF-Connect SDK)
- **Toolchain**: LLVM (armclang-based Pico toolchain) or GCC, both targeting Cortex-M33
- **C++**: Clang C++ lib with custom libc++ support
- **Connectivity**: Zigbee (NBZ / zb_thread), MCUBoot bootloader
- **Debugger**: J-Link EDU (GDB at `jlink_edu_gdb.sh`)
- **Debug**: Core dump support with Flash partition backend
