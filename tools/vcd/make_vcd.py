#!/usr/bin/env python3
"""Build a synthetic but spec-conformant Video CD image (CUE + BIN).

Why this exists: every other Video CD test in this tree works on elementary
streams or on sectors handed straight to VCD_FeedSector. None of them exercise
the real disc path -- CDIF opening a CUE, the TOC, Mode 2 sector framing, and
VCD_ProbeDisc reading the control sectors off an actual image. That is exactly
where sector-size and track-layout assumptions hide, and no pressed VCD is to
hand.

Layout, all of it fixed by the VCD specification rather than chosen here:

  Track 1  MODE2/2352, data
    LBA  16   ISO 9660 Primary Volume Descriptor
              System Identifier "CD-RTOS CD-BRIDGE", which is what marks the
              disc as a CD-i Bridge disc and what the probe keys off
    LBA  17   Volume Descriptor Set Terminator
    LBA  18   root directory (., .., VCD, MPEGAV)
    LBA  19   VCD directory  (INFO.VCD, ENTRIES.VCD)
    LBA  20   MPEGAV directory (AVSEQ01.DAT)
    LBA 150   VCD/INFO.VCD     -- 00:04:00, mandated
    LBA 151   VCD/ENTRIES.VCD  -- 00:04:01, mandated

  Track 2  MODE2/2352, the MPEG-1 program stream in Form 2 sectors

Everything multi-byte inside the VCD control files is BIG-endian and every
MSF is BCD -- the opposite of the little-endian ISO structures surrounding
them, which is a good way to get a probe subtly wrong.

Usage: make_vcd.py stream.mpg out_basename [--pal]
"""
import struct
import sys
import os

SECTOR      = 2352
FORM1_DATA  = 2048
FORM2_DATA  = 2324

SYNC = b'\x00' + b'\xff' * 10 + b'\x00'

LBA_PVD      = 16
LBA_TERM     = 17
LBA_ROOT     = 18
LBA_VCDDIR   = 19
LBA_MPEGAV   = 20
LBA_INFO     = 150
LBA_ENTRIES  = 151

TRACK1_LEN   = 300          # LBA 0..299, comfortably past ENTRIES
PREGAP       = 150          # the 2-second gap before track 2


# CD EDC polynomial, x^32 + x^31 + x^16 + x^15 + x^4 + x^3 + x + 1 (0x8001801B),
# in reflected form. This is not the ordinary CRC-32 polynomial and using that
# one instead produces a sector every reader rejects.
EDC_POLY = 0xD8018001

_edc_table = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ EDC_POLY if _c & 1 else _c >> 1
    _edc_table.append(_c)


def edc_crc(data):
    crc = 0
    for b in data:
        crc = _edc_table[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc & 0xFFFFFFFF


def bcd(v):
    return ((v // 10) % 10) << 4 | (v % 10)


def lba_to_msf(lba):
    """Absolute disc MSF: LBA 0 is 00:02:00, the 150-frame lead-in offset.
    This is what goes in a sector header and in ENTRIES.VCD."""
    t = lba + 150
    return t // (60 * 75), (t // 75) % 60, t % 75


def lba_to_cue_msf(lba):
    """CUE INDEX MSF, which is file-relative: 00:00:00 is the first sector of
    the FILE, with no lead-in offset. Using the absolute form here shifts
    every track by two seconds -- the TOC then reports track 2 at LBA 600
    for a track that actually starts at 450, and reads land in the wrong
    place. Easy to get wrong because the two look identical."""
    return lba // (60 * 75), (lba // 75) % 60, lba % 75


def header(lba, mode=2):
    m, s, f = lba_to_msf(lba)
    return bytes([bcd(m), bcd(s), bcd(f), mode])


def form1(lba, payload, submode=0x08):
    """Mode 2 Form 1: 12 sync, 4 header, 8 subheader, 2048 data, 4 EDC,
    276 ECC = 2352.

    Note there is no 8-byte reserved gap here. Mode 1 has one -- 12 + 4 +
    2048 + 4 + 8 + 276 -- and Mode 2 Form 1 spends those eight bytes on the
    subheader instead. Including both makes a 2360-byte sector, which is not
    an error anything reports: the file comes out a plausible size, sector 0
    reads correctly, and every sector after it is progressively misaligned.
    EDC and ECC are left zero; CDIF does not verify them."""
    assert len(payload) <= FORM1_DATA
    sub = bytes([0, 0, submode, 0]) * 2
    body = sub + payload.ljust(FORM1_DATA, b'\x00')
    # EDC covers the subheader and user data, 2056 bytes from offset 16, and
    # is stored little-endian at 2072.
    #
    # It has to be right: CDIF_ReadSector runs the sector through
    # edc_lec_check_and_correct and returns nothing if it fails, so a probe
    # reading INFO.VCD off an image without a valid EDC gets zero bytes and
    # concludes the disc is not a Video CD. Raw reads still work, which makes
    # this look like a probe bug rather than an image one.
    #
    # The 276 ECC bytes are left zero. The reader skips L-EC entirely when the
    # EDC checks out (recover-raw.c: ValidateRawSector), so they are never
    # consulted here -- but a pressed disc carries them, and a stricter reader
    # would want them.
    edc = struct.pack('<I', edc_crc(body))
    sec = SYNC + header(lba) + body + edc + b'\x00' * 276
    assert len(sec) == SECTOR, len(sec)
    return sec


def form2(lba, payload, submode=0x64):
    """Mode 2 Form 2: 12 sync, 4 header, 8 subheader, 2324 data, 4 EDC.
    Submode 0x64 = form 2 (0x20) | real-time (0x40) | video (0x02) ... the
    caller passes what it wants; MPEG sectors are form2+realtime+video."""
    assert len(payload) <= FORM2_DATA
    sub = bytes([0, 0, submode, 0]) * 2
    # Form 2's EDC is optional and conventionally zero; nothing checks it.
    sec = (SYNC + header(lba) + sub
           + payload.ljust(FORM2_DATA, b'\x00')
           + b'\x00' * 4)
    assert len(sec) == SECTOR, len(sec)
    return sec


def both_endian32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)


def both_endian16(v):
    return struct.pack('<H', v) + struct.pack('>H', v)


def dir_record(name, lba, length, is_dir, special=None):
    """ISO 9660 directory record. `special` is 0 for '.' and 1 for '..'."""
    ident = bytes([special]) if special is not None else name.encode('ascii')
    ln = 33 + len(ident)
    pad = ln % 2
    ln += pad
    r = bytearray()
    r.append(ln)
    r.append(0)                                   # extended attr length
    r += both_endian32(lba)
    r += both_endian32(length)
    r += bytes([95, 1, 1, 0, 0, 0, 0])            # recording date/time
    r.append(0x02 if is_dir else 0x00)            # flags
    r += bytes([0, 0])                            # unit size, gap size
    r += both_endian16(1)                         # volume sequence number
    r.append(len(ident))
    r += ident
    r += b'\x00' * pad
    assert len(r) == ln
    return bytes(r)


def make_pvd(volume_lbas):
    """Primary Volume Descriptor. The System Identifier is the part that
    matters: 'CD-RTOS CD-BRIDGE' is what tells a player this is a CD-i Bridge
    disc, and it is what VCD_ProbeDisc checks before touching anything else."""
    p = bytearray(b'\x00' * FORM1_DATA)
    p[0] = 1
    p[1:6] = b'CD001'
    p[6] = 1
    p[8:40]    = b'CD-RTOS CD-BRIDGE'.ljust(32, b' ')   # System Identifier
    p[40:72]   = b'VIDEOCD'.ljust(32, b' ')             # Volume Identifier
    p[80:88]   = both_endian32(volume_lbas)
    p[120:128] = both_endian16(1) + both_endian16(1)    # set size, seq number
    p[128:132] = both_endian16(FORM1_DATA)
    p[132:140] = both_endian32(10)                      # path table size
    p[140:144] = struct.pack('<I', 21)                  # L path table LBA
    p[148:152] = struct.pack('>I', 22)                  # M path table LBA
    p[156:156 + 34] = dir_record('', LBA_ROOT, FORM1_DATA, True, special=0)
    p[190:318] = b' ' * 128                             # volume set id
    p[318:446] = b' ' * 128                             # publisher
    p[574:702] = b'CDI/CDI_VCD.APP;1'.ljust(128, b' ')  # application id
    p[813:830] = b'\x30' * 16 + b'\x00'                 # creation date
    p[881] = 1                                          # file structure version
    # CD-XA identifying signature, which every VCD and PSX disc carries
    p[1024:1032] = b'CD-XA001'
    return bytes(p)


def make_terminator():
    p = bytearray(b'\x00' * FORM1_DATA)
    p[0] = 0xFF
    p[1:6] = b'CD001'
    p[6] = 1
    return bytes(p)


def make_root_dir():
    r = bytearray()
    r += dir_record('', LBA_ROOT, FORM1_DATA, True, special=0)
    r += dir_record('', LBA_ROOT, FORM1_DATA, True, special=1)
    r += dir_record('VCD', LBA_VCDDIR, FORM1_DATA, True)
    r += dir_record('MPEGAV', LBA_MPEGAV, FORM1_DATA, True)
    return bytes(r)


def make_vcd_dir():
    r = bytearray()
    r += dir_record('', LBA_VCDDIR, FORM1_DATA, True, special=0)
    r += dir_record('', LBA_ROOT, FORM1_DATA, True, special=1)
    r += dir_record('INFO.VCD;1', LBA_INFO, FORM1_DATA, False)
    r += dir_record('ENTRIES.VCD;1', LBA_ENTRIES, FORM1_DATA, False)
    return bytes(r)


def make_mpegav_dir(track2_lba, track2_sectors):
    r = bytearray()
    r += dir_record('', LBA_MPEGAV, FORM1_DATA, True, special=0)
    r += dir_record('', LBA_ROOT, FORM1_DATA, True, special=1)
    r += dir_record('AVSEQ01.DAT;1', track2_lba,
                    track2_sectors * FORM2_DATA, False)
    return bytes(r)


def make_info(pal):
    """VCD/INFO.VCD. Big-endian throughout."""
    p = bytearray(b'\x00' * FORM1_DATA)
    p[0:8]  = b'VIDEO_CD'
    p[8]    = 0x02                       # version major -> VCD 2.0
    p[9]    = 0x00                       # system profile tag
    p[10:26] = b'SYNTHETIC VCD   '       # album id, 16 bytes, space padded
    p[26:28] = struct.pack('>H', 1)      # number of CDs in album
    p[28:30] = struct.pack('>H', 1)      # this CD's number
    # 0x1E: 13 bytes of PAL flags, one bit per track, MSB first. Track 2 is
    # the first video track, so bit 6 of the first byte.
    p[0x1E] = 0x40 if pal else 0x00
    p[0x2B] = 0x00                       # info status flags
    p[0x2C:0x30] = struct.pack('>I', 0)  # PSD size: 0 = no playback control
    return bytes(p)


def make_entries(track2_lba):
    """VCD/ENTRIES.VCD. Count is big-endian; each entry is four BCD bytes,
    track number then MM:SS:FF."""
    p = bytearray(b'\x00' * FORM1_DATA)
    p[0:8]  = b'ENTRYVCD'
    p[8]    = 0x02
    p[9]    = 0x00
    p[0x0A:0x0C] = struct.pack('>H', 1)  # one entry
    m, s, f = lba_to_msf(track2_lba)
    p[0x0C] = bcd(2)                     # track 2
    p[0x0D] = bcd(m)
    p[0x0E] = bcd(s)
    p[0x0F] = bcd(f)
    return bytes(p)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    pal  = '--pal' in sys.argv
    if len(args) < 2:
        sys.exit(__doc__)
    mpg_path, base = args[0], args[1]

    mpg = open(mpg_path, 'rb').read()
    n_mpeg = (len(mpg) + FORM2_DATA - 1) // FORM2_DATA

    track2_lba = TRACK1_LEN + PREGAP
    total      = track2_lba + n_mpeg

    out = bytearray()

    # ---- track 1 ------------------------------------------------------
    for lba in range(TRACK1_LEN):
        if lba == LBA_PVD:
            data = make_pvd(total)
        elif lba == LBA_TERM:
            data = make_terminator()
        elif lba == LBA_ROOT:
            data = make_root_dir()
        elif lba == LBA_VCDDIR:
            data = make_vcd_dir()
        elif lba == LBA_MPEGAV:
            data = make_mpegav_dir(track2_lba, n_mpeg)
        elif lba == LBA_INFO:
            data = make_info(pal)
        elif lba == LBA_ENTRIES:
            data = make_entries(track2_lba)
        else:
            data = b''
        out += form1(lba, data)

    # ---- pregap, written into the file as track 2 index 0 -------------
    for lba in range(TRACK1_LEN, track2_lba):
        out += form2(lba, b'', submode=0x20)

    # ---- track 2: the program stream ----------------------------------
    for i in range(n_mpeg):
        chunk = mpg[i * FORM2_DATA:(i + 1) * FORM2_DATA]
        # form 2 | real-time | video, which is what a VCD marks its MPEG
        # sectors with and what VCD_FeedSector filters on
        out += form2(track2_lba + i, chunk, submode=0x20 | 0x40 | 0x02)

    bin_name = base + '.bin'
    with open(bin_name, 'wb') as f:
        f.write(out)

    m0, s0, f0 = lba_to_cue_msf(TRACK1_LEN)      # track 2 index 0 (pregap)
    m1, s1, f1 = lba_to_cue_msf(track2_lba)      # track 2 index 1
    with open(base + '.cue', 'w') as f:
        f.write('FILE "%s" BINARY\n' % os.path.basename(bin_name))
        f.write('  TRACK 01 MODE2/2352\n')
        f.write('    INDEX 01 00:00:00\n')
        f.write('  TRACK 02 MODE2/2352\n')
        f.write('    INDEX 00 %02d:%02d:%02d\n' % (m0, s0, f0))
        f.write('    INDEX 01 %02d:%02d:%02d\n' % (m1, s1, f1))

    print('%s: %d sectors (%d bytes)' % (bin_name, total, len(out)))
    print('  track 1  LBA 0..%d          (%s)'
          % (TRACK1_LEN - 1, 'NTSC' if not pal else 'PAL'))
    am, asec, af = lba_to_msf(track2_lba)
    print('  track 2  LBA %d, %d sectors, CUE %02d:%02d:%02d, absolute %02d:%02d:%02d'
          % (track2_lba, n_mpeg, m1, s1, f1, am, asec, af))


if __name__ == '__main__':
    main()
