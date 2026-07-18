#!/bin/bash
source ~/.venvs/zephyr-tools/bin/activate

PARTITIONS_YML=$(find "${BUILD_DIR:-build_ezurio_llvm}" -name partitions.yml -print -quit)
if [ -z "$PARTITIONS_YML" ]; then
    echo "partitions.yml not found under ${BUILD_DIR:-build_ezurio_llvm}" >&2
    exit 1
fi

read PART_ADDR PART_SIZE < <(python3 -c '
import sys, yaml
with open(sys.argv[1]) as f:
    parts = yaml.safe_load(f)
p = parts["breadcrumbs_partition"]
print(hex(p["address"]), hex(p["size"]))
' "$PARTITIONS_YML")

python3 breadcrumbs/scripts/jlink_extract_breadcrumbs.py "$PART_ADDR" --max-payload "$PART_SIZE" --clear-only
