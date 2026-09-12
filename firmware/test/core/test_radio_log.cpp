// The station log: newest first, and nothing kept that a screen could not show.
#include "core/radio/log.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

radio::Entry heard(uint32_t addr, uint32_t at_s) {
    radio::Entry e{};
    e.event = radio::Event::Received;
    e.source = messages::Source::AdslDirect;
    e.addr = addr;
    e.at_s = at_s;
    e.rssi_dbm = -87;
    e.rssi_valid = true;
    e.utc = true;
    return e;
}

}  // namespace

TEST_CASE("radio log: an empty log has nothing to read") {
    radio::Log log;
    CHECK(log.count() == 0);
}

TEST_CASE("radio log: the burst that just happened is the one at the top") {
    radio::Log log;
    log.record(heard(0xAAAAAA, 10));
    log.record(heard(0xBBBBBB, 11));
    log.record(heard(0xCCCCCC, 12));

    REQUIRE(log.count() == 3);
    CHECK(log.newest(0).addr == 0xCCCCCC);
    CHECK(log.newest(1).addr == 0xBBBBBB);
    CHECK(log.newest(2).addr == 0xAAAAAA);
}

// What falls off the bottom of the screen is gone: the page is a tape, not a history.
TEST_CASE("radio log: past capacity the oldest burst is the one dropped") {
    radio::Log log;
    for (int i = 0; i < radio::Log::kCapacity + 5; i++)
        log.record(heard(static_cast<uint32_t>(i), static_cast<uint32_t>(i)));

    CHECK(log.count() == radio::Log::kCapacity);
    CHECK(log.newest(0).addr == radio::Log::kCapacity + 4);
    CHECK(log.newest(radio::Log::kCapacity - 1).addr == 5);
}

TEST_CASE("radio log: every outcome a burst can have is one it keeps") {
    radio::Log log;
    for (radio::Event event :
         {radio::Event::Transmitted, radio::Event::Withheld, radio::Event::Lost,
          radio::Event::Received, radio::Event::Unframed}) {
        radio::Entry e{};
        e.event = event;
        log.record(e);
        CHECK(log.newest(0).event == event);
    }
}

TEST_CASE("radio log: a cleared log holds nothing and starts over") {
    radio::Log log;
    log.record(heard(0xAAAAAA, 10));
    log.clear();
    REQUIRE(log.count() == 0);

    log.record(heard(0xBBBBBB, 11));
    REQUIRE(log.count() == 1);
    CHECK(log.newest(0).addr == 0xBBBBBB);
}
