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
    0  4  id[4] = 'T','H','R','D'
    4  2  size        (uint16 LE)
    6  1  task_count  (1 byte amount of threads)
    7  1  unused      (1 byte)
    8  tasks...
"""

import argparse
import struct
import sys
import shutil
import subprocess
import json

HDR_FORMAT = "<4sHBc"
HDR_SIZE = struct.calcsize(HDR_FORMAT)  # 16
EXPECTED_ID = b"DRHT"

TASK_FORMAT = "<16s10I"
TASK_SIZE = struct.calcsize(TASK_FORMAT) 

# nRF54L15 application core. Adjust if your device/core name differs.
DEFAULT_DEVICE = "nRF54L15_M33"


def err(msg):
    print(f"error: {msg}", file=sys.stderr)


def parse_header(raw):
    id_bytes, size, task_count, unused = struct.unpack(
        HDR_FORMAT, raw[:HDR_SIZE]
    )
    return {
        "id": id_bytes,
        "size": size,
        "task_count": task_count,
    }

def parse_task(raw):
    # Unpack the entire block into a single tuple
    unpacked = struct.unpack(TASK_FORMAT, raw[:TASK_SIZE])
    
    # 1. Strip trailing null bytes from the fixed-width char array
    raw_name = unpacked[0]
    name = raw_name.split(b'\x00')[0].decode('utf-8', errors='ignore')
    
    # 2. Slice out the frames and collect them until we hit a 0 marker
    raw_frames = unpacked[1:]
    valid_frames = []
    for frame in raw_frames:
        if frame == 0:
            break
        valid_frames.append(frame)
        
    return {
        "name": name,
        "stack": valid_frames
    }


def read_mem(jlink, addr, nbytes):
    """Read nbytes from target memory as a bytes object."""
    data = jlink.memory_read8(addr, nbytes)
    return bytes(data)


def write_mem(jlink, addr, data):
    """Write bytes to target flash/RRAM, preferring the flash-aware path."""
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
    """Overwrite the 4-byte 'THRD' magic with 0x00 and verify the write took."""
    write_mem(jlink, base, b"\x00\x00\x00\x00")
    check = read_mem(jlink, base, 4)
    if check == b"\x00\x00\x00\x00":
        print(f"cleared breadcrumbs magic at 0x{base:x} (verified).")
        return True
    err(f"clear FAILED: magic at 0x{base:x} reads {check!r} after write, "
        f"expected b'\\x00\\x00\\x00\\x00'.")
    return False

def parse_and_symbolize_tasks(payload, total_tasks, elf_target):
    # 1. Quick sanity check to ensure llvm-symbolizer is installed
    if not shutil.which("llvm-symbolizer-21"):
        print("Error: 'llvm-symbolizer-21' not found in your system PATH.")
        return

    # Zephyr OS fatal error reason lookup table
    ZEPHYR_REASONS = {
        0: "K_ERR_CPU_EXCEPTION",
        1: "K_ERR_SPURIOUS_IRQ",
        2: "K_ERR_STACK_CHK_FAIL",
        3: "K_ERR_KERNEL_OOPS",
        4: "K_ERR_KERNEL_PANIC",
        16: "K_ERR_ARM_MEM_GENERIC",
        17: "K_ERR_ARM_MEM_STACKING",
        18: "K_ERR_ARM_MEM_UNSTACKING",
        19: "K_ERR_ARM_MEM_DATA_ACCESS",
        20: "K_ERR_ARM_MEM_INSTRUCTION_ACCESS",
        21: "K_ERR_ARM_MEM_FP_LAZY_STATE_PRESERVATION",
        22: "K_ERR_ARM_BUS_GENERIC",
        23: "K_ERR_ARM_BUS_STACKING",
        24: "K_ERR_ARM_BUS_UNSTACKING",
        25: "K_ERR_ARM_BUS_PRECISE_DATA_BUS",
        26: "K_ERR_ARM_BUS_IMPRECISE_DATA_BUS",
        27: "K_ERR_ARM_BUS_INSTRUCTION_BUS",
        28: "K_ERR_ARM_BUS_FP_LAZY_STATE_PRESERVATION",
        29: "K_ERR_ARM_USAGE_GENERIC",
        30: "K_ERR_ARM_USAGE_DIV_0",
        31: "K_ERR_ARM_USAGE_UNALIGNED_ACCESS",
        32: "K_ERR_ARM_USAGE_STACK_OVERFLOW",
        33: "K_ERR_ARM_USAGE_NO_COPROCESSOR",
        34: "K_ERR_ARM_USAGE_ILLEGAL_EXC_RETURN",
        35: "K_ERR_ARM_USAGE_ILLEGAL_EPSR",
        36: "K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION",
        37: "K_ERR_ARM_SECURE_GENERIC",
        38: "K_ERR_ARM_SECURE_ENTRY_POINT",
        39: "K_ERR_ARM_SECURE_INTEGRITY_SIGNATURE",
        40: "K_ERR_ARM_SECURE_EXCEPTION_RETURN",
        41: "K_ERR_ARM_SECURE_ATTRIBUTION_UNIT",
        42: "K_ERR_ARM_SECURE_TRANSITION",
        43: "K_ERR_ARM_SECURE_LAZY_STATE_PRESERVATION",
        44: "K_ERR_ARM_SECURE_LAZY_STATE_ERROR",
        45: "K_ERR_ARM_UNDEFINED_INSTRUCTION (Cortex-A/R)",
        46: "K_ERR_ARM_ALIGNMENT_FAULT",
        47: "K_ERR_ARM_BACKGROUND_FAULT",
        48: "K_ERR_ARM_PERMISSION_FAULT",
        49: "K_ERR_ARM_SYNC_EXTERNAL_ABORT",
        50: "K_ERR_ARM_ASYNC_EXTERNAL_ABORT",
        51: "K_ERR_ARM_SYNC_PARITY_ERROR",
        52: "K_ERR_ARM_ASYNC_PARITY_ERROR",
        53: "K_ERR_ARM_DEBUG_EVENT",
        54: "K_ERR_ARM_TRANSLATION_FAULT",
        55: "K_ERR_ARM_UNSUPPORTED_EXCLUSIVE_ACCESS_FAULT"
    }

    # 2. Start a single persistent symbolizer process
    try:
        symbolizer = subprocess.Popen(
            ["llvm-symbolizer-21", f"--obj={elf_target}", 
             "--functions=linkage", "--demangle", "--output-style=JSON"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1
        )
    except Exception as e:
        print(f"Failed to start llvm-symbolizer: {e}")
        symbolizer = None

    print(f"Parsing {total_tasks} tasks using ELF: {elf_target}\n")
    
    for i in range(total_tasks):
        start_offset = i * TASK_SIZE
        end_offset = start_offset + TASK_SIZE
        
        if end_offset > len(payload):
            print(f"Error: Payload ended unexpectedly at task {i}!")
            break
            
        task = parse_task(payload[start_offset:end_offset])
        print(f"[{i}] Thread: {task['name']}")
        
        if not task["stack"]:
            print("    Stack Frames: [Empty / No active frames]")
            print("-" * 60)
            continue
            
        print("    Stack Frames:")
        
        stack = task["stack"]
        is_fault = len(stack) > 0 and stack[0] < 100
        
        if is_fault:
            reason = stack[0]
            exc_return = stack[1] if len(stack) > 1 else 0
            cfsr = stack[2] if len(stack) > 2 else 0
            
            reason_str = ZEPHYR_REASONS.get(reason, f"Unknown Reason ({reason})")
            print(f"      [Zephyr Fault Frame Block Detected]")
            print(f"        Reason:      {reason_str} ({reason})")
            
            # Decode EXC_RETURN context bits for Cortex-M33 (ARMv8-M Main Extension)
            print(f"        EXC_RETURN:  0x{exc_return:08X}")
            print(f"          - Return Security State: {'Non-secure' if (exc_return & 0x1) else 'Secure'}")
            print(f"          - Return Stack Pointer:  {'PSP' if (exc_return & 0x4) else 'MSP'}")
            print(f"          - Return Mode:           {'Thread' if (exc_return & 0x8) else 'Handler'}")
            print(f"          - FP Context Allocated:  {'No' if (exc_return & 0x16) else 'Yes'}")
            print(f"          - Callee Registers Rule: {'Default (Not on stack)' if (exc_return & 0x32) else 'Stacked Extended Context'}")
            print(f"          - Exception Exec State:  {'Secure' if (exc_return & 0x64) else 'Non-secure'}")
            
            # Decode CFSR Register
            if cfsr == 0xFFFFFFFF:
                print(f"        CFSR:        0x00000000 (Originally 0 / No active fault bits)")
            else:
                print(f"        CFSR:        0x{cfsr:08X}")
                mmfsr = cfsr & 0xFF
                bfsr = (cfsr >> 8) & 0xFF
                ufsr = (cfsr >> 16) & 0xFFFF
                
                if mmfsr:
                    print("          [MemManage Fault Status]:")
                    if mmfsr & 0x01: print("            - IACCVIOL: Instruction access violation")
                    if mmfsr & 0x02: print("            - DACCVIOL: Data access violation")
                    if mmfsr & 0x08: print("            - MUNSTKERR: Fault occurred on unstacking")
                    if mmfsr & 0x10: print("            - MSTKERR: Fault occurred on stacking")
                    if mmfsr & 0x20: print("            - MLSPERR: Fault during lazy FP preservation")
                    if mmfsr & 0x80: print("            - MMARVALID: MMAR register holds valid address")
                if bfsr:
                    print("          [Bus Fault Status]:")
                    if bfsr & 0x01: print("            - IBUSERR: Instruction bus error")
                    if bfsr & 0x02: print("            - PRECISERR: Precise data bus error")
                    if bfsr & 0x04: print("            - IMPRECISERR: Imprecise data bus error")
                    if bfsr & 0x08: print("            - UNSTKERR: Fault occurred on unstacking")
                    if bfsr & 0x10: print("            - STKERR: Fault occurred on stacking")
                    if bfsr & 0x20: print("            - LSPERR: Fault during lazy FP preservation")
                    if bfsr & 0x80: print("            - BFARVALID: BFAR register holds valid address")
                if ufsr:
                    print("          [Usage Fault Status]:")
                    if ufsr & 0x0001: print("            - UNDEFINSTR: Undefined instruction executed")
                    if ufsr & 0x0002: print("            - INVSTATE: Invalid execution state (EPSR.T or ISA change)")
                    if ufsr & 0x0004: print("            - INVPC: Invalid PC load by EXC_RETURN")
                    if ufsr & 0x0008: print("            - NOCP: No coprocessor access permitted")
                    if ufsr & 0x0010: print("            - STKOF: Stack overflow detected")
                    if ufsr & 0x0100: print("            - UNALIGNED: Unaligned memory access")
                    if ufsr & 0x0200: print("            - DIVBYZERO: Divide by zero")
            
            # Drop the 3 fake handler frames to print real symbols
            frames_to_process = stack[3:]
            if frames_to_process:
                print("        Genuine Stack Frames:")
        else:
            frames_to_process = stack

        for frame in frames_to_process:
            hex_addr = f"0x{frame:08X}"
            func_name = "??"
            file_line = "xx"
            if symbolizer is not None:
                symbolizer.stdin.write(f"{hex_addr}\n")
                symbolizer.stdin.flush()
                
                response_line = symbolizer.stdout.readline().strip()
                data = json.loads(response_line)
                if isinstance(data, dict):
                    symbols = data.get("Symbol", [])
                elif isinstance(data, list) and len(data) > 0:
                    symbols = data[0].get("Symbol", [])
                else:
                    symbols = []
                if symbols:
                    for idx, sym in enumerate(symbols):
                        func_name = sym.get("FunctionName", "??")
                        file_name = sym.get("FileName", "??")
                        line_num = sym.get("Line", 0)

                        if func_name == "??": func_name = "<unknown_func>"
                        if file_name == "??": file_name = "<unknown_file>"

                        if idx == 0:
                            print(f"      {hex_addr} -> {func_name} ({file_name}:{line_num})")
                        else:
                            print(f"                  [Inlined] -> {func_name} ({file_name}:{line_num})")
                else:
                    print(f"No 'Symbol' entry in {isinstance(data, dict)}")
            else:
                print(f"      {hex_addr} -> {func_name} ({file_line})")
            
        print("-" * 60)

    if symbolizer is not None:
        symbolizer.stdin.close()
        symbolizer.terminate()
        symbolizer.wait()

def main():
    p = argparse.ArgumentParser(
        description="Read+extract a Zephyr flash-partition breadcrumbs over SWD "
                    "via J-Link/pylink, parsing the size on the fly."
    )
    p.add_argument("address", help="partition base address, e.g. 0x14c000")
    p.add_argument("output", nargs="?", default=None,
                   help="output file for the extracted payload ")
    p.add_argument("--elf", default=None,
                   help="elf file for symbolization payload ")
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
                   help="after a successful extraction, clear the stored dump")
    p.add_argument("--clear-only", action="store_true",
                   help="do not extract; just clear the stored dump's magic and exit")
    p.add_argument("--info", action="store_true",
                   help="report whether a coredump is stored and print header details.")
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
        err("pylink not installed. Run: pip install pylink-square")
        return 1

    jlink = pylink.JLink()
    try:
        jlink.open(serial_no=args.serial)
        jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
        jlink.connect(args.device, speed=args.speed)
        if not jlink.connected():
            err("failed to connect to target.")
            return 1
        print(f"connected: {args.device}, core id 0x{jlink.core_id():x}, speed {args.speed} kHz")

        if args.clear_only:
            magic = read_mem(jlink, base, 4)
            if magic != EXPECTED_ID and not args.force:
                err(f"no dump to clear: magic at 0x{base:x} is {magic!r}, not {EXPECTED_ID!r}.")
                return 1
            return 0 if clear_magic(jlink, base) else 1

        if args.info:
            raw_hdr = read_mem(jlink, base, HDR_SIZE)
            hdr = parse_header(raw_hdr)
            if hdr["id"] == EXPECTED_ID:
                size_ok = 0 < hdr["size"] <= args.max_payload
                if size_ok:
                    print(f"breadcrumbs FOUND at 0x{base:x}")
                    print(f"  payload size : {hdr['size']} (0x{hdr['size']:x}) bytes")
                    print(f"  task count   : {hdr['task_count']}")
                    return 0
                err(f"header magic is 'THRD' but size {hdr['size']} is implausible.")
                return 1
            return 1

        raw_hdr = read_mem(jlink, base, HDR_SIZE)
        hdr = parse_header(raw_hdr)

        print("flash breadcrumbs header:")
        print(f"  id           : {hdr['id']!r}")
        print(f"  task_count   : {hdr['task_count']}")
        print(f"  payload size : {hdr['size']} (0x{hdr['size']:x}) bytes")

        valid = True
        if hdr["id"] != EXPECTED_ID:
            err(f"bad magic {hdr['id']!r} (expected {EXPECTED_ID!r}).")
            valid = False
        if hdr["size"] in (0, 0xFFFFFFFF):
            err("no coredump stored.")
            valid = False
        if hdr["size"] > args.max_payload:
            err(f"payload size 0x{hdr['size']:x} exceeds max threshold.")
            valid = False
        if not valid and not args.force:
            err("validation failed; aborting.")
            return 1

        payload_size = hdr["size"] - HDR_SIZE
        if payload_size in (0, 0xFFFFFFFF) or payload_size > args.max_payload:
            payload_size = min(args.max_payload, 0x10000)

        total = HDR_SIZE + payload_size
        region = read_mem(jlink, base, total)

        if args.raw:
            with open(args.raw, "wb") as f:
                f.write(region)

        payload = region[HDR_SIZE:HDR_SIZE + payload_size]

        with open(args.output, "wb") as f:
            f.write(payload)
        print(f"\nwrote {len(payload)} bytes to {args.output}")
        parse_and_symbolize_tasks(payload, hdr["task_count"], args.elf)
        
        if args.clear:
            if not clear_magic(jlink, base):
                return 1

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
