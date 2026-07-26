# skyblip

Firmware for the **skyBlip** (airborne) and **skyPost** (ground) product lines —
one shared pure-C++ `core/`, one composition root per product. EASA **ADS-L 4
SRD-860 Issue 2**.

Current focus is **skyBlip Go** (LilyGO **T-Echo Plus**: nRF52840 + SX1262 +
1.54" e-paper): air-to-air ADS-L TX/RX, ground→air uplink RX, e-paper radar +
collision alarms, ALP-TAS NMEA to EFBs over BLE.

## Layout (`firmware/`)

```
core/       pure C++, no framework, 100% host-tested
            protocol/ fec/ gnss/ timing/ traffic/ flight/ settings/ comms/ util/
ui/         1-bit framebuffer + radar / ALT-VS / status screens
hal/        capability ports (clock, link, display, kvstore, annunciator, dfu)
devices/    the only vendor zone: io/ drivers/ soc/zephyr/ boards/ host/ (fakes)
products/   skyblip/ (app.h = framework-agnostic root, main.cpp = Zephyr shell)
            sim/ (virtual T-Echo: terminal + WASM frontends)
test/       mirrored doctest suites
```

Dependencies point inward at contracts: `hal/` and `devices/io/` include nothing,
`core/` + `ui/` touch no framework header, `products/*` is where layers meet.
`App` holds all wiring and runs unchanged on device, host and in the browser.

## Build & test (host)

```
cd firmware
make test        # all doctest suites (-Werror) — 102 cases, 8108 assertions
make sim         # terminal simulator: ./build/skyblip_sim
make web         # browser simulator (needs Emscripten)
make serve       # serve the browser sim at :8000 with live reload
make watch       # incremental rebuild on save (WATCH=web|sim|test)
```

The simulator runs the real firmware against simulated sensors — GNSS emits
valid `$GPRMC`/`$GPGGA`, traffic is encoded as real scrambled ADS-L frames — so
the production parse/decode/fusion/alarm path is what executes. Nothing is
mocked below `App`.

## Build (device)

Zephyr v4.1, C++20, MCUboot-signed DFU. See
[`docs/MIGRATION-ZEPHYR.md`](docs/MIGRATION-ZEPHYR.md).

```
west init -l firmware && west update
west build -b t_echo_plus firmware --sysbuild
west flash
```

CI (`.github/workflows/ci.yml`) runs host tests, the test-coverage gate,
clang-format, the Zephyr build and a 512 KB flash-size budget, and uploads a
flashable artifact per push.

## skyShip

The ownship symbol — the aeroplane at the centre of the radar screen — is a 1-bit
sprite with a declared hot spot (`ui/screens/radar.cpp: kOwnshipRows[]`), not
hand-waved art. Design it in [`skyship-editor/`](skyship-editor/README.md):

```
open skyship-editor/index.html                    # pixel editor + live 200x200 preview
python3 skyship-editor/apply.py --origin 5 skyship-editor/skyship.txt
```

Even width (the 200x200 screen has no middle pixel — its centre is where 99 and
100 meet), hot spot on the plot origin, no loose pixels: a dot with no neighbour
reads as traffic. `apply.py` refuses sprites that break those. A user-loadable
sprite in QSPI, one per aircraft, is the next step; the compiled table is then
the fallback.

## Acknowledgements

skyBlip stands on a decade of open work by the free-flight community. Thanks to
the authors of the projects we learned from while building it:

- **Paweł Jałocha** — the ADS-L reference implementation and
  [nrf52-ogn-tracker](https://github.com/pjalocha/nrf52-ogn-tracker)
- **Linar Yusupov** — [SoftRF](https://github.com/lyusupov/SoftRF)
- **Moshe Braner** — the [SoftRF fork](https://github.com/moshe-braner/SoftRF)

## License

**GPL v3** — see [`LICENSE`](LICENSE).
