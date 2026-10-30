# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Compare independent FWAV/BWAV decoding, including a second pass through every loop."""
import argparse
import hashlib
import json
import subprocess
import wave
from pathlib import Path


def verify(args):
    report = {}
    args.output.mkdir(parents=True, exist_ok=True)
    sounds = json.loads((args.champions / 'audio-receipt.json').read_text())['sounds']
    for cue, info in sounds.items():
        decoded = []
        extensions = ('bfwav', 'bwav') if (args.champions / 'audio' / f'{cue}.bfwav').exists() else ('bwav',)
        for extension in extensions:
            target = args.output / f'{cue}-{extension}.wav'
            options = ['-l', '2', '-f', '0'] if info['loop'] else ['-i']
            subprocess.run([str(args.decoder), *options, '-o', str(target),
                            str(args.champions / 'audio' / f'{cue}.{extension}')],
                           check=True, capture_output=True)
            with wave.open(str(target), 'rb') as source:
                geometry = (source.getnchannels(), source.getframerate(), source.getsampwidth(), source.getnframes())
                decoded.append((geometry, source.readframes(source.getnframes())))
        assert all(value == decoded[0] for value in decoded), f'PCM or geometry changed: {cue}'
        frames = info['samples'] * (2 if info['loop'] else 1) - (info['loop_start'] if info['loop'] else 0)
        assert decoded[0][0] == (info['channels'], info['rate'], 2, frames), cue
        report[cue] = {'channels': info['channels'], 'rate': info['rate'], 'decoded_frames': frames,
                       'loop_tested': info['loop'], 'pcm_sha256': hashlib.sha256(decoded[0][1]).hexdigest()}
    assert len(report) == 21
    (args.output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print('21 Champion sounds decode; original and converted PCM match, including stereo, 32-kHz voices and retained loops')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('champions', 'decoder', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    verify(parser.parse_args())
