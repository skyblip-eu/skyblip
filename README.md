# skyBlip

Open-source electronic conspicuity for general aviation: [ADS-L 4 SRD-860](https://www.easa.europa.eu/en/document-library/agency-decisions/ed-decision-2022024r) for aircraft (skyBlip) and ground stations (skyPost).

## What it does

[`docs/BEHAVIOR.md`](docs/BEHAVIOR.md) lists every claim the firmware makes about itself, straight from the host test suite that checks them. It is generated, so it cannot describe a behavior that stopped being true.

## The tree

| Directory | What lives there |
|---|---|
| [`firmware/`](firmware) | the C++ tree: `core/`, `ui/`, `hal/`, the Zephyr platform, the host test suite, and the simulator's world |
| [`simulator/`](simulator) | the development harness page that drives the WASM build of the firmware |
| [`website/`](website) | [skyblip.eu](https://skyblip.eu), a Rails app that Parklife renders to static files |
| `docs/`, `schemas/`, `scripts/`, `skyship/` | the generated behavior index, the wire schemas, the build and release tooling, the artwork |

The firmware and the site share a tree so that a change in behavior and the page documenting it can land in one commit. They do not share a build: each has its own workflow, gated on the paths it owns.

## Acknowledgements

skyBlip stands on a decade of open work by the free-flight community. Thanks to the authors of the projects we learned from while building it:

- **Paweł Jałocha**: the ADS-L reference implementation and [nrf52-ogn-tracker](https://github.com/pjalocha/nrf52-ogn-tracker)
- **Linar Yusupov**: [SoftRF](https://github.com/lyusupov/SoftRF)
- **Moshe Braner**: the [SoftRF fork](https://github.com/moshe-braner/SoftRF)

## License

The firmware, the simulator and the tooling are **GPL-3.0-only**: see [`LICENSE`](LICENSE).

The website is **MIT**, and carries its own [`website/LICENSE`](website/LICENSE). The root license does not reach into that directory.

Copyright (C) 2026 François Catuhe
