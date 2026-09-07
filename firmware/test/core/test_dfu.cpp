// The record a device writes before the swap, and what the image that boots next makes of it.
#include <cstring>
#include <string>

#include "core/dfu/smp_policy.h"
#include "core/dfu/update.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::dfu;

namespace {
UpdateRecord attempt() {
    UpdateRecord r;
    r.from = hal::ImageVersion{0, 1, 0, 12};
    r.to = hal::ImageVersion{0, 2, 0, 15};
    return r;
}
}  // namespace

TEST_CASE("dfu: the update record survives the blob byte for byte") {
    const UpdateRecord before = attempt();
    uint8_t blob[kUpdateRecordBytes];
    REQUIRE(to_blob(before, blob, sizeof(blob)) == kUpdateRecordBytes);
    UpdateRecord after;
    REQUIRE(from_blob(blob, sizeof(blob), after));
    CHECK(after.from == before.from);
    CHECK(after.to == before.to);
}

TEST_CASE("dfu: a blob of the wrong length is refused rather than half read") {
    uint8_t blob[kUpdateRecordBytes] = {0};
    UpdateRecord out;
    CHECK_FALSE(from_blob(blob, kUpdateRecordBytes - 1, out));
    CHECK_FALSE(from_blob(blob, 0, out));
    uint8_t small[kUpdateRecordBytes - 1];
    CHECK(to_blob(attempt(), small, sizeof(small)) == 0);
}

TEST_CASE("dfu: the image that boots after the swap is classified by what it is running") {
    const UpdateRecord r = attempt();
    CHECK(outcome(r, r.to) == Outcome::Landed);
    CHECK(outcome(r, r.from) == Outcome::Reverted);
    CHECK(outcome(r, hal::ImageVersion{0, 3, 0, 1}) == Outcome::Unrelated);
    CHECK(outcome(r, hal::ImageVersion{0, 2, 0, 16}) == Outcome::Unrelated);
}

TEST_CASE("dfu: two builds of one release are two images") {
    const UpdateRecord r = attempt();
    hal::ImageVersion other_build = r.to;
    other_build.build++;
    CHECK(r.to != other_build);
    CHECK(outcome(r, other_build) == Outcome::Unrelated);
}

TEST_CASE("dfu: a version reads as imgtool stamps it, and the widest one fits the cap") {
    char text[kVersionTextCap];
    CHECK(format_version(hal::ImageVersion{0, 1, 0, 12}, text, sizeof(text)) == 8);
    CHECK(std::string(text) == "0.1.0+12");
    const int widest =
        format_version(hal::ImageVersion{255, 255, 65535, 4294967295u}, text, sizeof(text));
    CHECK(std::string(text) == "255.255.65535+4294967295");
    CHECK(widest == static_cast<int>(kVersionTextCap) - 1);

    char tight[8];
    CHECK(format_version(hal::ImageVersion{0, 1, 0, 12}, tight, sizeof(tight)) == 0);
    CHECK(tight[0] == 0);
}

TEST_CASE("dfu: every image state has a name a phone can switch on") {
    CHECK(std::string(to_string(ImageState::Confirmed)) == "confirmed");
    CHECK(std::string(to_string(ImageState::Probation)) == "probation");
    CHECK(std::string(to_string(ImageState::Reverted)) == "reverted");
}

// The SMP permission matrix off the silicon; the Zephyr hook pins the numbers with static_assert.
namespace {
constexpr uint8_t kRead = static_cast<uint8_t>(SmpOp::Read);
constexpr uint8_t kWrite = static_cast<uint8_t>(SmpOp::Write);
constexpr uint16_t kOs = static_cast<uint16_t>(SmpGroup::Os);
constexpr uint16_t kImage = static_cast<uint16_t>(SmpGroup::Image);
}  // namespace

TEST_CASE("smp: every read passes, in or out of the upload window") {
    for (const bool window : {false, true}) {
        CHECK(smp_permitted({kOs, kSmpOsEcho, kRead}, window));
        CHECK(smp_permitted({kImage, kSmpImageState, kRead}, window));
        CHECK(smp_permitted({kImage, kSmpImageUpload, kRead}, window));
        CHECK(smp_permitted({7, 3, kRead}, window));
    }
}

TEST_CASE("smp: the upload is the one write the window opens") {
    CHECK_FALSE(smp_permitted({kImage, kSmpImageUpload, kWrite}, false));
    CHECK(smp_permitted({kImage, kSmpImageUpload, kWrite}, true));
}

TEST_CASE(
    "smp: marking pending, confirming, erasing and resetting are refused even inside the window") {
    for (const bool window : {false, true}) {
        CHECK_FALSE(smp_permitted({kImage, kSmpImageState, kWrite}, window));
        CHECK_FALSE(smp_permitted({kImage, kSmpImageErase, kWrite}, window));
        CHECK_FALSE(smp_permitted({kOs, kSmpOsReset, kWrite}, window));
        CHECK_FALSE(smp_permitted({7, 3, kWrite}, window));
    }
}

TEST_CASE(
    "smp: echo is a write a phone may always make, and a response opcode is never a request") {
    CHECK(smp_permitted({kOs, kSmpOsEcho, kWrite}, false));
    CHECK_FALSE(smp_permitted({kOs, kSmpOsEcho, static_cast<uint8_t>(SmpOp::WriteResponse)}, true));
    CHECK_FALSE(
        smp_permitted({kImage, kSmpImageUpload, static_cast<uint8_t>(SmpOp::ReadResponse)}, true));
}
