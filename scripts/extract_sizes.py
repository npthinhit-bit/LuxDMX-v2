#!/usr/bin/env python3
"""Extract Flash and RAM usage from PlatformIO build artifacts (.elf or .map).

Parses the ELF section sizes to compute:
  - Flash: .flash.text + .flash.rodata + .flash.rodata_noload + .dram0.data (stored in flash image)
  - RAM:  .dram0.bss + .dram0.data + .iram0.text + .iram0.vectors

Works with ESP32/ESP32-S3 ELF files by recognizing ESP-IDF-specific section
names (.flash.*, .dram0.*, .iram0.*). Falls back to standard GNU sections
(.text, .data, .bss, .rodata) for non-ESP targets.

Outputs a JSON report. In CI, this is used to:
  1. Check thresholds (flash <= 90%, RAM <= 85% of available)
  2. Compare against a baseline for delta tracking

Usage:
    python3 scripts/extract_sizes.py <env_name> <elf_path> [--output report.json]
    python3 scripts/extract_sizes.py <env_name> <map_path> --from-map --output report.json
"""
import sys
import os
import json
import subprocess
import re
import argparse
import pathlib


# Memory limits per board family
# ESP32 (classic): 520 KB SRAM, 4 MB flash
# ESP32-S3: 512 KB SRAM (internal), 8 MB PSRAM (external, if enabled)
# Flash: 4 MB (most envs) or 16 MB (esp32s3_n16r8_eth)
FLASH_AVAILABLE_BYTES = 4 * 1024 * 1024
FLASH_AVAILABLE_16M   = 16 * 1024 * 1024
# ESP32-S3 has 512 KB SRAM; ESP32 (classic) has 520 KB
RAM_AVAILABLE_BYTES_S3  = 512 * 1024
RAM_AVAILABLE_BYTES_S3  = 512 * 1024
RAM_AVAILABLE_BYTES_ESP32 = 520 * 1024

# Thresholds (fail if exceeded)
FLASH_THRESHOLD_PCT = 90.0
RAM_THRESHOLD_PCT   = 85.0


def get_env_limits(env_name: str) -> tuple:
    """Return (flash_limit, ram_limit) for a given build environment."""
    if env_name == "esp32s3_n16r8_eth":
        flash = FLASH_AVAILABLE_16M
    else:
        flash = FLASH_AVAILABLE_BYTES

    if env_name in ("esp32s3dev", "esp32s3_psram", "esp32s3_n16r8_eth"):
        ram = RAM_AVAILABLE_BYTES_S3
    else:
        # ESP32 classic (esp32dev, wt32eth01)
        ram = RAM_AVAILABLE_BYTES_ESP32

    return flash, ram


def _categorize_section_esp32(name: str, size: int) -> dict:
    """Categorize an ESP32/ESP32-S3 section into flash or ram buckets.

    ESP-IDF section naming convention:
      .flash.text       — code in flash (e.g., 1033332 bytes)
      .flash.rodata     — read-only data in flash (e.g., 565680 bytes)
      .flash.rodata_noload — read-only data not loaded (e.g., 23338 bytes)
      .dram0.data       — initialized RAM (stored in flash image, runs in DRAM)
      .dram0.bss        — uninitialized RAM
      .iram0.text       — code in IRAM
      .iram0.vectors    — interrupt vectors in IRAM
    """
    result = {"flash": 0, "dram": 0, "iram": 0}

    if name.startswith(".flash."):
        result["flash"] = size
    elif name == ".dram0.data" or name == ".dram0.bss":
        result["dram"] = size
    elif name.startswith(".iram0."):
        result["iram"] = size
    elif name in (".rtc.text",):
        result["flash"] = size  # RTC fast memory, negligible
    elif name in (".dport",):
        result["ram"] = size  # DPORT region

    return result


def parse_elf_size(env_name: str, elf_path: str) -> dict:
    """Parse ELF sections using xtensa-esp32-elf-size -A (all sections).

    On CI (ubuntu-latest), the xtensa toolchain is installed by PlatformIO.
    Falls back to standard GNU size if xtensa tools are unavailable.
    """
    # Find the xtensa toolchain size utility
    pio_home = os.path.expanduser("~/.platformio")
    toolchain_paths = []
    for base in ("packages", "tools"):
        pkg_dir = os.path.join(pio_home, base, "toolchain-xtensa-esp-elf")
        if os.path.isdir(pkg_dir):
            nested = os.path.join(pkg_dir, "xtensa-esp-elf", "bin")
            if os.path.isdir(nested):
                toolchain_paths.append(nested)
            direct = os.path.join(pkg_dir, "bin")
            if os.path.isdir(direct):
                toolchain_paths.append(direct)

    # Determine the right toolchain name for this env
    if env_name in ("esp32dev", "wt32eth01"):
        tool_names = ("xtensa-esp32-elf-size", "xtensa-esp-elf-size")
    elif env_name in ("esp32s3dev", "esp32s3_psram", "esp32s3_n16r8_eth"):
        tool_names = ("xtensa-esp32s3-elf-size", "xtensa-esp32-elf-size", "xtensa-esp-elf-size")
    else:
        tool_names = ("xtensa-esp32-elf-size", "xtensa-esp32s3-elf-size", "xtensa-esp-elf-size")

    size_tool = None
    for tp in toolchain_paths:
        for tname in tool_names:
            candidate = os.path.join(tp, tname)
            try:
                subprocess.run([candidate, "--version"], capture_output=True, timeout=5)
                size_tool = candidate
                break
            except (FileNotFoundError, subprocess.TimeoutExpired):
                continue
        if size_tool:
            break

    if not size_tool:
        for tname in tool_names:
            try:
                subprocess.run([tname, "--version"], capture_output=True, timeout=5)
                size_tool = tname
                break
            except (FileNotFoundError, subprocess.TimeoutExpired):
                continue

    if not size_tool:
        raise RuntimeError(
            "No xtensa size tool found. Install the PlatformIO toolchain "
            "or use --from-map with a .map file."
        )

    # Use -A (sysv format) to get per-section sizes
    result = subprocess.run(
        [size_tool, "-A", elf_path],
        capture_output=True, text=True, timeout=30
    )

    if result.returncode != 0:
        raise RuntimeError(f"size tool failed: {result.stderr}")

    # Parse the sysv-format output: each line has "section_name size address"
    sections = {}
    for line in result.stdout.splitlines():
        # Format: "  .section_name   1234  0x12345678"
        parts = line.strip().split()
        if len(parts) >= 2:
            name = parts[0]
            try:
                size = int(parts[1])
                sections[name] = size
            except ValueError:
                continue  # Header lines, etc.

    # Categorize sections (ESP32 approach)
    flash_bytes = 0
    dram_bytes = 0
    iram_bytes = 0

    for name, size in sections.items():
        cat = _categorize_section_esp32(name, size)
        flash_bytes += cat["flash"]
        dram_bytes += cat["dram"]
        iram_bytes += cat["iram"]

    # Also handle standard GNU sections (non-ESP targets) as fallback
    if flash_bytes == 0 and dram_bytes == 0 and iram_bytes == 0:
        if ".text" in sections:
            flash_bytes += sections[".text"]
        if ".rodata" in sections:
            flash_bytes += sections[".rodata"]
        if ".data" in sections:
            dram_bytes += sections[".data"]
            flash_bytes += sections[".data"]  # data stored in flash image too
        if ".bss" in sections:
            dram_bytes += sections[".bss"]

    # Flash = all flash sections + dram0.data (stored in flash image)
    # RAM = dram0.bss + dram0.data + iram0.text + iram0.vectors
    ram_bytes = dram_bytes + iram_bytes

    return {
        "sections_raw": sections,
        "text": sections.get(".flash.text", sections.get(".text", 0)) + sections.get(".iram0.text", 0) + sections.get(".iram0.vectors", 0),
        "data": sections.get(".dram0.data", sections.get(".data", 0)),
        "bss": sections.get(".dram0.bss", sections.get(".bss", 0)),
        "rodata": sections.get(".flash.rodata", 0) + sections.get(".flash.rodata_noload", 0),
        "iram": sections.get(".iram0.text", 0) + sections.get(".iram0.vectors", 0),
        "dram": sections.get(".dram0.data", 0) + sections.get(".dram0.bss", 0),
        "flash": flash_bytes,
        "ram": ram_bytes,
    }


def parse_map_file(map_path: str) -> dict:
    """Parse a PlatformIO/ESP-IDF .map file for section sizes.

    ESP-IDF .map files use a per-object-file section listing format:
      .text           0x00000000        0x0 .pio/build/.../file.cpp.o
    We accumulate sizes per section name across all object files.

    We then categorize ESP32 sections (.flash.*, .dram0.*, .iram0.*) into
    flash and RAM buckets.
    """
    sections = {}
    section_pattern = re.compile(
        r'^\s*(\.[a-z0-9_.]+)\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+',
        re.MULTILINE
    )

    with open(map_path, 'r', errors='replace') as f:
        content = f.read()

    for match in section_pattern.finditer(content):
        name = match.group(1)
        size = int(match.group(2), 16)
        sections[name] = sections.get(name, 0) + size

    # Categorize (same logic as ELF parsing)
    flash_bytes = 0
    dram_bytes = 0
    iram_bytes = 0

    for name, size in sections.items():
        cat = _categorize_section_esp32(name, size)
        flash_bytes += cat["flash"]
        dram_bytes += cat["dram"]
        iram_bytes += cat["iram"]

    # Also accumulate standard GNU sections
    if ".rodata" in sections and "rodata" not in [s.replace(".flash.", "") for s in sections if s.startswith(".flash.")]:
        flash_bytes += sections.get(".rodata", 0)
    if ".data" in sections and ".dram0.data" not in sections:
        dram_bytes += sections.get(".data", 0)
        flash_bytes += sections.get(".data", 0)
    if ".bss" in sections and ".dram0.bss" not in sections:
        dram_bytes += sections.get(".bss", 0)
    if ".text" in sections and ".flash.text" not in sections and ".iram0.text" not in sections:
        flash_bytes += sections.get(".text", 0)

    ram_bytes = dram_bytes + iram_bytes

    return {
        "sections_raw": sections,
        "flash": flash_bytes,
        "ram": ram_bytes,
        "text": sections.get(".flash.text", sections.get(".text", 0)),
        "data": sections.get(".dram0.data", sections.get(".data", 0)),
        "bss": sections.get(".dram0.bss", sections.get(".bss", 0)),
        "rodata": sections.get(".flash.rodata", 0) + sections.get(".flash.rodata_noload", 0),
        "iram": sections.get(".iram0.text", 0) + sections.get(".iram0.vectors", 0),
        "dram": sections.get(".dram0.data", 0) + sections.get(".dram0.bss", 0),
    }


def compute_percentages(sizes: dict, flash_limit: int, ram_limit: int) -> dict:
    """Compute percentage usage of flash and RAM."""
    flash_pct = (sizes["flash"] / flash_limit * 100) if flash_limit > 0 else 0
    ram_pct   = (sizes["ram"]   / ram_limit   * 100) if ram_limit   > 0 else 0
    return {
        "flash_pct": round(flash_pct, 2),
        "ram_pct": round(ram_pct, 2),
    }


def main():
    parser = argparse.ArgumentParser(
        description="Extract Flash/RAM usage from firmware .elf or .map"
    )
    parser.add_argument("env", help="PlatformIO environment name (e.g. esp32s3_psram)")
    parser.add_argument("path", help="Path to firmware.elf or firmware.map")
    parser.add_argument("--from-map", action="store_true", help="Parse as .map file instead of .elf")
    parser.add_argument("--output", "-o", help="Output JSON report path")
    parser.add_argument("--baseline", "-b", help="Path to baseline JSON for delta comparison")
    args = parser.parse_args()

    if not os.path.isfile(args.path):
        print(f"ERROR: file not found: {args.path}", file=sys.stderr)
        sys.exit(1)

    try:
        if args.from_map:
            sizes = parse_map_file(args.path)
        else:
            sizes = parse_elf_size(args.env, args.path)
    except RuntimeError as e:
        # Fall back to .map if .elf parsing fails
        map_path = args.path.replace(".elf", ".map")
        if not args.from_map and os.path.isfile(map_path):
            print(f"WARNING: ELF parse failed ({e}), falling back to .map", file=sys.stderr)
            sizes = parse_map_file(map_path)
        else:
            print(f"ERROR: {e}", file=sys.stderr)
            sys.exit(1)

    flash_limit, ram_limit = get_env_limits(args.env)
    pcts = compute_percentages(sizes, flash_limit, ram_limit)

    report = {
        "env": args.env,
        "elf_or_map": args.path,
        "sections": sizes,
        "limits": {
            "flash_bytes": flash_limit,
            "ram_bytes": ram_limit,
        },
        "usage_pct": pcts,
        "thresholds": {
            "flash_max_pct": FLASH_THRESHOLD_PCT,
            "ram_max_pct": RAM_THRESHOLD_PCT,
        },
        "within_limits": (
            pcts["flash_pct"] <= FLASH_THRESHOLD_PCT
            and pcts["ram_pct"] <= RAM_THRESHOLD_PCT
        ),
    }

    # Delta tracking
    if args.baseline:
        baseline_path = pathlib.Path(args.baseline)
        if baseline_path.is_file():
            with open(baseline_path) as f:
                baseline = json.load(f)
            b_sizes = baseline.get("sections", {})
            report["delta"] = {
                "flash_delta": sizes["flash"] - b_sizes.get("flash", 0),
                "ram_delta":   sizes["ram"]   - b_sizes.get("ram", 0),
                "text_delta":  sizes.get("text", 0)  - b_sizes.get("text", 0),
                "bss_delta":   sizes.get("bss", 0)   - b_sizes.get("bss", 0),
                "data_delta":  sizes.get("data", 0)  - b_sizes.get("data", 0),
            }

    output = json.dumps(report, indent=2)
    print(output)

    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"Report written to {args.output}", file=sys.stderr)

    # Exit code: 0 if within limits, 1 if exceeded
    if not report["within_limits"]:
        print(
            f"FAIL: {args.env} flash={pcts['flash_pct']}% (limit {FLASH_THRESHOLD_PCT}%), "
            f"ram={pcts['ram_pct']}% (limit {RAM_THRESHOLD_PCT}%)",
            file=sys.stderr
        )
        sys.exit(1)

    print(f"OK: {args.env} flash={pcts['flash_pct']}%, ram={pcts['ram_pct']}%")
    sys.exit(0)


if __name__ == "__main__":
    main()
