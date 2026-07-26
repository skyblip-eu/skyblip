# skyship-editor

**skyShip** is skyBlip's *ownship symbol* — the little aeroplane at the centre of
the radar screen, the thing that means "you". It is a 1-bit **sprite** with a
declared **hot spot**, and the hand-adjustment that keeps it legible at 24 px is
**hinting**, exactly as in fonts.

These tools produce the **compiled default** in
`firmware/ui/screens/radar.cpp`. Later a user will be able to load their own
sprite into the device's QSPI flash and pick one per aircraft; the default then
becomes the fallback. `Ownship` in `radar.cpp` is already a value carrying its
own width, height and hot spot, so that change is storage plumbing, not a
rewrite of the renderer.

## The three tools

| | |
|---|---|
| `index.html` | The editor. Open it directly (`file://`) — no server, no build, no dependencies. Draw with mirror lock over the reference outline, watch the 200×200 screen live, copy the C++ table. |
| `trace.py` | Turn a plan-view drawing you supply into a starting sprite: flood-fill the line art to a silhouette, de-rotate by maximising mirror symmetry, area-average to N px of span, pick the threshold by IoU. Needs python3 + numpy + pillow. |
| `apply.py` | Write a sprite into `radar.cpp` in place. Accepts the ASCII art or a `kOwnshipRows[]` table, trims blank edges, keeps the per-row comments, refuses sprites that would look broken at 1 bit. |

```sh
open index.html                                 # draw
python3 apply.py skyship.txt                    # ship it as the default
python3 trace.py 30 --plan drawing.png          # trace an outline
python3 trace.py 24 --plan d.png --sweep 2 --prop 6
python3 apply.py --print --to-even old.txt      # convert an odd-width sprite
```

`skyship.txt` is the current symbol: **24 × 16, hot spot row 5**.

## Rules the sprite has to follow

`apply.py` and the editor enforce the same list, because each one has bitten us:

- **Even width.** The 200×200 screen has no middle pixel — its centre is the
  point where pixels 99 and 100 meet. An even sprite straddles that point
  exactly; an odd one sits half a pixel off it. The rings and every target
  offset use the same convention.
- **Hot spot, not bounding box.** The row that means "where I am" lands on the
  centre point. Bounding-box centring a long-tailed shape put the wing 4 px
  forward, which drew a target 20 px abeam **11° off its true bearing** and 4 %
  off in range. DO-257A asks for placement within 1 % of the depiction (2 px
  here), so that was twice the accepted figure. Bonus in the current sprite: it
  has two full-span wing rows, and they land on 99 and 100 — so the hot spot is
  exact vertically too.
- **No loose pixels.** A dot with no neighbour reads as *traffic* on a collision
  display. This is the one that matters most, and it is why fine details from a
  trace get cleaned up.
- **Continuous centreline**, both middle pixels on every row, and **mirror
  symmetry** about the centre point.

## Fidelity, and what the score does not tell you

The editor carries a **reference outline** — a low-wing single in plan, nose-up —
as the ghost under the grid, and scores the sprite against it: **IoU**, plus
*missing* (grey — airframe you have not drawn) and *extra* (orange — ink the
outline does not have). Two caveats, both easy to misread:

- It is a **shape** score. The comparison stretches the sprite onto the outline,
  so aspect differences are normalised away — watch **span/length** separately.
- Anything the outline does not draw counts as "extra". A propeller usually is
  not drawn on a plan view, so drawing one always costs IoU. That is a fair
  trade, not an error.
- Load your own outline with **replace outline** to hint against a different
  airframe: any image, at any polarity (light-on-dark is detected and flipped).

Small features hit a floor: below about 2 px they stop reading at all, so on a
24 px-span sprite the fuselage, propeller and tailplane are all deliberately
oversize relative to a real airframe. Overall proportion is traded for legibility
of the details, on purpose.

## Terminology

**Ownship** is the term of art — RTCA **DO-257A**, invoked by FAA **TSO-C165**.
"Own aircraft" is the plain-English variant common in European and FLARM
documentation. *Owncraft* is not used. The firmware keeps the standard name
(`kOwnshipRows[]`, `struct Ownship`); **skyShip** is our name for the symbol
itself, which is also why it echoes it.
