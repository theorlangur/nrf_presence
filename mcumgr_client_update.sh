#!/bin/bash
./mcumgr-client --nb-retry 16 -u 2000 -b 921600 -m 4096 -l 8192 -d /dev/nrf54l15 upload build_ezurio_llvm/presence_multi_v2/zephyr/zephyr.signed.bin
