# core/radio

The station log: every burst this radio sent or heard, in the order it happened, newest first.

`rx_ok` and `tx_ok` are totals, and a total cannot tell an empty sky from a receiver that frames nothing. The traffic table only ever holds what already decoded, so a burst that arrived and did not become a frame leaves no trace in it. That burst is the one worth seeing: it is the difference between "nobody is transmitting" and "everybody is transmitting and I am deaf to them", and the two have the same reading on every other page. Two skyBlips that both transmit and neither hears is the fault this exists for, and it happened: `git log core/protocol/air.cpp`.

`Event` names the five things that can happen to a burst.

| | |
|---|---|
| `Transmitted` | own-ship's burst left the antenna |
| `Withheld` | the band was busy at every carrier sample the dwell had room for |
| `Lost` | own-ship's burst was armed and never completed: the dwell ran out, or the radio's transmit timeout did |
| `Received` | a burst arrived, framed, and named an aircraft |
| `Unframed` | a burst arrived and did not become a frame: wrong chips behind the sync window, a CRC no forward correction could rescue, or two transmitters at once |

`Lost` and `Unframed` are both `rx_bad` on the counters, which is wrong of the counters: `messages::RfEventType::Missed` is only ever emitted for a dwell or a transmission of ours that did not complete, never for a reception. The log is split because a transmit failure reported as a bad reception sends a reader hunting the wrong fault, which is the exact thing this page exists to stop. The counter keeps its old meaning until it is given one of its own: [#61](https://github.com/fcatuhe/skyblip/issues/61).

`Entry::at_s` is UTC once the receiver has given us a second, and time since boot before that. Which of the two is a flag per entry rather than a flag on the log, because the log outlives a first fix and the entries either side of one are dated differently.

The capacity is what one screen holds. Nothing is kept that could not be shown: this is a tape of what is happening now, not a history to scroll back through. `ui/screens/radio_log.cpp` draws exactly `Log::kCapacity` rows for that reason.

`go::TrafficService` is the only writer. It already drains `bus.rf`, and it is the one place that knows whether a burst that arrived also decoded, which is the distinction the whole page turns on.
