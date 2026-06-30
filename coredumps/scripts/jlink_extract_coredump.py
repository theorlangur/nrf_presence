#!/usr/bin/env python3
"""
Connect to an nRF54L15 over SWD via J-Link, read the flash-partition coredump
header, parse the payload size on the fly, and save exactly the coredump
payload (ready for coredump_gdbserver.py).

Requires:
    pip install pylink-square
    plus the SEGGER J-Link software installed (libjlinkarm.so / JLinkARM.dll).

This avoids dumping the whole partition: it reads the 16-byte header first,
learns the real payload length, then reads only header+payload.

Header layout (struct flash_hdr_t, __packed, 32-bit target), 16 bytes:
    0  4  id[4] = 'C','O','R','E'
    4  4  hdr_version (uint32 LE)
    8  4  size        (size_t -> 4 bytes; payload size excl. header)
    ...
    8  2  flags       (uint16)
    10 2  checksum    (uint16)
    12 4  error       (int32)
"""

import argparse
import struct
import sys

HDR_FORMAT = "<4sII"
HDR_SIZE = struct.calcsize(HDR_FORMAT)  # 12
EXPECTED_ID = b"CORE"

FOOTER_FORMAT = "<4sI8s"
EXPECTED_FOOTER_ID = b"DUMP"
FOOTER_SIZE = struct.calcsize(FOOTER_FORMAT)  # 16

# nRF54L15 application core. Adjust if your device/core name differs.
DEFAULT_DEVICE = "nRF54L15_M33"


def err(msg):
    print(f"error: {msg}", file=sys.stderr)


def parse_header(raw, base, jlink):
    id_bytes, hdr_version, size = struct.unpack(
        HDR_FORMAT, raw[:HDR_SIZE]
    )
    if id_bytes == EXPECTED_ID:
        raw_footer = read_mem(jlink, base + size - FOOTER_SIZE, FOOTER_SIZE)
        footer, flags, reserved = struct.unpack(
            FOOTER_FORMAT, raw_footer[:FOOTER_SIZE]
        )
    else:
        footer = "INVL"
        flags = 0
    return {
        "id": id_bytes,
        "hdr_version": hdr_version,
        "size": size,
        "footer": footer, 
        "flags": flags
    }


def read_mem(jlink, addr, nbytes):
    """Read nbytes from target memory as a bytes object."""
    # read_mem8 returns a list of ints (bytes). Robust across pylink versions.
    data = jlink.memory_read8(addr, nbytes)
    return bytes(data)


def write_mem(jlink, addr, data):
    """Write bytes to target flash/RRAM, preferring the flash-aware path.

    On the nRF54L the coredump partition lives in RRAM, which is byte
    rewritable (no erase-before-write needed). We try flash_write8 first
    (routes through the flash loader, the defensible choice for any flash),
    and fall back to memory_write8 if that method isn't available in this
    pylink version.
    """
    payload = list(data)
    try:
        jlink.flash_write8(addr, payload)
        return
    except AttributeError:
        pass
    except Exception as e:
        err(f"WARNING: flash_write8 failed ({e}); trying memory_write8.")
    jlink.memory_write8(addr, payload)


def clear_magic(jlink, base):
    """Overwrite the 4-byte 'CORE' magic with 0x00 and verify the write took.

    Returns True on verified clear, False otherwise.

    NOTE: this invalidates the header so the backend's HAS_STORED_DUMP query
    reports no dump. It does NOT run the backend's own erase routine. If your
    NCS version's query validates more than the magic, this is still expected
    to read as "no valid dump", but the firmware-side `coredump erase` /
    COREDUMP_CMD_ERASE is the canonical clear if you hit any ambiguity.
    """
    write_mem(jlink, base, b"\x00\x00\x00\x00")
    # Read back to confirm -- a write that silently didn't stick is the
    # worst outcome (stale dump survives or next dump is mishandled).
    check = read_mem(jlink, base, 4)
    if check == b"\x00\x00\x00\x00":
        print(f"cleared coredump magic at 0x{base:x} (verified).")
        return True
    err(f"clear FAILED: magic at 0x{base:x} reads {check!r} after write, "
        f"expected b'\\x00\\x00\\x00\\x00'. The write did not stick -- the dump is "
        f"NOT cleared. Consider the firmware-side `coredump erase` instead.")
    return False


def main():
    p = argparse.ArgumentParser(
        description="Read+extract a Zephyr flash-partition coredump over SWD "
                    "via J-Link/pylink, parsing the size on the fly."
    )
    p.add_argument("address", help="partition base address, e.g. 0x14c000")
    p.add_argument("output", nargs="?", default=None,
                   help="output file for the extracted payload "
                        "(not required with --info or --clear-only)")
    p.add_argument("--device", default=DEFAULT_DEVICE,
                   help=f"J-Link device name (default: {DEFAULT_DEVICE})")
    p.add_argument("--speed", type=int, default=4000,
                   help="SWD speed in kHz (default: 4000)")
    p.add_argument("--serial", type=int, default=None,
                   help="J-Link probe serial number (if multiple connected)")
    p.add_argument("--raw", default=None,
                   help="also save the raw header+payload region here")
    p.add_argument("--max-payload", type=lambda x: int(x, 0), default=0x100000,
                   help="sanity cap on payload size (default 0x100000)")
    p.add_argument("--force", action="store_true",
                   help="write output even if validation fails")
    p.add_argument("--clear", action="store_true",
                   help="after a successful extraction, clear the stored dump "
                        "by zeroing the 'CD' magic (with read-back verify)")
    p.add_argument("--clear-only", action="store_true",
                   help="do not extract; just clear the stored dump's magic "
                        "and exit")
    p.add_argument("--info", action="store_true",
                   help="report whether a coredump is stored and print header "
                        "details, without extracting or modifying anything. "
                        "Exit code: 0 if a valid dump is present, 1 otherwise.")
    args = p.parse_args()

    if not (args.info or args.clear_only) and args.output is None:
        err("output file required (unless --info or --clear-only is used).")
        return 1
    if args.info and args.clear_only:
        err("--info and --clear-only are mutually exclusive.")
        return 1

    base = int(args.address, 0)

    try:
        import pylink
    except ImportError:
        err("pylink not installed. Run: pip install pylink-square "
            "(in a venv if your Python is externally managed).")
        return 1

    jlink = pylink.JLink()
    try:
        jlink.open(serial_no=args.serial)
        jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
        jlink.connect(args.device, speed=args.speed)
        if not jlink.connected():
            err("failed to connect to target.")
            return 1
        print(f"connected: {args.device}, "
              f"core id 0x{jlink.core_id():x}, speed {args.speed} kHz")

        # --clear-only: verify a dump is actually present, then clear and exit.
        if args.clear_only:
            magic = read_mem(jlink, base, 4)
            if magic != EXPECTED_ID and not args.force:
                err(f"no dump to clear: magic at 0x{base:x} is {magic!r}, "
                    f"not {EXPECTED_ID!r}. Use --force to write zeros anyway.")
                return 1
            return 0 if clear_magic(jlink, base) else 1

        # --info: report dump presence and header details, then exit.
        if args.info:
            raw_hdr = read_mem(jlink, base, HDR_SIZE)
            hdr = parse_header(raw_hdr, base, jlink)

            # An erased flash header (all 0xFF) or a cleared magic both mean
            # "no dump". The magic check is the primary signal.
            if hdr["id"] == EXPECTED_ID and hdr["footer"] == EXPECTED_FOOTER_ID:
                # Plausibility check on size to distinguish a real dump from
                # garbage that happens to start with 'CD'.
                size_ok = 0 < hdr["size"] <= args.max_payload
                if size_ok:
                    print(f"coredump FOUND at 0x{base:x}")
                    print(f"  hdr_version  : {hdr['hdr_version']}")
                    print(f"  payload size : {hdr['size']} "
                          f"(0x{hdr['size']:x}) bytes")
                    return 0
                err(f"header magic is 'CORE' but size {hdr['size']} "
                    f"(0x{hdr['size']:x}) is implausible; treating as no "
                    f"valid dump.")
                return 1

            if hdr["id"] == b"\x00\x00\x00\x00":
                print(f"no coredump at 0x{base:x} (magic cleared to zero -- "
                      f"previously extracted and cleared).")
            elif hdr["id"] == b"\xff\xff\xff\xff":
                print(f"no coredump at 0x{base:x} (erased flash).")
            else:
                print(f"no coredump at 0x{base:x} (magic is {hdr['id']!r}, {hdr['footer']!r}"
                      f"not {EXPECTED_ID!r}, {EXPECTED_FOOTER_ID!r}).")
            return 1

        # 1) Read just the header.
        raw_hdr = read_mem(jlink, base, HDR_SIZE)
        hdr = parse_header(raw_hdr, base, jlink)

        print("flash coredump header:")
        print(f"  id           : {hdr['id']!r}")
        print(f"  footer       : {hdr['footer']!r}")
        print(f"  hdr_version  : {hdr['hdr_version']}")
        print(f"  payload size : {hdr['size']} (0x{hdr['size']:x}) bytes")
        print(f"  flags        : {hdr['flags']:x}")

        # 2) Validate before reading a potentially bogus length.
        valid = True
        if hdr["id"] != EXPECTED_ID or hdr["footer"] != EXPECTED_FOOTER_ID:
            err(f"bad magic {hdr['id']!r}, {hdr['footer']!r} (expected {EXPECTED_ID!r}, {EXPECTED_FOOTER_ID!r}); "
                f"wrong address or no dump stored.")
            valid = False
        if hdr["size"] in (0, 0xFFFFFFFF):
            err("no coredump stored (size is 0 or erased flash).")
            valid = False
        if hdr["size"] > args.max_payload:
            err(f"payload size 0x{hdr['size']:x} exceeds --max-payload "
                f"0x{args.max_payload:x}; refusing huge read. "
                f"Check struct field widths / address.")
            valid = False
        if hdr["error"] != 0:
            err(f"WARNING: backend recorded error {hdr['error']}; "
                f"payload may be incomplete.")

        if not valid and not args.force:
            err("validation failed; aborting. Use --force to override.")
            return 1

        payload_size = hdr["size"]
        if payload_size in (0, 0xFFFFFFFF) or payload_size > args.max_payload:
            # Only reachable with --force; clamp to something readable.
            payload_size = min(args.max_payload, 0x10000)
            err(f"forcing read of {payload_size} bytes.")

        # 3) Read exactly header + payload.
        # total = HDR_SIZE + payload_size
        total = payload_size #payload_size includes header and footer
        region = read_mem(jlink, base, total)

        if args.raw:
            with open(args.raw, "wb") as f:
                f.write(region)
            print(f"wrote raw region ({total} bytes) to {args.raw}")

        payload = region[HDR_SIZE:payload_size - FOOTER_SIZE]

        with open(args.output, "wb") as f:
            f.write(payload)
        print(f"\nwrote {len(payload)} bytes to {args.output}")

        # Only clear after the payload is safely written to disk, so a clear
        # never destroys a dump we failed to save.
        if args.clear:
            if not clear_magic(jlink, base):
                err("extraction succeeded but clear failed; dump still on "
                    "device.")
                return 1

        print("next:")
        print(f"  python zephyr/scripts/coredump/coredump_gdbserver.py "
              f"build/zephyr/zephyr.elf {args.output}")
        return 0

    except Exception as e:
        err(f"J-Link operation failed: {e}")
        return 1
    finally:
        try:
            jlink.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
