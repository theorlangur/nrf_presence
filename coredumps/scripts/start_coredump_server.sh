#!/bin/bash
coredumps/scripts/kill_existing_coredump_server.sh
source ~/.venvs/zephyr-tools/bin/activate
python3 ../../zephyr/scripts/coredump/coredump_gdbserver.py build_ezurio_llvm/presence_multi_v2/zephyr/zephyr.elf coredumps/coredump.bin
