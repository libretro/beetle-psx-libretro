#!/usr/bin/env python3
"""Write a synthetic two-track CD image (CUE + BIN) for the CHD tests.

  Track 1  MODE2/2352  data sectors: sync, BCD MSF header, mode 2, then a
                       payload of slow-moving noise -- compressible enough
                       that every CD codec stores real compressed hunks
                       rather than falling back to uncompressed ones
  Track 2  AUDIO       16-bit stereo, a sweeping triangle with noise

--variant N rewrites a few scattered data sectors with different content.
A CHD built from the variant against the base image as its parent then
holds hunks of its own alongside hunks that reference the parent, which is
what the parent chain has to resolve.

Usage: make_fixture.py out_basename [--variant N]
"""
import os
import struct
import sys

SECTOR      = 2352
DATA_FRAMES = 1500
AUDIO_FRAMES = 900
SYNC = b'\x00' + b'\xff' * 10 + b'\x00'


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def msf(lba):
    lba += 150
    return bytes((bcd(lba // 4500), bcd((lba // 75) % 60), bcd(lba % 75)))


class Lcg:
    def __init__(self, seed):
        self.s = seed & 0xffffffff

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0xffffffff
        return self.s >> 16


def data_sector(lba, rng, salt):
    out = bytearray(SECTOR)
    out[0:12] = SYNC
    out[12:15] = msf(lba)
    out[15] = 2
    sub = bytes((0, 0, 0x08, 0))
    out[16:20] = sub
    out[20:24] = sub
    level = (lba * 7 + salt) & 0xff
    for i in range(24, SECTOR):
        if (i & 15) == 0:
            level = (level + (rng.next() & 3)) & 0xff
        out[i] = level ^ (rng.next() & 1)
    return bytes(out)


def audio_frames(rng):
    out = bytearray()
    phase = 0
    for _ in range(AUDIO_FRAMES):
        for _ in range(588):
            phase = (phase + 37) & 0xffff
            tri = phase if phase < 0x8000 else 0xffff - phase
            l = tri - 0x4000 + (rng.next() & 7)
            r = (0x4000 - tri) + (rng.next() & 7)
            out += struct.pack('<hh', l, r)
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    base = sys.argv[1]
    variant = 0
    if len(sys.argv) >= 4 and sys.argv[2] == '--variant':
        variant = int(sys.argv[3])

    rng = Lcg(0x5eed)
    data = bytearray()
    for lba in range(DATA_FRAMES):
        data += data_sector(lba, rng, 0)

    if variant:
        vr = Lcg(0x1000 + variant)
        for k in range(8):
            lba = (k * 181 + variant * 13) % DATA_FRAMES
            data[lba * SECTOR:(lba + 1) * SECTOR] = data_sector(lba, vr,
                                                                variant)

    pcm = audio_frames(Lcg(0xa0d10))

    name = os.path.basename(base)
    with open(base + '.bin', 'wb') as f:
        f.write(data)
        f.write(pcm)
    with open(base + '.cue', 'w') as f:
        f.write('FILE "%s.bin" BINARY\n' % name)
        f.write('  TRACK 01 MODE2/2352\n')
        f.write('    INDEX 01 00:00:00\n')
        f.write('  TRACK 02 AUDIO\n')
        f.write('    INDEX 01 %02d:%02d:%02d\n'
                % (DATA_FRAMES // 4500, (DATA_FRAMES // 75) % 60,
                   DATA_FRAMES % 75))
    return 0


if __name__ == '__main__':
    sys.exit(main())
