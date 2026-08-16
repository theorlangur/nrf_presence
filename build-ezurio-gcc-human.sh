#!/bin/bash
source /home/theorlangur/ncs/toolchains/7cbc0036f4/gcc16_cross_compile_human_env.sh
west build --build-dir build_ezurio_gcc16_human . --pristine \
    --board orlangur_ezurio_nrf54l15/nrf54l15/cpuapp -- \
    -DBOARD_ROOT=~/myapps/cpp/nrf \
    -DCONF_FILE="prj.conf config/cpp_gcc16.conf config/nrf54l15.conf config/zb.conf config/no_log.conf" \
    -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
    -DNCS_TOOLCHAIN_VERSION=3.4.0 \
    -Dmcuboot_NCS_TOOLCHAIN_VERSION=3.4.0 \
    -DCMAKE_TOOLCHAIN_FILE=/home/theorlangur/ncs/toolchains/7cbc0036f4/gcc16_cross_compile_human.cmake \
    -DTOOLCHAIN_HAS_PICOLIBC=ON

# -Dmcuboot_CONF_FILE="/home/theorlangur/myapps/cpp/nrf/zbvscode/app/presence_multi_v2/sysbuild/mcuboot_gcc16.conf" \
# -Dmcuboot_EXTRA_DTC_OVERLAY_FILE="/home/theorlangur/myapps/cpp/nrf/zbvscode/app/presence_multi_v2/sysbuild/mcuboot.overlay" \
