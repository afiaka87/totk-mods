# SPDX-License-Identifier: MIT
"""Compare exact NSO mappings and configured rings; optionally summarize a boot log."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

def nso_profile(data):
    if len(data) < 0x100 or data[:4] != b"NSO0":
        raise ValueError("Expected an NSO0 header")
    segments = {}
    for name, offset in (("text", 0x10), ("rodata", 0x20), ("data", 0x30)):
        _, address, size = struct.unpack_from("<III", data, offset)
        if address % 4096:
            raise ValueError("Unexpected unaligned NSO segment")
        segments[name] = {"offset": address, "bytes": size}
    bss = struct.unpack_from("<I", data, 0x3C)[0]
    data_end = segments["data"]["offset"] + segments["data"]["bytes"] + bss
    end = max(data_end, *(s["offset"] + s["bytes"] for s in segments.values()))
    return {"sha256": hashlib.sha256(data).hexdigest(), "file_bytes": len(data),
            "segments": segments, "bss_bytes": bss, "mapped_bytes": (end + 4095) & ~4095}

def boot_samples(log):
    pattern = r"MEMORY phase=(\S+) total=(\d+) used=(\d+) peak=(\d+) system=(\d+)/(\d+) rc=([0-9a-f,]+)"
    return [{"phase": m[0], "total": int(m[1]), "used": int(m[2]),
             "peak": int(m[3]), "system_used": int(m[4]), "system_total": int(m[5]),
             "results": [int(v, 16) for v in m[6].split(",")]} for m in re.findall(pattern, log)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-nso", type=Path, required=True)
    parser.add_argument("--candidate-nso", type=Path, required=True)
    parser.add_argument("--baseline-ring", type=int, default=45184000)
    parser.add_argument("--candidate-ring", type=int, default=204800)
    parser.add_argument("--eden-log", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    baseline = nso_profile(args.baseline_nso.read_bytes())
    candidate = nso_profile(args.candidate_nso.read_bytes())
    report = {"baseline": baseline, "candidate": candidate,
              "mapped_reduction_bytes": baseline["mapped_bytes"] - candidate["mapped_bytes"],
              "baseline_configured_ring_bytes": args.baseline_ring,
              "candidate_configured_ring_bytes": args.candidate_ring,
              "configured_ring_reduction_bytes": args.baseline_ring - args.candidate_ring,
              "boot_samples": boot_samples(args.eden_log.read_text(errors="replace")) if args.eden_log else [],
              "caveat": "NSO mapping and configured ring are separate budgets. Not measured GPU time or physical Switch headroom. Kernel samples include the whole game; compare matching scenes/settings."}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
