# ui/input

Two contacts, four things a pilot can say, all of it pure logic: the board samples the pins and passes levels plus the time, so the simulator and the host tests drive the same code the silicon does.

| Gesture | On the traffic pages | In the settings mode |
|---|---|---|
| pad touched and released under `Pad::kHoldMs` | the next page | the next row |
| pad held past `Pad::kHoldMs` (1 s) | the radar | the radar |
| button pressed | opens the settings | changes the focused row |
| button held past `power::kLongPressMs` (2 s) | off | off |
| pad held through that press | off, with a blank panel: the stow | the stow |

One rule in two places: the pad moves, the button acts, and the long touch is the way back to the traffic picture from anywhere. Nothing a pilot learns on the pages has to be unlearned on the rows.

The pad carries the navigation because it is what a gloved thumb finds on the top edge without looking, and the page lands on the release rather than on the contact: that is what leaves room for the long touch in the same finger. A touch the button joins says nothing at all, in either direction, because that pair is already the stow and a device on its way off must not change page on the way.

`Pad::kSettleMs` is there for the same reason `Button::kDebounceMs` is, on different physics: a mechanical contact bounces, and a fingertip crossing the edge of a capacitive electrode flutters. Neither is anything a hand did.

## The button's third meaning

`ConfirmGesture` is the security boundary, not an input helper. This product ships with BLE pairing off, so nothing proves cryptographically that the phone asking for a firmware upload belongs to the pilot, and physical presence stands in for it. The gesture has to be one a thumb cannot produce by accident and one that cannot be confused with the press that opens the settings or the hold that switches the device off: a double press inside `kDoublePressMs` is the only one of the button's meanings a pilot has to mean to make.

It authorises nothing unless a prompt the pilot can read is on the glass, and a lone press at a prompt refuses the operation rather than leaving it standing. Fail closed, which is what makes "press twice to allow, once to refuse" true on the panel.
