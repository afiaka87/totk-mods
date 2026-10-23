# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Preserve owned DSP channels and native BARS records when composing Champion audio."""

from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def align(size, boundary=64):
    return (size + boundary - 1) // boundary * boundary


def convert_fwav(wave):
    if len(wave) < 44 or wave[:6] != b'FWAV\xff\xfe' or u32(wave, 12) != len(wave):
        raise ValueError('expected complete little-endian FWAV')
    info, data = u32(wave, 24), u32(wave, 36)
    if wave[info:info + 4] != b'INFO' or wave[data:data + 4] != b'DATA':
        raise ValueError('missing FWAV section')
    loop = wave[info + 9]
    rate, start, samples = struct.unpack_from('<III', wave, info + 12)
    table = info + 28
    channels = u32(wave, table)
    if (wave[info + 8] != 2 or loop not in (0, 1) or channels not in (1, 2)
            or rate not in (32000, 48000) or not 0 <= start < samples <= rate * 120):
        raise ValueError('unsupported DSP waveform geometry')
    payloads, contexts, coefficients, loop_contexts = [], [], [], []
    for index in range(channels):
        channel = table + u32(wave, table + 8 + index * 8)
        coefficient = channel + u32(wave, channel + 12)
        coefficients.append(struct.unpack_from('<16h', wave, coefficient))
        contexts.append(struct.unpack_from('<Hhh', wave, coefficient + 32))
        loop_contexts.append(struct.unpack_from('<Hhh', wave, coefficient + 38))
        offset = data + 8 + u32(wave, channel + 4)
        size = (samples + 13) // 14 * 8
        payload = wave[offset:offset + size]
        if len(payload) != size or payload[0] != contexts[-1][0]:
            raise ValueError('invalid DSP payload or initial predictor')
        payloads.append(payload)
    first = align(16 + channels * 76)
    stride = align(len(payloads[0]))
    result = bytearray(struct.pack('<4sHHIHH', b'BWAV', 0xfeff, 1,
                                  zlib.crc32(b''.join(payloads)), 0, channels))
    for index in range(channels):
        offset = first + index * stride
        result.extend(struct.pack('<HHIII16hIIIIIHhhH', 1, index if channels == 2 else 2,
                                  rate, samples, samples, *coefficients[index], offset, offset,
                                  1, samples if loop else 0xffffffff, start, *contexts[index], 0))
    result.extend(b'\0' * (first - len(result)))
    for payload in payloads:
        result.extend(payload)
        result.extend(b'\0' * (align(len(result)) - len(result)))
    return bytes(result), {'rate': rate, 'samples': samples, 'channels': channels, 'loop': bool(loop),
                           'loop_start': start, 'loop_contexts': loop_contexts,
                           'source_sha256': hashlib.sha256(wave).hexdigest(),
                           'channel_sha256': [hashlib.sha256(p).hexdigest() for p in payloads]}


@dataclass(frozen=True)
class Sound:
    name: str
    metadata: bytes
    wave: bytes


def sound_record(bank, name, wave, loop):
    channels = struct.unpack_from('<H', wave, 14)[0]
    donor_name = {1: 'Sys_HeartRecovery_00', 2: 'Sys_Pouch_ZonauGearCapsule_TakeOutReduce'}[channels]
    donor = next(e for e in bank.entries if e.name == donor_name)
    if (donor.flags != 2 or donor.metadata[6:8] != b'\0\x05'
            or donor.metadata[0x31] != channels
            or any(u32(donor.metadata, p) for p in (0x0c, 0x14, 0x18, 0x1c, 0x20))):
        raise ValueError('native audio donor is not a simple embedded record')
    name_offset = 0x24 + u32(donor.metadata, 0x24)
    metadata = bytearray(donor.metadata[:name_offset] + name.encode('ascii') + b'\0')
    metadata.extend(b'\0' * (-len(metadata) % 4))
    struct.pack_into('<I', metadata, 8, len(metadata))
    struct.pack_into('<I', metadata, 0x28, zlib.crc32(name.encode('ascii')))
    struct.pack_into('<I', metadata, 0x2c, 2 | (4 if loop else 0))
    struct.pack_into('<f', metadata, u32(metadata, 0x10) + 4, 1.0)
    return Sound(name, bytes(metadata), wave)


def append_records(bank, sounds):
    checksums = [e.checksum for e in bank.entries] + [zlib.crc32(s.name.encode('ascii')) for s in sounds]
    if len(set(checksums)) != len(checksums):
        raise ValueError('duplicate sound name or lookup hash')
    count = len(checksums)
    output = bytearray(bank.data[:16] + b'\0' * (12 * count) + bank.index_padding)
    records = []
    for entry in sorted(bank.entries, key=lambda e: e.metadata_offset):
        records.append([entry.checksum, len(output), entry.audio_offset])
        output.extend(entry.metadata)
    added = []
    for sound in sounds:
        if sound.wave[:6] != b'BWAV\xff\xfe' or len(sound.wave) < 128:
            raise ValueError('invalid appended waveform')
        added.append([zlib.crc32(sound.name.encode('ascii')), len(output), 0])
        output.extend(sound.metadata)
    output.extend(b'\0' * (-len(output) % 64))
    delta = len(output) - bank.audio_start
    output.extend(bank.data[bank.audio_start:])
    for row in records:
        row[2] += delta
    for sound, row in zip(sounds, added, strict=True):
        output.extend(b'\0' * (-len(output) % 64))
        row[2] = len(output)
        output.extend(sound.wave)
    struct.pack_into('<I', output, 4, len(output))
    struct.pack_into('<I', output, 12, count)
    for index, row in enumerate(sorted(records + added)):
        struct.pack_into('<I', output, 16 + index * 4, row[0])
        struct.pack_into('<II', output, 16 + count * 4 + index * 8, *row[1:])
    return bytes(output)
