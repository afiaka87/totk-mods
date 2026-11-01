"""Verify the original SDK debug wrapper and resolve it relative to an imported export."""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

import numpy as np


def inspect(sdk_path: Path, tick_got: int) -> dict:
    from nso_decompress import decompress_nso
    header = sdk_path.read_bytes()[:0x100]
    build_id = header[0x40:0x54].hex().upper()
    # Matches the SDK the patch01 Citron boot loaded beside main 1.2.1.
    if header[:4] != b"NSO0" or build_id != "B9046C31EB5D31271BE970FE732D38DF49C6AA21":
        raise ValueError("SDK does not match the verified TotK 1.2.1 module")
    data = decompress_nso(str(sdk_path))
    text_end = struct.unpack_from("<I", header, 0x18)[0]
    digest = hashlib.sha256(data[:text_end]).digest()
    if digest != header[0xa0:0xc0]:
        raise ValueError("SDK text hash does not match its native header")
    mod = struct.unpack_from("<I", data, 4)[0]
    cursor = mod + struct.unpack_from("<i", data, mod + 4)[0]
    tags = {}
    while True:
        tag, value = struct.unpack_from("<qQ", data, cursor)
        cursor += 16
        if tag == 0:
            break
        tags[tag] = value
    count = struct.unpack_from("<I", data, tags[4] + 4)[0]
    tick = None
    for i in range(count):
        name_offset, _, _, section, value, size = struct.unpack_from("<IBBHQQ", data, tags[6] + i * 24)
        start = tags[5] + name_offset
        name = data[start:data.index(b"\0", start)].decode()
        if section and name == "_ZN2nn2os13GetSystemTickEv":
            tick = value
            if size != 8:
                raise ValueError("Native tick export changed")
    words = np.frombuffer(data, dtype="<u4", count=text_end // 4)
    sites = [int(i * 4) for i in np.flatnonzero(words == 0xd40004e1)
             if words[i + 1] == 0xd65f03c0]
    if tick is None or len(sites) != 1:
        raise ValueError("SDK debug SVC/RET wrapper is not unique or tick export is absent")
    offset = sites[0]
    return {"sdk_build_id": build_id, "sdk_text_sha256": digest.hex(), "tick_got": tick_got,
            "tick_export": tick, "debug_export": offset, "debug_from_tick": offset - tick,
            "debug_words": [int(words[offset // 4]), int(words[offset // 4 + 1])]}
