#!/usr/bin/env python3
"""
Extract a Zephyr coredump payload from a raw flash-partition image.

The Zephyr flash-partition coredump backend
(coredump_backend_flash_partition.c) stores a small header at the start of
the partition followed by the raw coredump payload, then erased flash
(0xFF) padding for the remainder of the partition.

This tool reads a raw partition image (e.g. produced by J-Link
`savebin coredump.bin <addr> <size>`), validates the backend header, and
writes out just the coredump payload in the form that
`scripts/coredump/coredump_gdbserver.py` expects.

Header layout (struct flash_hdr_t, __packed, 32-bit target):

    offset  size  field
    0       2     id[2]        = 'C','D'
    2       2     hdr_version  (uint16, little-endian)
    4       4     size         (size_t -> 4 bytes; payload size excl. header)
    8       2     flags        (uint16)
    10      2     checksum     (uint16)
    12      4     error        (int32)
    --------------------------
    16 bytes total
"""

import argparse
import struct
import sys

# struct flash_hdr_t, __packed, little-endian, 32-bit size_t/int
# < = little-endian, no alignment padding (matches __packed)
HDR_FORMAT = "<2sHIHHi"
HDR_SIZE = struct.calcsize(HDR_FORMAT)  # 16

EXPECTED_ID = b"CD"


def err(msg):
    print(f"error: {msg}", file=sys.stderr)


def parse_header(raw):
    if len(raw) < HDR_SIZE:
        err(f"input too small ({len(raw)} bytes) to contain a "
            f"{HDR_SIZE}-byte header")
        return None

    id_bytes, hdr_version, size, flags, checksum, error = struct.unpack(
        HDR_FORMAT, raw[:HDR_SIZE]
    )

    hdr = {
        "id": id_bytes,
        "hdr_version": hdr_version,
        "size": size,
        "flags": flags,
        "checksum": checksum,
        "error": error,
    }
    return hdr


def validate(hdr, total_len, args):
    ok = True

    if hdr["id"] != EXPECTED_ID:
        err(f"bad magic: expected {EXPECTED_ID!r}, got {hdr['id']!r}. "
            f"This does not look like a flash-partition coredump image "
            f"(is the start address correct? is a dump actually stored?).")
        ok = False

    payload_size = hdr["size"]
    available = total_len - HDR_SIZE

    if payload_size == 0:
        err("header reports payload size 0 -- no coredump stored.")
        ok = False
    elif payload_size == 0xFFFFFFFF:
        err("header size field is 0xFFFFFFFF (erased flash) -- "
            "no coredump stored in this partition.")
        ok = False
    elif payload_size > available:
        err(f"header reports payload size {payload_size} (0x{payload_size:x}) "
            f"but only {available} bytes are available after the header. "
            f"Either the dumped region is too small, or size_t is not 4 bytes "
            f"on this target.")
        ok = False

    # The 'error' field is written by the backend if the dump capture itself
    # hit a problem. Non-zero is a warning, not necessarily fatal.
    if hdr["error"] != 0:
        err(f"WARNING: backend recorded a non-zero error code "
            f"({hdr['error']}) when storing this dump. The payload may be "
            f"incomplete. Continuing anyway.")

    return ok


def main():
    p = argparse.ArgumentParser(
        description="Extract a Zephyr coredump payload from a raw flash "
                    "partition image (e.g. J-Link savebin output)."
    )
    p.add_argument("input", help="raw partition image (savebin output)")
    p.add_argument("output", help="output file for the extracted coredump "
                                   "payload (feed to coredump_gdbserver.py)")
    p.add_argument("--force", action="store_true",
                   help="write output even if validation fails")
    p.add_argument("--info", action="store_true",
                   help="print header info and exit without writing output")
    args = p.parse_args()

    try:
        with open(args.input, "rb") as f:
            raw = f.read()
    except OSError as e:
        err(f"cannot read input: {e}")
        return 1

    hdr = parse_header(raw)
    if hdr is None:
        return 1

    print("flash coredump header:")
    print(f"  id           : {hdr['id']!r}")
    print(f"  hdr_version  : {hdr['hdr_version']}")
    print(f"  payload size : {hdr['size']} (0x{hdr['size']:x}) bytes")
    print(f"  flags        : 0x{hdr['flags']:04x}")
    print(f"  checksum     : 0x{hdr['checksum']:04x}")
    print(f"  error        : {hdr['error']}")
    print(f"  total input  : {len(raw)} bytes "
          f"(header {HDR_SIZE} + available {len(raw) - HDR_SIZE})")

    valid = validate(hdr, len(raw), args)

    if args.info:
        return 0 if valid else 1

    if not valid and not args.force:
        err("validation failed; refusing to write output. "
            "Use --force to override.")
        return 1

    # Clamp to available bytes in case --force was used with a bogus size.
    payload_size = min(hdr["size"], len(raw) - HDR_SIZE)
    payload = raw[HDR_SIZE:HDR_SIZE + payload_size]

    # Optional checksum verification: the backend's checksum algorithm is
    # version-specific (a simple additive/xor over the payload in most
    # versions). We verify with a 16-bit additive sum, which matches the
    # common implementation; mismatch is reported as a warning only, since
    # the exact algorithm can differ across NCS versions.
    calc = sum(payload) & 0xFFFF
    if calc != hdr["checksum"]:
        err(f"WARNING: computed additive-sum checksum 0x{calc:04x} != "
            f"stored 0x{hdr['checksum']:04x}. The backend's checksum "
            f"algorithm may differ in your NCS version; verify the payload "
            f"loads in gdb before trusting it.")

    try:
        with open(args.output, "wb") as f:
            f.write(payload)
    except OSError as e:
        err(f"cannot write output: {e}")
        return 1

    print(f"\nwrote {len(payload)} bytes to {args.output}")
    print("next:")
    print(f"  python zephyr/scripts/coredump/coredump_gdbserver.py "
          f"build/zephyr/zephyr.elf {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
