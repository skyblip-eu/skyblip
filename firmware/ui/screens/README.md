# ui/screens

One file per page, each one a pure function from a snapshot struct to pixels. A page reads nothing, owns nothing and decides nothing: `go::ScreenService` fills the snapshot and the page draws it, which is what lets every page be tested without a device, a bus or a clock.

## radar

Heading up, not north up. The own ship is drawn nose-up and cannot turn, so the picture turns instead: a target is plotted by how far ahead of the nose and how far right of it it lies, which is the bearing a pilot then looks along. North is wherever the letters say it went.

The digits at the top are GNSS track and carry no label. There is no magnetometer on the board, and `HDG` would claim one, so the page says a number and lets the cardinal letters give it its meaning. They ride an inner radius rather than the outer ring, because the ring's top and bottom are taken by the two labels.

Those two labels sit ON the outer ring with the line cleared under them: the track at the top, what the ring is worth at the bottom. The ring is the scale, so the number belongs on it rather than beside it, and a digit read through a black stroke is not read at all.

The footer counts the picture, not the receiver. Left is the aircraft actually plotted, so a target beyond the ring is not in it. Right is the satellites behind them, which is what separates an empty sky from a device that has lost the sky. `signal` is where everything heard is listed.

## radio_log

The station log, newest at the top, `radio::Log::kCapacity` rows and no more: what scrolls off the bottom is gone, because the page is a tape of what is happening now rather than a history.

Each row is one burst.

```
12:34:56 RX M A 3FA21C -87     an ADS-L frame from 3FA21C
12:34:56 RX M F 4C11A0 -93     an ALP-TAS frame, same dwell
12:34:55 RX M BAD       -101   a burst that arrived and never framed
12:34:55 TX M SENT             own-ship's burst left the antenna
12:34:54 TX M HELD             listen-before-talk never found the band clear
12:34:53 TX M LOST             armed, and the radio never reported it sent
```

The columns are the stamp, the direction, the band the dwell was armed for, the verdict, the emitter's address and the level it arrived at. `BAD` is the one that matters: a burst reached the dwell and did not become a frame. It is the only reading on the device that separates an empty sky from a receiver that hears everything and frames none of it, and that second case is a real fault that once shipped, see `git log core/protocol/air.cpp`.

The stamp is UTC as `hh:mm:ss` once the receiver has given us a second, and `T+<seconds>` since boot before that. Two shapes rather than one, so a reading is never taken for a wall clock it is not. A bench indoors never gets a fix and would otherwise have a column of dashes.

The GNSS line is on this page for the same reason the log is: a radio that hears nothing and a radio that is not being told where it is read identically on every other page. The solution count beside it is the one that separates a receiver saying nothing at all from one saying it cannot see the sky.

## sixpack

Six dials over the GNSS-derived own-ship state, each with its own number under it: ground speed, attitude, altimeter, turn coordinator, track, vertical speed. The snapshot arrives in knots, feet and feet per minute, because the bank and flight-path geometry is worked in them.

`settings::units` decides the speed dial and nothing else here. Altitude stays in feet and vertical speed in feet per minute on both settings: a level is cleared in feet and a climb rate is flown to in feet per minute wherever the aeroplane is, and a pilot who reads km/h on the speed dial still reads feet on the altimeter. The status page has room for two columns and prints the aeronautical figure and the SI one side by side; a dial has one needle, so the one place a habit has to be asked for is the speed.

The card reads `TRK`, not `HDG`. It is GNSS course over ground, referenced to true north: there is no magnetometer on the board, and no magnetic variation model to turn true into magnetic, so labelling it a heading would claim a sensor and a datum the device does not have. In a crosswind it differs from the heading the compass shows, which is the pilot's to reconcile.

## The others

| Page | What it answers |
|---|---|
| `sixpack` | what own-ship is doing: speed, altitude, vertical speed, track, turn |
| `status` | what the sensors say: fix, position, pressure, battery, UTC |
| `signal` | every emitter heard, nearest first, with the e.r.p. its level implies |
| `settings` | the values a pilot can change without a phone |
| `boot`, `confirm`, `installing` | the three moments that are not pages: coming up, being asked, being written |

`go::Page` lists them in the order the button walks. Settings is last and, alone among them, has no bit in `settings.page_mask`: it is where the mask is changed, so a mask that hid it would be one nobody could undo without a phone.
