#!/bin/bash
source /home/theorlangur/ncs/toolchains/7cbc0036f4/gcc16_cross_compile_env.sh
west build --build-dir build_ezurio_gcc16 . --pristine \
    --board orlangur_ezurio_nrf54l15/nrf54l15/cpuapp -- \
    -DBOARD_ROOT=~/myapps/cpp/nrf \
    -DCONF_FILE="prj.conf config/cpp_gcc16.conf config/nrf54l15.conf config/zb.conf config/no_log.conf" \
    -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
    -DNCS_TOOLCHAIN_VERSION=3.0.2 \
    -Dmcuboot_NCS_TOOLCHAIN_VERSION=3.0.2 \
    -DCMAKE_TOOLCHAIN_FILE=/home/theorlangur/ncs/toolchains/7cbc0036f4/gcc16_cross_compile.cmake
