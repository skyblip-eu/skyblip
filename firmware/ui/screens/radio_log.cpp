#include "ui/screens/radio_log.h"

#include "core/util/format.h"

namespace skyblip::ui {

namespace {
constexpr int kLeft = 4;
constexpr int kCellW = 6;  // the 5x7 font's advance at scale 1
constexpr int kColumn(int cell) { return kLeft + cell * kCellW; }

constexpr int kTitleY = 2;
constexpr int kGnssY = 13;
constexpr int kRuleY = 23;
constexpr int kFirstRowY = 27;
constexpr int kLineH = 10;

constexpr int kClockX = kColumn(0);
constexpr int kWayX = kColumn(9);
constexpr int kBandX = kColumn(12);
constexpr int kVerdictX = kColumn(14);
constexpr int kAddrX = kColumn(19);
constexpr int kRssiEnd = kColumn(30);
constexpr int kRightEnd = kColumn(32);

constexpr uint8_t kFewestSatsForAltitude = 4;
constexpr uint32_t kUptimeClockWrapS = 10000;

void right_aligned(Framebuffer& fb, int x_end, int y, const char* text, int len) {
    fb.draw_text(x_end - len * kCellW, y, text, true, 1);
}

bool own_burst(radio::Event event) {
    return event == radio::Event::Transmitted || event == radio::Event::Withheld ||
           event == radio::Event::Lost;
}

bool names_one_emitter(messages::Source source) { return source != messages::Source::AdslUplink; }

const char* verdict_of(const radio::Entry& entry) {
    switch (entry.event) {
        case radio::Event::Transmitted: return "SENT";
        case radio::Event::Withheld: return "HELD";
        case radio::Event::Lost: return "LOST";
        case radio::Event::Unframed: return "BAD";
        case radio::Event::Received:
        default: return nullptr;
    }
}

int fmt_stamp(char* out, const radio::Entry& entry) {
    if (entry.utc) return fmt_seconds_of_day(out, entry.at_s);
    int n = fmt_string(out, "T+");
    return n + fmt_uint(out + n, entry.at_s % kUptimeClockWrapS);
}

void draw_title(Framebuffer& fb, const RadioLogSnapshot& snap) {
    fb.draw_text(kLeft, kTitleY, "RADIO LOG", true, 1);

    char buf[20];
    int n = fmt_string(buf, "RX ");
    n += fmt_uint(buf + n, snap.rx_ok);
    n += fmt_string(buf + n, " TX ");
    n += fmt_uint(buf + n, snap.tx_ok);
    buf[n] = 0;
    right_aligned(fb, kRightEnd, kTitleY, buf, n);
}

void draw_gnss(Framebuffer& fb, const GnssReception& gnss) {
    char buf[34];
    int n = fmt_string(buf, "GNSS ");
    if (!gnss.fix_valid) {
        n += fmt_string(buf + n, "NO FIX");
    } else {
        n += fmt_string(buf + n, gnss.sats >= kFewestSatsForAltitude ? "3D " : "2D ");
        n += fmt_uint(buf + n, gnss.sats);
        n += fmt_string(buf + n, "SV");
        if (gnss.hdop_e2 != 0) {
            n += fmt_string(buf + n, " H");
            n += fmt_uint(buf + n, gnss.hdop_e2 / 10u, 2, 1);
        }
    }
    if (gnss.pps_locked) n += fmt_string(buf + n, " PPS");
    buf[n] = 0;
    fb.draw_text(kLeft, kGnssY, buf, true, 1);

    n = fmt_uint(buf, gnss.solutions);
    n += fmt_string(buf + n, " FIX");
    buf[n] = 0;
    right_aligned(fb, kRightEnd, kGnssY, buf, n);
}

void draw_row(Framebuffer& fb, int y, const radio::Entry& entry) {
    char buf[16];

    int n = fmt_stamp(buf, entry);
    buf[n] = 0;
    fb.draw_text(kClockX, y, buf, true, 1);

    fb.draw_text(kWayX, y, own_burst(entry.event) ? "TX" : "RX", true, 1);

    buf[0] = entry.band == messages::Band::O ? 'O' : 'M';
    buf[1] = 0;
    fb.draw_text(kBandX, y, buf, true, 1);

    const char* verdict = verdict_of(entry);
    if (verdict != nullptr) {
        fb.draw_text(kVerdictX, y, verdict, true, 1);
    } else {
        buf[0] = messages::source_letter(entry.source);
        buf[1] = 0;
        fb.draw_text(kVerdictX, y, buf, true, 1);
        if (names_one_emitter(entry.source)) {
            n = fmt_hex(buf, entry.addr, 6);
            buf[n] = 0;
            fb.draw_text(kAddrX, y, buf, true, 1);
        }
    }

    if (!entry.rssi_valid) return;
    n = fmt_int(buf, entry.rssi_dbm, 1, 0, false);
    buf[n] = 0;
    right_aligned(fb, kRssiEnd, y, buf, n);
}

}  // namespace

void draw_radio_log(Framebuffer& fb, const RadioLogSnapshot& snap) {
    draw_title(fb, snap);
    draw_gnss(fb, snap.gnss);
    fb.hline(kLeft, kRuleY, kRightEnd - kLeft, true);

    if (snap.log == nullptr || snap.n_rows == 0) {
        fb.draw_text(kLeft, kFirstRowY + kLineH, "NOTHING ON AIR YET", true, 1);
        return;
    }

    const int rows = snap.n_rows < kRadioLogRows ? snap.n_rows : kRadioLogRows;
    for (int i = 0; i < rows; i++) draw_row(fb, kFirstRowY + i * kLineH, snap.log->newest(i));
}

}  // namespace skyblip::ui
