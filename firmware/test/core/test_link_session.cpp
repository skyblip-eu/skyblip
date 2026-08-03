// The connection, as the firmware sees it. Both platforms drive this same
// object - Zephyr's connected/disconnected/att_mtu_updated callbacks on silicon,
// raise_link()/drop_link() on the host - so the rules a lifecycle has to obey are
// proved once, here, instead of living inside a Bluetooth callback nothing can
// reach from a host suite.
#include "core/comms/link_session.h"
#include "doctest/doctest.h"

using namespace skyblip;
using comms::LinkSession;
using messages::LinkEvent;
using messages::LinkEventType;

TEST_CASE("link session: a connect and a disconnect arrive as one ordered pair") {
    LinkSession session;
    CHECK_FALSE(session.up());

    LinkEvent event{};
    CHECK_FALSE(session.pop(event));

    session.connected(0x4231, 244);
    CHECK(session.up());
    CHECK(session.session_id() == 0x4231);

    REQUIRE(session.pop(event));
    CHECK(event.type == LinkEventType::Up);
    CHECK(event.session_id == 0x4231);
    CHECK(event.payload_bytes == 244);
    CHECK_FALSE(session.pop(event));

    session.disconnected(0x4231);
    CHECK_FALSE(session.up());
    REQUIRE(session.pop(event));
    CHECK(event.type == LinkEventType::Down);
    CHECK(event.session_id == 0x4231);
    CHECK_FALSE(session.pop(event));
}

// An iOS central connects first and exchanges the MTU afterwards, so the figure
// the link came up with is not the figure it will carry.
TEST_CASE("link session: a late MTU exchange refreshes the session it belongs to") {
    LinkSession session;
    session.connected(7, hal::kMinimumLinkPayload);
    LinkEvent event{};
    REQUIRE(session.pop(event));
    CHECK(event.payload_bytes == hal::kMinimumLinkPayload);

    session.payload_changed(182);
    REQUIRE(session.pop(event));
    CHECK(event.type == LinkEventType::Up);
    // The same central, saying a bigger number. A reader that treated this as a
    // new phone would throw away the prompt the old one is still answering.
    CHECK(event.session_id == 7);
    CHECK(event.payload_bytes == 182);
    CHECK(session.payload_bytes() == 182);

    // Said twice, it is not news.
    session.payload_changed(182);
    CHECK_FALSE(session.pop(event));

    // And an exchange with nobody on the other end is not an event at all.
    session.disconnected(7);
    REQUIRE(session.pop(event));
    session.payload_changed(247);
    CHECK_FALSE(session.pop(event));
}

TEST_CASE("link session: a payload below what BLE guarantees is floored, never carried") {
    LinkSession session;
    session.connected(1, 0);
    LinkEvent event{};
    REQUIRE(session.pop(event));
    CHECK(event.payload_bytes == hal::kMinimumLinkPayload);
    CHECK(session.payload_bytes() == hal::kMinimumLinkPayload);

    session.payload_changed(4);
    CHECK_FALSE(session.pop(event));
}

TEST_CASE("link session: a disconnect for a session that is not the one up is ignored") {
    LinkSession session;
    session.connected(3, 244);
    LinkEvent event{};
    REQUIRE(session.pop(event));

    session.disconnected(9);
    CHECK(session.up());
    CHECK_FALSE(session.pop(event));

    // And a disconnect with nothing up says nothing: a Down with no Up before it
    // would leave a reader cancelling a prompt that a live central raised.
    session.disconnected(3);
    REQUIRE(session.pop(event));
    session.disconnected(3);
    CHECK_FALSE(session.pop(event));
}

TEST_CASE("link session: a second central closes the first, so no reader sees two ups") {
    LinkSession session;
    session.connected(1, 244);
    LinkEvent event{};
    REQUIRE(session.pop(event));

    session.connected(2, 182);
    REQUIRE(session.pop(event));
    CHECK(event.type == LinkEventType::Down);
    CHECK(event.session_id == 1);
    REQUIRE(session.pop(event));
    CHECK(event.type == LinkEventType::Up);
    CHECK(event.session_id == 2);
    CHECK(session.session_id() == 2);
}

TEST_CASE("link session: a lifecycle event that does not fit is counted, not lost quietly") {
    LinkSession session;
    for (uint16_t i = 1; i <= LinkSession::kEventCapacity + 2; i++) {
        session.connected(i, 244);
    }
    CHECK(session.dropped() > 0);
    // What survives is still an ordered stream, and the last state the object
    // itself reports is the truth about the radio.
    CHECK(session.up());
    CHECK(session.session_id() == LinkSession::kEventCapacity + 2);
}
