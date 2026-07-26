# scenarios/

Recorded flights + encounter scripts for the host simulator (3-ARCHITECTURE §8
skeleton). A scenario feeds the pure `core/` through the `devices/host` fakes:
scripted GNSS NMEA (fake L76K), scripted radio RX packets over the `BerChannel`,
and a simulated PPS phase into the slot `Scheduler`. Used for end-to-end
regression (see `test/core/test_scenario.cpp`) and PER-vs-dBm curves.

Format (planned): line-oriented CSV / NMEA logs captured on the bench, committed
as fixtures so a bug found in flight becomes a deterministic host test.
