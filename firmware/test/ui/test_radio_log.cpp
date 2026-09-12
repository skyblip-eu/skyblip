// The page a bench reads: what it draws per burst, and what it refuses to invent.
#include <cstring>

#include "doctest/doctest.h"
#include "ui/framebuffer.h"
#include "ui/screens/radio_log.h"

using namespace skyblip;
using namespace skyblip::ui;

namespace {

constexpr int kFirstRowY = 27;
constexpr int kLineH = 10;

// Text read back off the glass: the same string on a scratch panel, compared where it lands.
bool shows(const Framebuffer& fb, int x, int y, const char* text) {
    Framebuffer expected;
    const int width = expected.draw_text(x, y, text, true, 1) - x;
    for (int row = y; row < y + 8; row++)
        for (int col = x; col < x + width; col++)
            if (fb.get_pixel(col, row) != expected.get_pixel(col, row)) return false;
    return true;
}

int ink_in_row(const Framebuffer& fb, int index) {
    const int y0 = kFirstRowY + index * kLineH;
    int n = 0;
    for (int y = y0; y < y0 + 8; y++)
        for (int x = 0; x < Framebuffer::kW; x++)
            if (fb.get_pixel(x, y)) n++;
    return n;
}

radio::Entry entry_of(radio::Event event) {
    radio::Entry e{};
    e.event = event;
    e.band = messages::Band::M;
    e.at_s = 45296;  // 12:34:56
    e.utc = true;
    return e;
}

radio::Entry received(uint32_t addr, int8_t rssi) {
    radio::Entry e = entry_of(radio::Event::Received);
    e.source = messages::Source::AdslDirect;
    e.addr = addr;
    e.rssi_dbm = rssi;
    e.rssi_valid = true;
    return e;
}

RadioLogSnapshot with(const radio::Log& log) {
    RadioLogSnapshot snap;
    snap.gnss.fix_valid = true;
    snap.gnss.sats = 9;
    snap.gnss.hdop_e2 = 120;
    snap.gnss.pps_locked = true;
    snap.gnss.solutions = 1234;
    snap.n_rows = log.count();
    snap.log = &log;
    return snap;
}

}  // namespace

TEST_CASE("radio log page: a received frame shows when, from whom, and how loud") {
    radio::Log log;
    log.record(received(0x3FA21C, -87));

    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK(shows(fb, 4, kFirstRowY, "12:34:56"));
    CHECK(shows(fb, 4 + 9 * 6, kFirstRowY, "RX"));
    CHECK(shows(fb, 4 + 12 * 6, kFirstRowY, "M"));
    CHECK(shows(fb, 4 + 14 * 6, kFirstRowY, "A"));
    CHECK(shows(fb, 4 + 19 * 6, kFirstRowY, "3FA21C"));
    CHECK(shows(fb, 4 + 27 * 6, kFirstRowY, "-87"));
}

// The one row that separates an empty sky from a receiver that frames nothing.
TEST_CASE("radio log page: a burst that never framed says so instead of naming an aircraft") {
    radio::Log log;
    radio::Entry bad = entry_of(radio::Event::Unframed);
    bad.rssi_dbm = -101;
    bad.rssi_valid = true;
    bad.addr = 0x3FA21C;
    log.record(bad);

    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK(shows(fb, 4 + 14 * 6, kFirstRowY, "BAD"));
    CHECK_FALSE(shows(fb, 4 + 19 * 6, kFirstRowY, "3FA21C"));
}

// A transmission that failed, read as a bad reception, sends a reader after the wrong fault.
TEST_CASE("radio log page: own-ship's three outcomes read apart, and all read as TX") {
    struct Case {
        radio::Event event;
        const char* verdict;
    };
    const Case cases[] = {{radio::Event::Transmitted, "SENT"},
                          {radio::Event::Withheld, "HELD"},
                          {radio::Event::Lost, "LOST"}};
    for (const Case& c : cases) {
        radio::Log log;
        log.record(entry_of(c.event));
        Framebuffer fb;
        draw_radio_log(fb, with(log));
        CHECK(shows(fb, 4 + 9 * 6, kFirstRowY, "TX"));
        CHECK(shows(fb, 4 + 14 * 6, kFirstRowY, c.verdict));
    }
}

// A level nothing measured is a level the page does not print.
TEST_CASE("radio log page: a burst with no level reported shows none") {
    radio::Log log;
    log.record(entry_of(radio::Event::Unframed));

    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK_FALSE(shows(fb, 4 + 27 * 6, kFirstRowY, "+0"));
    CHECK(shows(fb, 4 + 14 * 6, kFirstRowY, "BAD"));
}

TEST_CASE("radio log page: the newest burst is the top row and the rest fall away") {
    radio::Log log;
    for (int i = 0; i < radio::Log::kCapacity + 3; i++)
        log.record(received(static_cast<uint32_t>(0xA00000 + i), -80));

    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK(shows(fb, 4 + 19 * 6, kFirstRowY, "A00012"));
    CHECK(ink_in_row(fb, kRadioLogRows - 1) > 0);
    CHECK(ink_in_row(fb, kRadioLogRows) == 0);
}

// Before a fix the stamp is not a wall clock, and does not pretend to be one.
TEST_CASE("radio log page: without UTC the stamp counts from boot") {
    radio::Log log;
    radio::Entry e = received(0x3FA21C, -87);
    e.utc = false;
    e.at_s = 412;
    log.record(e);

    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK(shows(fb, 4, kFirstRowY, "T+412"));
}

TEST_CASE("radio log page: the GNSS line states the fix, the satellites and the lock") {
    radio::Log log;
    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK(shows(fb, 4, 13, "GNSS 3D 9SV H1.2 PPS"));
}

TEST_CASE("radio log page: no fix is stated rather than left blank") {
    radio::Log log;
    RadioLogSnapshot snap = with(log);
    snap.gnss.fix_valid = false;
    snap.gnss.pps_locked = false;
    snap.gnss.sats = 0;

    Framebuffer fb;
    draw_radio_log(fb, snap);
    CHECK(shows(fb, 4, 13, "GNSS NO FIX"));
}

TEST_CASE("radio log page: a silent band says so rather than showing an empty grid") {
    radio::Log log;
    Framebuffer fb;
    draw_radio_log(fb, with(log));
    CHECK(shows(fb, 4, kFirstRowY + kLineH, "NOTHING ON AIR YET"));
}
