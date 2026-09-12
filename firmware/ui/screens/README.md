# ui/screens

One file per page, each one a pure function from a snapshot struct to pixels. A page reads nothing, owns nothing and decides nothing: `go::ScreenService` fills the snapshot and the page draws it, which is what lets every page be tested without a device, a bus or a clock.

## radar

Heading up, not north up. The own ship is drawn nose-up and cannot turn, so the picture turns instead: a target is plotted by how far ahead of the nose and how far right of it it lies, which is the bearing a pilot then looks along. North is wherever the letters say it went.

The digits at the top are GNSS track and carry no label. There is no magnetometer on the board, and `HDG` would claim one, so the page says a number and lets the cardinal letters give it its meaning. The letters ride just inside the outer ring, each on a patch of cleared glass so a target under one does not eat it, and a letter that a ring label is in the way of steps in along its own bearing until it clears. Only that letter moves: the bearing is the direction of the letter from the ship, not its distance, so stepping one in costs nothing, and pulling the other three in with it would cost the rose its radius for no reason.

Those two labels sit ON the outer ring with the line cleared under them: the track at the top, what the ring is worth at the bottom. The ring is the scale, so the number belongs on it rather than beside it, and a digit read through a black stroke is not read at all.

The ring is four nautical miles, and the range is carried in whole miles rather than in metres: the label is then the setting itself, and no rounding stands between what a pilot picked and what the glass says. Miles because that is what a pilot's other instruments and the airspace around them are marked in, and four because at the speeds this device is flown at a head-on conflict entering the ring is around two minutes away. Metres are the plot's business, one multiplication further in.

The footer counts the picture, not the receiver. Both numbers are as big as the track, and both sit on the range label's baseline rather than on the glass edge, because a number at the very bottom of a round window reads as an afterthought. Left is the satellites, the receiver's own state, and it reads `NO FIX` in place of `SAT` when there is no position, which is the one message the page owes a pilot staring at an empty plot. Right is the aircraft actually plotted, so a target beyond the ring is not in it. `SAT` and `ACT` stay in the small font, set beside their number: the count is what is read, the word only says what it counted. `signal` is where everything heard is listed.

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

Six dials over the GNSS-derived own-ship state, each with its own number: ground speed, attitude, altimeter, turn coordinator, track, vertical speed. The snapshot arrives in knots, feet and feet per minute, because the bank and flight-path geometry is worked in them.

The page reads outward from the middle. The six faces are a block: 66 px between centres in both axes, so the same 3 px of glass between two dials side by side and between the two rows, so the panel reads as one instrument rather than as two shelves. Each row's numbers and labels are stacked off its outer edge: number first at double size, label above or below it in the small font. Nothing is written between the rows, which is what pays for both the larger faces and that spacing, and the number a pilot glances at is then the biggest thing on the panel rather than a line of 5x7 text wedged under a needle.

`settings::units` decides the speed dial and nothing else here. Altitude stays in feet and vertical speed in feet per minute on both settings: a level is cleared in feet and a climb rate is flown to in feet per minute wherever the aeroplane is, and a pilot who reads km/h on the speed dial still reads feet on the altimeter. The status page has room for two columns and prints the aeronautical figure and the SI one side by side; a dial has one needle, so the one place a habit has to be asked for is the speed.

The card reads `TRK`, not `HDG`. It is GNSS course over ground, referenced to true north: there is no magnetometer on the board, and no magnetic variation model to turn true into magnetic, so labelling it a heading would claim a sensor and a datum the device does not have. In a crosswind it differs from the heading the compass shows, which is the pilot's to reconcile.

## settings

The panel half of "a pilot with no phone can change the things that matter". It is a list of rows a thumb walks and a small editor that decides what a press means, both pure: the page takes a snapshot, the editor takes the values in and hands new values back, so the service owns the state and the file owns the meaning.

The two contacts mean here what they mean everywhere else: a tap of the pad moves the focus down a row, a press of the button acts on the row the focus is on, and every further press steps the same field again, which is what makes a subscale settable with a thumb. No timing to get right, so nothing here can be produced by accident out of the hold that switches the device off, and a standing prompt takes the button away from this page entirely before the gesture that answers it can be armed.

A pilot cannot get stuck here: the rows only ever advance and the tap past the last one leaves, the button on the `Leave` row leaves, a long touch of the pad goes back to the radar, and a page nobody has touched for `kIdleReturnMs` shows the traffic again on its own.

## The others

| Page | What it answers |
|---|---|
| `sixpack` | what own-ship is doing: speed, altitude, vertical speed, track, turn |
| `status` | what the sensors say: fix, position, pressure, battery, UTC |
| `signal` | every emitter heard, nearest first, with the e.r.p. its level implies |
| `settings` | the values a pilot can change without a phone |
| `boot`, `confirm`, `installing` | the three moments that are not pages: coming up, being asked, being written |

`go::Page` lists them in the order the pad walks, and a long touch of the pad goes back to `Radar` from any of them. Settings is not on that walk at all: it is a mode the button opens, and alone among the pages it has no bit in `settings.page_mask`, because that is where the mask is changed and a mask that hid it would be one nobody could undo without a phone.
