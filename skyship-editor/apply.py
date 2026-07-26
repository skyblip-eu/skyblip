#!/usr/bin/env python3
"""Write a skyShip sprite into ui/screens/radar.cpp, in place.

Closes the loop from the editor (index.html) or the tracer (trace.py) back into
the firmware without hand-editing hex. Writes the COMPILED DEFAULT symbol;
user-loaded sprites will live in QSPI flash, not here.

  python3 skyship-editor/apply.py skyship.txt        # rows of '#' and '.'
  open skyship-editor/index.html -> copy art -> pbpaste | python3 skyship-editor/apply.py
  python3 skyship-editor/trace.py 30 --plan p.png | python3 skyship-editor/apply.py

Accepts either the ASCII art or a C++ kOwnship table (so it round-trips its own
output). Refuses to write a glyph that would look broken on a 1-bit collision
display: even width, unmirrored pixels, loose dots (they read as traffic),
blank rows inside the glyph, or a broken centreline. --force overrides.
"""
import argparse
import os
import re
import sys

RADAR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     "firmware", "ui", "screens", "radar.cpp")


def parse(text):
    # NB the lookbehind: without it "200x200," in a comment parses as 0x200.
    # The hex table wins when present: gen_ownship_glyph.py prints the art as a
    # comment block AND the table, so reading the art would count every row
    # twice. Plain art (the editor's "copy art only") has no hex to find.
    hexes = [int(h, 16) for h in re.findall(r"(?<![0-9a-zA-Z])0x([0-9a-fA-F]{1,8})\s*,", text)]
    if len(hexes) > 2:
        m = re.search(r"kGlyphW\s*=\s*(\d+)", text)
        w = int(m.group(1)) if m else 30
        return [[(v >> i) & 1 for i in range(w)] for v in hexes]
    art = [m.group(0) for m in (re.search(r"[#.]{5,}", l) or _N for l in text.splitlines())
           if m is not _N]
    if len(art) > 2:
        w = max(len(r) for r in art)
        return [[1 if c == "#" else 0 for c in r.ljust(w, ".")] for r in art]
    sys.exit("no glyph found: expected rows of '#'/'.' or a kOwnshipRows[] table")


class _N:                      # sentinel for "no match" in the comprehension above
    pass


def to_even(g):
    """Odd width -> even, so the glyph straddles the screen's centre POINT.

    200x200 has no middle pixel: the centre is the corner where pixels 99 and
    100 meet. A glyph symmetric about a pixel column is half a pixel off it; a
    glyph symmetric about the boundary between two columns is exactly on it.

    Each row keeps its outer extent, so a run of width w across the axis becomes
    w-1 (a pair of middle pixels instead of one), and a 1 px feature - spinner,
    rudder, fuselage spine - becomes 2 px. Off-axis features (wing marks) move
    to the mirrored pair at the same distance.
    """
    old = len(g[0])
    if old % 2 == 0:
        return g
    ax, w = old // 2, old - 1          # new axis: between columns w/2-1 and w/2
    out = []
    for row in g:
        runs, start = [], None
        for i, v in enumerate(list(row) + [0]):
            if v and start is None:
                start = i
            elif not v and start is not None:
                runs.append((start, i - 1))
                start = None
        new = [0] * w
        for a, b in runs:
            if a <= ax <= b:                       # crosses the axis: keep the edges
                r = max(1, (b - a) // 2)           # 1 px becomes a 2 px pair
                for x in range(ax - r, ax + r):
                    new[x] = 1
            else:                                  # off-axis: mirrored pair
                d = abs((a + b) // 2 - ax)
                for x in (ax - d, ax - 1 + d):
                    if 0 <= x < w:
                        new[x] = 1
        out.append(new)
    return out


def trim(g, origin):
    """Drop blank edge rows, and blank edge COLUMNS in pairs.

    Columns must go two at a time or the mirror axis moves and the width changes
    parity. Blank edges are not harmless padding: they make kGlyphH lie, and if
    anything ever centres the sprite by its bounding box again they shift it.
    """
    top = 0
    while len(g) > 1 and not any(g[0]):
        g = g[1:]
        top += 1
    while len(g) > 1 and not any(g[-1]):
        g = g[:-1]
    while len(g[0]) > 4 and not any(r[0] for r in g) and not any(r[-1] for r in g):
        g = [r[1:-1] for r in g]
    return g, origin - top


def check(g):
    h, w = len(g), len(g[0])
    bad = []
    # The 200x200 screen has no middle pixel: the centre is the point where
    # pixels 99 and 100 meet. An EVEN-width glyph straddles it and is exactly
    # centred; an ODD one puts its axis half a pixel off. Even is preferred, so
    # only warn about odd - and for even, require a 2 px (not 1 px) spine.
    if w % 2:
        bad.append(f"odd width {w}: the glyph axis lands half a pixel off the "
                   f"screen centre; even width straddles it exactly")
    asym = sum(g[r][c] != g[r][w - 1 - c] for r in range(h) for c in range(w))
    if asym:
        bad.append(f"{asym} px not mirrored about the centreline")
    loose = 0
    for r in range(h):
        for c in range(w):
            if not g[r][c]:
                continue
            n = ((g[r - 1][c] if r else 0) + (g[r + 1][c] if r + 1 < h else 0)
                 + (g[r][c - 1] if c else 0) + (g[r][c + 1] if c + 1 < w else 0))
            if not n:
                loose += 1
    if loose:
        bad.append(f"{loose} loose px with no neighbour: reads as a traffic dot")
    rows = [any(r) for r in g]
    if any(not v for v in rows[rows.index(True):len(rows) - rows[::-1].index(True)]):
        bad.append("blank row inside the glyph: the tail will look detached")
    if w % 2:
        broken = sum(1 for r in g if not r[w // 2])
    else:                      # both middle pixels must be ink on every row
        broken = sum(1 for r in g if not (r[w // 2 - 1] and r[w // 2]))
    if broken:
        bad.append(f"centreline broken on {broken} row(s)")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file", nargs="?", help="glyph art or C++ table (default: stdin)")
    ap.add_argument("--force", action="store_true", help="write despite warnings")
    ap.add_argument("--to-even", action="store_true",
                    help="convert an odd-width glyph so it straddles the screen "
                         "centre point (99|100) instead of sitting half a px off")
    ap.add_argument("--print", dest="show", action="store_true", help="print, do not write")
    ap.add_argument("--origin", type=int,
                    help="hot-spot row: the aircraft's POSITION on the glyph, which "
                         "sits on the plot origin. Default: the widest (wing) row, "
                         "because bbox-centring skews every bearing on screen.")
    a = ap.parse_args()
    g = parse(open(a.file).read() if a.file else sys.stdin.read())
    if a.to_even:
        before = len(g[0])
        g = to_even(g)
        print(f"  width {before} -> {len(g[0])} (even: straddles the centre point)",
              file=sys.stderr)
    origin_in = a.origin
    before = (len(g[0]), len(g))
    g, shift = trim(g, a.origin if a.origin is not None else 0)
    if (len(g[0]), len(g)) != before:
        print(f"  trimmed blank edges: {before[0]}x{before[1]} -> {len(g[0])}x{len(g)}",
              file=sys.stderr)
        if origin_in is not None:
            a.origin = shift
    h, w = len(g), len(g[0])

    problems = check(g)
    for p in problems:
        print(f"  warning: {p}", file=sys.stderr)
    if problems and not (a.force or a.show):
        sys.exit("refusing to write; fix the above or pass --force")

    src = open(RADAR).read()
    # keep any hand-written note after the art on each row, if the shape still fits
    notes = re.findall(r"0x[0-9a-fA-F]{8},\s*//\s*[#.]+(.*)", src)
    if len(notes) != h:
        notes = [""] * h

    origin = a.origin
    if origin is None:
        widths = [sum(r) for r in g]
        origin = widths.index(max(widths))          # the wing = the CG, near enough
    print(f"  hot spot: row {origin} of {h} "
          f"({'given' if a.origin is not None else 'widest row'})", file=sys.stderr)

    body = "\n".join(
        f"    0x{sum(v << i for i, v in enumerate(row)):08x},"
        f"  // {''.join('#' if v else '.' for v in row)}{note}"
        for row, note in zip(g, notes))
    table = (f"constexpr int kGlyphW = {w};\n"
             f"constexpr int kGlyphOriginRow = {origin};\n"
             f"constexpr uint32_t kOwnshipRows[] = {{\n{body}\n}};")
    if a.show:
        print(table)
        return

    new, n = re.subn(r"constexpr int kGlyphW = \d+;\s*\nconstexpr int kGlyphOriginRow = \d+;\s*\n"
                     r"constexpr uint32_t kOwnshipRows\[\] = \{.*?\n\};",
                     table.replace("\\", "\\\\"), src, flags=re.S)
    if not n:
        sys.exit(f"could not find the kOwnshipRows table in {RADAR}")
    if new == src:
        print(f"{RADAR}: unchanged")
        return
    open(RADAR, "w").write(new)
    print(f"{RADAR}: {w} x {h} px, {sum(map(sum, g))} px ink")
    if any(notes):
        print("kept the per-row notes; check they still describe the right rows")
    print("next: make test && make web   (the comment block above the table is yours)")


if __name__ == "__main__":
    main()
