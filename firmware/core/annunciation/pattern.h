// core/annunciation/pattern.h: what the buzzer should be doing right now.
//
// One decision, in one place. The traffic tracker says which level stands and
// whether it just got worse; this says how that sounds - on times, off times,
// how many, and how often it is said again - and it is the only thing allowed
// to hand the buzzer from one voice to another. hal::Annunciator::alarm() opens
// a continuous tone that runs until silence(), so a level with no off time is a
// level that never stops: every pattern here ends by itself.
//
// The patterns are told apart by RHYTHM, never by pitch, because the pitch step
// is a hardware question that may still move:
//
//   level 1 info       one short blip, then nothing until it changes
//   level 2 important  two beeps, beep-length gap: a deliberate "look"
//   level 3 urgent     six fast pulses, re-announced while it stands
//   first fix          one long chirp, and it loses the buzzer to any traffic
//
// INFO: al 02aug26 the shape is FLARM's, by way of SoftRF's fork: one long beep
// at low, a short pair at important, a fast train at urgent
// (oss/SoftRF-moshe-braner .../src/driver/Buzzer.h:36-42, Buzzer.cpp:172-215).
// Upstream SoftRF has no cadence at all - one 1000 ms tone, re-armed every 2 s
// (oss/SoftRF-lyusupov .../src/driver/Sound.cpp:39-58) - which is the bug this
// file exists to not have.
#ifndef SKYBLIP_CORE_ANNUNCIATION_PATTERN_H
#define SKYBLIP_CORE_ANNUNCIATION_PATTERN_H

#include <cstdint>

namespace skyblip::annunciation {

// A piezo reaches full amplitude in a couple of milliseconds, so this is an ear
// figure, not a driver one: shorter than this and a blip is a click that a
// pilot cannot place, and cannot count.
constexpr uint16_t kShortestBlipEarCanPlaceMs = 90;

// Level 1 in a thermal is heard dozens of times in one climb. One blip at the
// floor of what the ear can place is the whole announcement.
constexpr uint16_t kInfoDiscreetBlipMs = 120;
constexpr uint8_t kInfoDiscreetBlipCount = 1;

// Level 2 is "look now": two beeps long enough to be separate events, with a
// gap the same length so the pair reads as a pair rather than as one stutter.
constexpr uint16_t kImportantPairBeepMs = 250;
constexpr uint16_t kImportantPairGapSameAsBeepMs = 250;
constexpr uint8_t kImportantPairBeepCount = 2;

// Level 3 is the one a pilot must not be able to ignore or mistake: a train of
// pulses at 5.5 Hz, which no other pattern here comes near, and which reads as
// urgency on its own without any change of pitch.
constexpr uint16_t kUrgentTrainPulseMs = 90;
constexpr uint16_t kUrgentTrainGapSameAsPulseMs = 90;
constexpr uint8_t kUrgentTrainPulseCount = 6;

// A standing urgent keeps saying so. The interval is the tracker's own
// re-notification cadence (traffic::kRenotifyMs), so the train and the alert
// that produced it stay on one clock, and the train (990 ms) is followed by
// about a second of quiet a call or a vario can be heard in.
constexpr uint16_t kUrgentStandingReannounceMs = 2000;

// A fix is worth a chirp, not an alarm: one tone, longer than any traffic beep
// so it cannot be mistaken for one, at the lowest step, and no haptics at all.
// INFO: al 02aug26 SoftRF plays a six-note jingle on the very first fix
// (oss/SoftRF-lyusupov .../src/platform/nRF52.cpp:2767-2787); the moshe-braner
// fork adds a two-tone confirmation before it will transmit. This is the
// confirmation half, and it is the one voice traffic is allowed to interrupt.
constexpr uint16_t kFirstFixChirpMs = 400;
constexpr uint8_t kFirstFixChirpLevel = 1;

// The shortest phase any pattern asks the service loop to resolve. The loop
// cadence has to divide into this several times over or the pattern the ear
// gets is not the pattern written above; products assert that against their own
// step rate.
constexpr uint16_t kShortestPhaseMs = kUrgentTrainPulseMs;
static_assert(kShortestPhaseMs >= kShortestBlipEarCanPlaceMs, "a phase the ear cannot place");

struct Pattern {
    uint16_t tone_ms{0};
    uint16_t gap_ms{0};
    uint8_t repeats{0};
    // 0: said once, and not again until something changes.
    uint16_t reannounce_ms{0};
};

// Who is holding the buzzer. Traffic outranks the fix chirp, always: the rule
// is written here and nowhere else.
enum class Voice : uint8_t { None, FirstFix, Traffic };

Pattern pattern_for(Voice voice, uint8_t level);

// Everything the decision depends on, gathered by the service that owns the
// annunciator and passed in whole.
struct Situation {
    // The level being announced right now, after the tracker's hysteresis.
    uint8_t level{0};
    // A target got worse on this pass: say it again even at the same level.
    bool escalated{false};
    // The first fix landed on this pass.
    bool first_fix{false};
    // settings.alarm_enabled.
    bool enabled{true};
    // False from the moment the device starts going down.
    bool running{true};
};

// What the buzzer should be doing, and whether that differs from what it was
// already told. A PWM re-armed on every service pass is a click, not a tone, so
// the service acts on changed and on nothing else.
struct Command {
    bool tone_on{false};
    uint8_t tone_level{0};
    bool changed{false};
};

class Policy {
   public:
    Command update(const Situation& situation, uint32_t now_ms);

    Voice voice() const { return voice_; }
    // The level being announced, gaps included: a pattern between two pulses is
    // still announcing. 0 when the buzzer has been released.
    uint8_t announcing_level() const { return voice_ == Voice::Traffic ? level_ : 0; }
    bool sounding() const { return commanded_on_; }
    uint8_t said() const { return said_; }

   private:
    void begin(Voice voice, uint8_t level, uint32_t now_ms);
    void advance(uint32_t now_ms);
    void release();
    Command emit();

    Pattern pattern_{};
    Voice voice_{Voice::None};
    uint8_t level_{0};
    uint8_t said_{0};
    uint32_t announced_ms_{0};
    uint32_t phase_ms_{0};
    bool phase_on_{false};
    bool commanded_on_{false};
    uint8_t commanded_level_{0};
};

}  // namespace skyblip::annunciation

#endif
