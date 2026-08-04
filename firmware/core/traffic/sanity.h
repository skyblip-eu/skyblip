// core/traffic/sanity.h: what a decoded packet has to claim before it is allowed
// to be an aircraft.
//
// Every frame the table sees has already passed a CRC. That is not the same as
// being right: test/core/test_adsl.cpp counts silent miscorrections at the edge
// of the link budget and the count is not zero, and the O-band uplink adds
// Reed-Solomon miscorrection on top of it. A miscorrected position field decodes
// to a perfectly well-formed aircraft in the wrong place, and a ghost on the
// radar is worse than a missed target: it is the one failure that teaches a pilot
// to stop believing the instrument.
//
// So the gate is geometric, not statistical: a reception is only plausible if the
// position it claims is inside the range this radio could have heard it over.
// OGN does the same thing at 25 km and calls anything further a mis-decode
// (nrf52-ogn-tracker src/proc.cpp, the 25 km test it applies to every decoded
// packet before it enters its own traffic list).
#ifndef SKYBLIP_CORE_TRAFFIC_SANITY_H
#define SKYBLIP_CORE_TRAFFIC_SANITY_H

#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::traffic {

// The honest ceiling of our own link, computed rather than borrowed:
//
//   +14 dBm e.r.p. transmitted, which is the ERC 70-03 band h1.4 limit and what
//   the driver programs (hardware/parts/sx1262/sx1262.h);
//   about -107 dBm of receive sensitivity at the M band's 100 kchip/s with the
//   boosted gain of J1 - a bench figure, not a datasheet one;
//   dipole-referenced antennas at both ends and nothing at all in the way.
//
// That is 121 dB of budget, and free space at 868.2 MHz spends 120.8 dB of it by
// 30 km (core/traffic/link.h's own free_space_loss_db is where that number comes
// from, and test/core/test_traffic.cpp checks it still holds). A real
// installation loses several dB into a cockpit at each end, so the air-to-air
// figure in practice is a third of this: 30 km is a ceiling, never a promise.
//
// A ceiling is exactly what a sanity gate wants. It cannot hide a contact anyone
// could act on, because the alarm layer's outermost ring is 3 km
// (core/traffic/alarm.h kInfoDistM) and the radar's furthest range setting is
// well inside this figure, and it cannot be reached by a real transmitter on this
// band with this power. What is past it did not arrive over this link.
constexpr int32_t kMaxPlausibleRangeM = 30000;

// A relayed target did not travel that path. It travelled two: the aircraft to a
// ground station, and the ground station to us (core/protocol/adsl_uplink.h). The
// second hop is bounded by the figure above; the first is the station's own reach,
// which we cannot know from the frame and which is better than ours by
// construction - an outdoor antenna, on a mast, with no fuselage around it. So the
// budget for a relay is at least twice the single-hop one, and this is where a
// claimed position stops being a plausible two-hop path and becomes arithmetic on
// a corrupted field. Gating a relay at the single-hop figure would throw away
// exactly the distant traffic the uplink exists to provide.
constexpr int32_t kMaxRelayedRangeM = 2 * kMaxPlausibleRangeM;

// Which of the two applies to a report, from the path it came over.
int32_t plausible_range_m(messages::Source source);

// Why a reception was or was not believed. NoReference is not a verdict on the
// packet: without a fix of our own there is no point to measure from, and a
// target with no position of its own cannot be placed either. Both are kept -
// they are what the table already holds and what the screen already draws - and
// the range gate simply has nothing to say about them.
enum class Plausibility : uint8_t { Believable, NoReference, TooFar };

// slant_m is written whenever the answer is Believable or TooFar, so a caller
// that wants to log the refusal has the figure that caused it. Which ceiling is
// applied depends on obs.source, because that is what says how many hops the
// report crossed.
Plausibility range_check(const messages::OwnState& own, const messages::AircraftObs& obs,
                         int32_t& slant_m);

}  // namespace skyblip::traffic

#endif
