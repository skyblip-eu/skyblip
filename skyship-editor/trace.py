#!/usr/bin/env python3
"""Trace a plan-view outline into a 1-bit skyShip sprite.

skyShip is skyBlip's ownship symbol - the aeroplane at the centre of the radar
screen. This turns a plan view YOU supply into a starting sprite, which is then
hinted by hand in index.html. Nothing is bundled: whatever drawing you feed it,
check the drawing's licence before shipping the result.

Pipeline:
  1. seal the line art and flood-fill from outside, so an outline drawing becomes
     a solid silhouette - interior detail falls inside the fill and disappears,
  2. keep the largest blob, so captions and leader lines drop out,
  3. de-rotate by finding the angle that maximises mirror symmetry: the plan view
     may sit at any angle on the page,
  4. area-average down to SPAN px wide (Image.BOX = true coverage, not point
     sampling) and threshold; the threshold is swept and scored by IoU against
     the full-resolution silhouette, so the rounding is chosen by measurement
     rather than by eye,
  5. lay the fuselage spine at its MEASURED width, rounded up to an even number
     of pixels: a rear fuselage is often thinner than one pixel at this scale,
     and 1 px both breaks the tail off the aeroplane and understates the airframe,
  6. close 1 px gaps in a row and drop pixels with no 4-neighbour - fine details
     rasterise to loose dots, and a loose dot on a collision display reads as
     traffic.

SPAN must be EVEN (default 30). The 200x200 screen has no middle pixel: its
centre is the point where pixels 99 and 100 meet, so an even sprite straddles
that point exactly while an odd one sits half a pixel off it.

Optional stylisation, for legibility rather than fidelity:
  --sweep N   rake the leading edge back N px at the tip, so a straight-LE
              constant-chord wing reads as a wing and not as a rounded rectangle
  --prop N    draw an N px propeller one row aft of the spinner cone

Usage:  python3 trace.py [span] --plan FILE [--sweep N] [--prop N]
        (needs pillow, numpy)
"""
import argparse
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageDraw, ImageFilter



def parse_args():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("span", nargs="?", type=int, default=30,
                   help="sprite span in px; EVEN so it straddles the screen centre "
                        "point (200x200 has no middle pixel)")
    p.add_argument("--plan", required=True,
                   help="image cropped to the PLAN view alone (a whole 3-view page "
                        "would de-rotate onto the front view, which is more symmetric)")
    p.add_argument("--sweep", type=float, default=0.0,
                   help="extra leading-edge sweep at the tip, px (stylisation: "
                        "trades fidelity for a more tapered wing)")
    p.add_argument("--sweep-exp", type=float, default=1.3,
                   help="sweep distribution; 1 = straight swept LE, >1 = outboard")
    p.add_argument("--prop", type=int, default=0,
                   help="draw a propeller this many px wide, one row aft of the cone")
    return p.parse_args()


def largest_blob(mask):
    lab = np.zeros(mask.shape, int)
    best, bid, cur = 0, None, 0
    for y, x in zip(*np.where(mask)):
        if lab[y, x]:
            continue
        cur += 1
        q, n = deque([(y, x)]), 0
        lab[y, x] = cur
        while q:
            cy, cx = q.popleft()
            n += 1
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = cy + dy, cx + dx
                if (0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1]
                        and mask[ny, nx] and not lab[ny, nx]):
                    lab[ny, nx] = cur
                    q.append((ny, nx))
        if n > best:
            best, bid = n, cur
    return lab == bid


def silhouette_from_plan(path):
    """Any scanned plan view: seal the line work, fill from outside, keep the
    aircraft (largest blob, so captions drop out), then de-rotate by finding the
    angle that maximises mirror symmetry."""
    im = Image.open(path).convert("L")
    ink = im.point(lambda v: 0 if v < 140 else 255, "L").filter(ImageFilter.MinFilter(3))
    for seed in ((0, 0), (ink.width - 1, 0), (0, ink.height - 1), (ink.width - 1, ink.height - 1)):
        if ink.getpixel(seed) == 255:
            ImageDraw.floodfill(ink, seed, 128, thresh=10)
    blob = largest_blob(np.array(ink.point(lambda v: 0 if v == 128 else 255, "L")) > 127)
    base = Image.fromarray((blob * 255).astype("uint8"))

    def level(deg):
        a = np.array(base.rotate(deg, expand=True, resample=Image.BICUBIC)
                     .point(lambda v: 255 if v > 127 else 0, "L")) > 127
        a = a[np.ix_(a.any(1), a.any(0))]
        if a.shape[1] % 2 == 0:
            a = a[:, :-1]
        return a, (a & a[:, ::-1]).sum() / (a | a[:, ::-1]).sum()

    ys, xs = np.where(blob)
    cov = np.cov(np.vstack([xs - xs.mean(), ys - ys.mean()]))
    w, v = np.linalg.eigh(cov)
    guess = np.degrees(np.arctan2(*v[::-1, np.argmax(w)]))   # span axis (the long one)
    deg = max((level(guess + d / 4)[1], guess + d / 4) for d in range(-40, 41))[1]
    a, score = level(deg)
    print(f"// de-rotated {deg:.2f} deg, mirror IoU {score:.3f}", file=sys.stderr)
    width = np.array([0 if r.sum() == 0 else np.ptp(np.where(r)[0]) + 1 for r in a])
    if np.argmax(width) > len(width) / 2:     # wing sits aft of mid -> nose down
        a = a[::-1]
    return a | a[:, ::-1]




def fuselage_width(sil, span, rows):
    """Measured width of the run through the centreline, per output row, in
    output px. This is the fuselage: the wing rows measure the whole wing."""
    sh, sw = sil.shape
    cx = sw // 2
    out = []
    for j in range(rows):
        ws = []
        for y in range(int(j * sh / rows), int((j + 1) * sh / rows)):
            if not sil[y, cx]:
                ws.append(0)
                continue
            l = r = cx
            while l > 0 and sil[y, l - 1]:
                l -= 1
            while r < sw - 1 and sil[y, r + 1]:
                r += 1
            ws.append(r - l + 1)
        out.append(np.mean(ws) * span / sw if ws else 0.0)
    return out


def rasterise(sil, span, thr):
    src = Image.fromarray((sil * 255).astype("uint8"))
    rows = round(span * sil.shape[0] / sil.shape[1])
    cov = np.array(src.resize((span, rows), Image.BOX), float) / 255.0
    g = (cov >= thr).astype(int)
    g |= g[:, ::-1]
    ys = np.where(g.any(1))[0]
    fus = fuselage_width(sil, span, rows)                 # spine at measured width,
    for j in range(ys.min(), ys.max() + 1):
        # >1.2 px of airframe -> the smallest symmetric spine wider than 1 px.
        # Even span: a PAIR straddling the axis (2 px). Odd span: 3 px.
        if span % 2 == 0:
            hw = 1 if fus[j] >= 1.2 else 1        # 2 px minimum either way
            g[j, span // 2 - hw:span // 2 + hw] = 1
        else:
            hw = 1 if fus[j] >= 1.2 else 0
            g[j, span // 2 - hw:span // 2 + hw + 1] = 1
    hole = np.zeros_like(g)                               # close 1 px gaps in a row:
    hole[:, 1:-1] = g[:, :-2] & g[:, 2:] & (1 - g[:, 1:-1])   # a dotted edge is
    g = g | hole                                              # a raster artifact
    n = np.zeros_like(g)                                  # 4-neighbour count
    n[1:] += g[:-1]; n[:-1] += g[1:]; n[:, 1:] += g[:, :-1]; n[:, :-1] += g[:, 1:]
    g = g * (n > 0)
    ys = np.where(g.any(1))[0]
    return g[ys.min():ys.max() + 1]                        # trim blank rows


def stylise(g, sweep, exp, prop):
    """Optional departures from the drawing, for legibility at 1 bit.

    sweep: rake the leading edge back by up to `sweep` px at the tip, keeping
    the trailing edge, so a straight-LE constant-chord wing reads as a tapered
    one. Costs fidelity by construction.
    prop: a 1 px bar of blades one row aft of the spinner cone."""
    span = g.shape[1]
    even = span % 2 == 0
    cx = span // 2
    if sweep:
        w = [np.ptp(np.where(r)[0]) + 1 if r.sum() else 0 for r in g]
        r0 = min(y for y, v in enumerate(w) if v > 0.6 * span)   # wing band
        r1 = max(y for y, v in enumerate(w) if v > 0.6 * span)
        for y in range(r0):                       # bare nose: cone + cowl only,
            g[y, :] = 0                           # the root fillet reads as clutter
            g[y, cx - 1:cx + (1 if even else 2)] = 1
        g[0, :] = 0
        g[0, cx - 1 if even else cx] = 1          # spinner cone: a pair when even
        if even:
            g[0, cx] = 1
        # Columns are stepped outward from the axis. With an even span the axis
        # is between cx-1 and cx, so the mirrored pair is (cx-d, cx-1+d).
        for d in range(1, cx + 1):
            s = int(round(sweep * (d / cx) ** exp))
            for x in (cx - d, (cx - 1 + d) if even else (cx + d)):
                if not 0 <= x < span:
                    continue
                run = [y for y in np.where(g[:, x])[0] if r0 - 2 <= y <= r1 + 4]
                for y in run[:s]:
                    g[y, x] = 0
    if prop:                                      # blades: even count when even span
        g[1, cx - prop // 2:cx + (prop // 2 if even else prop // 2 + 1)] = 1
    ys = np.where(g.any(1))[0]
    return g[ys.min():ys.max() + 1]


def iou(sil, g):
    up = np.array(Image.fromarray((g * 255).astype("uint8"))
                  .resize((sil.shape[1], sil.shape[0]), Image.NEAREST)) > 127
    return (up & sil).sum() / (up | sil).sum()


def main():
    args = parse_args()
    span = args.span
    sil = silhouette_from_plan(args.plan)

    def build(thr):
        return stylise(rasterise(sil, span, thr), args.sweep, args.sweep_exp, args.prop)

    score, thr, g = max((iou(sil, build(t / 100)), t / 100, build(t / 100))
                        for t in range(30, 66, 5))
    style = (f", sweep {args.sweep} px^{args.sweep_exp}" if args.sweep else "") + \
            (f", prop {args.prop} px" if args.prop else "")
    print(f"// {span} px span, {g.shape[0]} rows, threshold {thr}{style}, IoU {score:.3f}")
    for r in g:
        print("//   " + "".join("#" if v else "." for v in r))
    print(f"constexpr int kGlyphW = {g.shape[1]};")
    print(f"constexpr uint32_t kOwnship[] = {{")
    for r in g:
        bits = sum(int(v) << i for i, v in enumerate(r))
        print(f"    0x{bits:08x},  // {''.join('#' if v else '.' for v in r)}")
    print("};")


if __name__ == "__main__":
    main()
