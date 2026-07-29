#!/usr/bin/env python3
"""Rasterise the skyBlip wordmark into ui/widgets/wordmark.cpp.

The panel is 1-bit and 200x200, so the wordmark cannot be a font at runtime:
this bakes the site's display face (Fira Sans Bold, the same woff2 the website
serves) plus the mark - dotless i, blip dot, two ping arcs - into one bitmap.

  python3 scripts/make_wordmark.py
  python3 scripts/make_wordmark.py --size 44 --preview /tmp/wm.png

1 bit has no grey to soften an edge, so fidelity is won before the threshold.
The glyphs are rasterised 16x oversampled and box-filtered to pixel COVERAGE,
then two things are searched rather than assumed: the sub-pixel origin, where
the least quantisation error wins (that is what snaps stems onto whole columns
instead of smearing each over two), and the cut, where the threshold that keeps
the same total ink as the outline wins (a 50% cut thins a bold face's curves,
and the wordmark reads lighter than the site's).

Geometry is the brand sheet's, in units of the font size (56 px there):
dot r 5.5 at 42.5 above the baseline, arcs r 14 and 22, stroke 3.5, +/-46 deg
about vertical, all concentric with the dot.
"""
import argparse
import os
import shutil
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "firmware", "ui", "widgets", "wordmark.cpp")
FONT = os.path.join(ROOT, "..", "website", "app", "assets", "fonts", "FiraSans-Bold.woff2")

TEXT = "skyBl\u0131p"  # dotless i: the tittle is the blip
REFERENCE_SIZE = 56.0
DOT_R = 5.5
DOT_ABOVE_BASELINE = 42.5
ARC_R = (14.0, 22.0)
ARC_W = 3.5
ARC_HALF_ANGLE = 46.0
SUPERSAMPLE = 16
SUBPIXEL_STEPS = 8  # offsets tried per axis, in 1/8 px


def coverage(font, size, w, h, baseline, dx, dy):
    """Ink coverage per final pixel, in [0, 1], for one sub-pixel origin."""
    s = SUPERSAMPLE
    unit = size / REFERENCE_SIZE
    img = Image.new("L", (w * s, h * s), 0)
    draw = ImageDraw.Draw(img)
    ox, oy = dx * s, baseline * s + dy * s
    draw.text((ox, oy), TEXT, font=font, fill=255, anchor="ls")

    # The mark sits over the dotless i: the glyph's own advance names the centre.
    dot_cx = ox + font.getlength("skyBl") + font.getlength("\u0131") / 2
    dot_cy = oy - DOT_ABOVE_BASELINE * unit * s
    r = DOT_R * unit * s
    draw.ellipse([dot_cx - r, dot_cy - r, dot_cx + r, dot_cy + r], fill=255)
    # Round caps, as the brand sheet draws them: PIL's arc() cuts its ends
    # square, which quantises to a pixel or two of grit at each tip.
    stroke = max(1, int(round(ARC_W * unit * s)))
    for arc_r in ARC_R:
        rr = arc_r * unit * s
        points = []
        for k in range(65):
            a = np.radians(270 - ARC_HALF_ANGLE + 2 * ARC_HALF_ANGLE * k / 64)
            points.append((dot_cx + rr * np.cos(a), dot_cy + rr * np.sin(a)))
        draw.line(points, fill=255, width=stroke, joint="curve")
        for cap in (points[0], points[-1]):
            draw.ellipse([cap[0] - stroke / 2, cap[1] - stroke / 2,
                          cap[0] + stroke / 2, cap[1] + stroke / 2], fill=255)

    a = np.asarray(img, dtype=np.float64) / 255.0
    return a.reshape(h, s, w, s).mean(axis=(1, 3))


def render(font_path, size, verbose=False):
    s = SUPERSAMPLE
    unit = size / REFERENCE_SIZE
    font = ImageFont.truetype(font_path, int(round(size * s)))
    ascent, descent = font.getmetrics()
    ascent, descent = ascent / s, descent / s

    arc_top = DOT_ABOVE_BASELINE * unit + max(ARC_R) * unit + ARC_W * unit / 2
    baseline = max(ascent, arc_top) + 1
    w = int(np.ceil(font.getlength(TEXT) / s)) + 2
    h = int(np.ceil(baseline + descent)) + 1

    # The offset that quantises with the least error is the one whose stems and
    # bowls already sit on pixel boundaries.
    best = None
    for i in range(SUBPIXEL_STEPS):
        for j in range(SUBPIXEL_STEPS):
            dx, dy = i / SUBPIXEL_STEPS, j / SUBPIXEL_STEPS
            cov = coverage(font, size, w, h, baseline, dx, dy)
            error = np.abs((cov >= 0.5).astype(np.float64) - cov).sum()
            if best is None or error < best[0]:
                best = (error, dx, dy, cov)
    error, dx, dy, cov = best

    # Weight before edges: of the cuts that keep the outline's ink area, take
    # the one that also quantises best.
    area = cov.sum()
    cut = min((abs((cov >= t).sum() - area), np.abs((cov >= t) - cov).sum(), t)
              for t in np.arange(0.34, 0.67, 0.01))[2]
    bits = cov >= cut
    if verbose:
        print("origin dx=%.3f dy=%.3f, cut %.2f, ink %d px vs outline %.1f px, error %.1f px"
              % (dx, dy, cut, bits.sum(), area, np.abs(bits - cov).sum()))
    # Trim to ink, and keep the x-height band's midline: that is what the eye
    # reads as the middle of the word, not the block that the arcs stretch.
    cols = np.nonzero(bits.any(axis=0))[0]
    rows = np.nonzero(bits.any(axis=1))[0]
    x0, x1, y0, y1 = cols.min(), cols.max() + 1, rows.min(), rows.max() + 1
    x_height = -font.getbbox("s", anchor="ls")[1] / s
    anchor = int(round(baseline + dy - x_height / 2)) - y0
    return bits[y0:y1, x0:x1], anchor


def pack(bits):
    h, w = bits.shape
    stride = (w + 7) // 8
    rows = []
    for y in range(h):
        row = bytearray(stride)
        for x in range(w):
            if bits[y, x]:
                row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))
    return w, h, stride, rows


def emit(w, h, stride, rows, anchor, font_path, size):
    lines = [
        "#include \"ui/widgets/wordmark.h\"",
        "",
        "namespace skyblip::ui {",
        "",
        "namespace {",
        "// GENERATED by scripts/make_wordmark.py from %s at %g px."
        % (os.path.basename(font_path), size),
        "// Edit the script, not the table.",
        "constexpr int kW = %d;" % w,
        "constexpr int kH = %d;" % h,
        "constexpr int kStride = %d;" % stride,
        "// The row the eye reads as the middle of the word: the x-height band's",
        "// midline, not the centre of a block the ping arcs stretch upwards.",
        "constexpr int kMidlineRow = %d;" % anchor,
        "constexpr unsigned char kBits[kStride * kH] = {",
    ]
    for row in rows:
        lines.append("    " + "".join("0x%02x, " % b for b in row).rstrip())
    lines += [
        "};",
        "}  // namespace",
        "",
        "void draw_wordmark(Framebuffer& fb, int cx, int cy) {",
        "    const int x0 = cx - kW / 2, y0 = cy - kMidlineRow;",
        "    for (int y = 0; y < kH; y++)",
        "        for (int x = 0; x < kW; x++)",
        "            if (kBits[y * kStride + (x >> 3)] & (0x80 >> (x & 7)))",
        "                fb.set_pixel(x0 + x, y0 + y, true);",
        "}",
        "",
        "int wordmark_width() { return kW; }",
        "int wordmark_height() { return kH; }",
        "",
        "}  // namespace skyblip::ui",
        "",
    ]
    return "\n".join(lines)


def stem_report(bits):
    """Widths of the vertical stems, row by row: they should all match."""
    h, w = bits.shape
    y = h // 2
    runs, start = [], None
    for x in range(w):
        if bits[y, x] and start is None:
            start = x
        elif not bits[y, x] and start is not None:
            runs.append(x - start)
            start = None
    return runs


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--font", default=FONT)
    p.add_argument("--size", type=float, default=50.0)
    p.add_argument("--out", default=OUT)
    p.add_argument("--preview")
    p.add_argument("--zoom", type=int, default=6)
    args = p.parse_args()

    bits, anchor = render(args.font, args.size, verbose=True)
    h, w = bits.shape
    if w > 200 or h > 200:
        raise SystemExit("wordmark is %dx%d, the panel is 200x200" % (w, h))
    w, h, stride, rows = pack(bits)
    with open(args.out, "w") as fh:
        fh.write(emit(w, h, stride, rows, anchor, args.font, args.size))
    # CI formats what is committed, so the generator hands over formatted code.
    formatter = shutil.which("clang-format") or "/opt/homebrew/opt/llvm@21/bin/clang-format"
    if os.path.exists(formatter):
        subprocess.run([formatter, "-i", args.out], check=False)
    if args.preview:
        img = Image.fromarray(np.where(bits, 0, 255).astype("uint8"), "L")
        img.resize((w * args.zoom, h * args.zoom), Image.NEAREST).save(args.preview)
    print("%s: %dx%d, midline row %d, %d bytes, mid-row runs %s"
          % (args.out, w, h, anchor, stride * h, stem_report(bits)))


if __name__ == "__main__":
    main()
