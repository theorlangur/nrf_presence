#!/bin/bash
mkdir -p coredumps
source ~/.venvs/zephyr-tools/bin/activate

PARTITIONS_YML=$(find "${BUILD_DIR:-build_ezurio_llvm}" -name partitions.yml -print -quit)
if [ -z "$PARTITIONS_YML" ]; then
    echo "partitions.yml not found under ${BUILD_DIR:-build}" >&2
    exit 1
fi

read COREDUMP_ADDR COREDUMP_SIZE < <(python3 -c '
import sys, yaml
with open(sys.argv[1]) as f:
    parts = yaml.safe_load(f)
p = parts["memfault_coredump_partition"]
print(hex(p["address"]), hex(p["size"]))
' "$PARTITIONS_YML")

python3 coredumps/scripts/jlink_extract_coredump.py "$COREDUMP_ADDR" coredumps/coredump.bin --max-payload "$COREDUMP_SIZE"
