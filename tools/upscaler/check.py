#!/usr/bin/env python3
"""Checks what the Vulkan renderer hands a temporal upscaler, from a run
of tools/vkhost with VKHOST_UPSCALER (frame_NNNN.ppm.raw, .mv.raw,
.depth.raw, .mask.raw in the output directory and the VKHOST_UPSCALER_LOG).

  check.py OUTDIR LOG FIRST LAST [--scene] [--still]

Every frame must come with inputs of the colour image's size (motion vectors
RG16F, depth R32F, the transparency mask R8), its jitter must follow the
Halton (2, 3) sequence, and the motion vectors must take each pixel that
changed from the frame before to where that frame had its color (to the
pixel), where standing still would not. --scene adds what
gen_motion_exe.py's scene says: the mask marks its translucent quad and
nothing else, and what is drawn is nearer than the background. --still
expects no motion at all (a frame shown twice).
"""
import argparse, struct, sys

# VkFormat
R8_UNORM, R16G16_SFLOAT, R32_SFLOAT = 9, 83, 100


def halton(i, base):
    r, f, i = 0.0, 1.0, i + 1
    while i:
        f /= base
        r += f * (i % base)
        i //= base
    return r


def read_log(path):
    frames = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 3:
                continue
            w, h = map(int, p[1].split("x"))
            if p[2] == "none":
                frames[int(p[0])] = None
                continue
            frames[int(p[0])] = {"size": (w, h), "jitter": (float(p[3]), float(p[4])), "flags": int(p[6]),
                                 "mv": tuple(map(int, p[8].split("x"))), "mv_fmt": int(p[10]),
                                 "depth": tuple(map(int, p[12].split("x"))), "depth_fmt": int(p[14]),
                                 "mask": tuple(map(int, p[16].split("x"))), "mask_fmt": int(p[18]),
                                 "fmt": int(p[p.index("color") + 2]) if "color" in p else 0}
    return frames


def close(a, b):
    """the same colour, give or take the scanout's dither"""
    return abs(a[0] - b[0]) <= 2 and abs(a[1] - b[1]) <= 2 and abs(a[2] - b[2]) <= 2


def load(out, n, w, h, fmt):
    with open("%s/frame_%04d.ppm.raw" % (out, n), "rb") as f:
        color = f.read()
    with open("%s/frame_%04d.mv.raw" % (out, n), "rb") as f:
        mv = struct.unpack("<%de" % (w * h * 2), f.read())
    with open("%s/frame_%04d.depth.raw" % (out, n), "rb") as f:
        depth = struct.unpack("<%df" % (w * h), f.read())
    with open("%s/frame_%04d.mask.raw" % (out, n), "rb") as f:
        mask = f.read()
    # to 8 bits a channel: the scanout's lower bits carry its dither
    words = struct.unpack("<%dI" % (w * h), color[:w * h * 4])
    if fmt == 64:   # VK_FORMAT_A2B10G10R10_UNORM_PACK32
        px = [((v >> 2) & 0xff, (v >> 12) & 0xff, (v >> 22) & 0xff) for v in words]
    else:           # 8 bits a channel
        px = [(v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff) for v in words]
    return px, mv, depth, mask


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("log")
    ap.add_argument("first", type=int)
    ap.add_argument("last", type=int)
    ap.add_argument("--scene", action="store_true")
    ap.add_argument("--still", action="store_true")
    ap.add_argument("--gte-depth", type=float,
                    help="the depth the scene's GTE triangle (magenta) must carry (PGXP on: 512 / (512 + 512))")
    a = ap.parse_args()

    frames = read_log(a.log)
    failures = []
    if not frames:
        failures.append("no frames logged")
    for n in range(a.first, a.last + 1):
        fr = frames.get(n)
        if fr is None:
            failures.append("frame %d: no inputs" % n)
            continue
        if fr["mv"] != fr["size"] or fr["depth"] != fr["size"] or fr["mask"] != fr["size"]:
            failures.append("frame %d: inputs %s/%s/%s for a %s frame" % (n, fr["mv"], fr["depth"], fr["mask"], fr["size"]))
        if fr["mv_fmt"] != R16G16_SFLOAT or fr["depth_fmt"] != R32_SFLOAT or fr["mask_fmt"] != R8_UNORM:
            failures.append("frame %d: formats %d/%d/%d" % (n, fr["mv_fmt"], fr["depth_fmt"], fr["mask_fmt"]))
    if failures:
        print("\n".join(failures))
        return 1

    # the jitter: consecutive Halton points, from wherever it started
    seq = [(halton(i, 2) - 0.5, halton(i, 3) - 0.5) for i in range(64)]
    jx, jy = frames[a.first]["jitter"]
    start = [i for i, (x, y) in enumerate(seq) if abs(x - jx) < 1e-5 and abs(y - jy) < 1e-5]
    if not start:
        failures.append("frame %d: jitter %s is not a Halton (2, 3) point" % (a.first, (jx, jy)))
    else:
        i = start[0]
        for n in range(a.first + 1, a.last + 1):
            got = frames[n]["jitter"]
            near = lambda w: abs(got[0] - w[0]) < 1e-5 and abs(got[1] - w[1]) < 1e-5
            if a.still:
                if not near(seq[i]):
                    failures.append("frame %d: shown again with jitter %s, not %s" % (n, got, seq[i]))
            elif near(seq[(i + 1) % 64]):
                i = (i + 1) % 64
            elif near(seq[0]) and i + 1 >= 8:
                i = 0
            else:
                failures.append("frame %d: jitter %s, want %s" % (n, got, seq[(i + 1) % 64]))
                break

    for n in range(a.first + 1, a.last + 1):
        cur, prev = frames[n], frames[n - 1]
        if cur["size"] != prev["size"]:
            failures.append("frame %d: size changed" % n)
            continue
        w, h = cur["size"]
        c, mv, depth, mask = load(a.out, n, w, h, cur["fmt"])
        cp, mvp, _, maskp = load(a.out, n - 1, w, h, prev["fmt"])
        jn, jp = cur["jitter"], prev["jitter"]
        # the pixels that changed: each must find its color where the
        # motion vectors say it was (to the pixel); a pixel carried off
        # the frame proves nothing either way
        hits = {"motion": 0, "still": 0}
        covered = 0
        moved = 0
        for i in range(w * h):
            if mv[2 * i] != 0.0 or mv[2 * i + 1] != 0.0:
                moved += 1
            if close(c[i], cp[i]):
                continue
            # what is blended over is the upscaler's to take as it is
            if mask[i]:
                continue
            # a still surface a moving one uncovered was nowhere the
            # frame before
            if mv[2 * i] == 0.0 and mv[2 * i + 1] == 0.0 and abs(mvp[2 * i]) + abs(mvp[2 * i + 1]) > 0.5:
                continue
            y, x = divmod(i, w)
            q = {}
            for name, use in (("motion", 1), ("still", 0)):
                q[name] = (int(round(x - jn[0] - use * mv[2 * i] + jp[0])),
                           int(round(y - jn[1] - use * mv[2 * i + 1] + jp[1])))
            if not all(0 <= qx < w and 0 <= qy < h for qx, qy in q.values()):
                continue
            # a pixel out from under something blended over it had
            # another colour there
            if maskp[q["motion"][1] * w + q["motion"][0]]:
                continue
            covered += 1
            for name, (qx, qy) in q.items():
                if any(close(cp[min(h - 1, max(0, qy + dy)) * w + min(w - 1, max(0, qx + dx))], c[i])
                       for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                    hits[name] += 1
        if a.still:
            if moved:
                failures.append("frame %d: %d pixels move in a frame shown again" % (n, moved))
            continue
        if covered < 100:
            failures.append("frame %d: only %d pixels changed, nothing moved" % (n, covered))
            continue
        moving, still = hits["motion"] / covered, hits["still"] / covered
        print("frame %d: %d pixels changed, %.3f found where the motion vectors say, %.3f standing still"
              % (n, covered, moving, still))
        # (slow motion stays near its pixels anyway: the motion vectors
        # must at least halve what standing still misses)
        if moving < 0.9 or 1.0 - moving > 0.5 * (1.0 - still):
            failures.append("frame %d: %.3f of the changed pixels found where the motion vectors say" % (n, moving))
        if a.scene:
            s = w // 320
            # the translucent quad: (220, 170) to (300, 230), standing
            # still (its edges land a pixel either way under the jitter)
            inside = sum(1 for y in range(172 * s, 228 * s) for x in range(222 * s, 298 * s) if mask[y * w + x])
            outside = sum(1 for y in range(0, h, 3) for x in range(0, w, 3)
                          if mask[y * w + x] and not (220 * s - 1 <= x <= 300 * s and 170 * s - 1 <= y <= 230 * s))
            if inside < (56 * s) * (76 * s) * 0.95 or outside:
                failures.append("frame %d: the mask marks %d of the translucent quad's pixels, %d others" % (n, inside, outside))
            # the background is as far as it gets, the triangle nearer
            bg = depth[(10 * s) * w + 10 * s]
            bg_color = c[(10 * s) * w + 10 * s]
            tri = min(depth[(40 * s) * w + x] for x in range(0, w, 2))
            if bg != 1.0 or tri >= 1.0:
                failures.append("frame %d: depth %s at the background, %s on the triangle" % (n, bg, tri))
            if a.gte_depth is not None:
                # PGXP's depth on the GTE triangle: what is drawn in its
                # box, (10, 100) to (73, 140), which nothing else enters
                gte = [depth[y * w + x] for y in range(100 * s, 141 * s) for x in range(10 * s, 120 * s)
                       if not close(c[y * w + x], bg_color)]
                off = [d for d in gte if abs(d - a.gte_depth) > 0.01]
                if len(gte) < 600 * s * s or off:
                    failures.append("frame %d: %d of the GTE triangle's %d pixels off depth %.3f (%s)"
                                    % (n, len(off), len(gte), a.gte_depth, off[:3]))

    if failures:
        print("\n".join(failures))
        return 1
    print("upscaler inputs: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
