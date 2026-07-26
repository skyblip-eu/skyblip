# skyblip/docs

The authoritative engineering & decision records live one level up in the
workspace under `project/` (architecture, devices, roadmap) and
`project/decisions/` (config, repo topology). Per `decisions/repos.md §4`, the
decision sheets are mirrored into this repo's `docs/` at publication time.

Key references for this firmware:

- `3-ARCHITECTURE.md` — Part I airborne (this code), Part II ground (skyPost).
- `4-ROADMAP.md` — the host-first v1 plan (stages 2.0–2.9).
- `decisions/config.md` — BLE/JSON config, in-flight lockout, DFU trust.
- `reference/efb-formats.md` — ALP-TAS NMEA / GDL90 mapping from ADS-L.

This tree also owns the wire schemas (`../schemas/`) as the single source of
truth (`decisions/repos.md §2`): consumers (`website`, future `skyscope`/
`skybook`) pin a version and never hand-copy.
