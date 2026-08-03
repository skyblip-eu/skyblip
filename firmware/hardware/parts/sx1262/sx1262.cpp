#include "hardware/parts/sx1262/sx1262.h"

namespace skyblip::parts {

Status Sx1262::wait_busy_low(uint32_t max_spins) {
    for (uint32_t i = 0; i < max_spins; i++) {
        if (!gpio_.get(busy_)) return Status::Ok;
    }
    return Status::Timeout;
}

void Sx1262::cmd(uint8_t opcode, const uint8_t* params, size_t n) {
    spi_.select(true);
    uint8_t op = opcode;
    spi_.transfer(&op, nullptr, 1);
    if (n) spi_.transfer(params, nullptr, n);
    spi_.select(false);
}

void Sx1262::cmd_read(uint8_t opcode, uint8_t* out, size_t n) {
    spi_.select(true);
    uint8_t op = opcode;
    spi_.transfer(&op, nullptr, 1);
    uint8_t nop = 0;
    spi_.transfer(&nop, nullptr, 1);
    for (size_t i = 0; i < n; i++) {
        uint8_t tx = 0;
        spi_.transfer(&tx, &out[i], 1);
    }
    spi_.select(false);
}

// The only delay this driver can spend: io::Gpio offers no sleep, so the pulse
// is counted in BUSY reads, each one a virtual call the compiler cannot fold.
void Sx1262::hold_reset_low() {
    for (uint32_t i = 0; i < sx::kResetLowSpins; i++) (void)gpio_.get(busy_);
}

Status Sx1262::enter_standby() {
    uint8_t stby = 0;  // STDBY_RC
    cmd(sx::kSetStandby, &stby, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    mode_ = RadioMode::Standby;
    return Status::Ok;
}

Status Sx1262::reset_to_standby() {
    gpio_.mode_output(reset_);
    gpio_.mode_input(busy_, false);
    gpio_.mode_input(dio1_, false);
    gpio_.set(reset_, false);
    hold_reset_low();
    gpio_.set(reset_, true);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    return enter_standby();
}

// BUSY going low proves a pull-down and a power rail, which a radio that is not
// fitted, not soldered on its MISO pad or held in reset also manages. Only a
// value that came back out of the part proves the part.
Status Sx1262::verify_link() {
    write_register(sx::kSyncWordRegister, sx::kLinkProbePattern, sizeof(sx::kLinkProbePattern));
    uint8_t back[sizeof(sx::kLinkProbePattern)] = {0};
    read_register(sx::kSyncWordRegister, back, sizeof(back));
    for (size_t i = 0; i < sizeof(back); i++)
        if (back[i] != sx::kLinkProbePattern[i]) return Status::Down;
    return Status::Ok;
}

Status Sx1262::probe() {
    const Status s = reset_to_standby();
    if (s != Status::Ok) return s;
    return verify_link();
}

Status Sx1262::begin() {
    const Status reset = reset_to_standby();
    if (reset != Status::Ok) return reset;
    const Status link = verify_link();
    if (link != Status::Ok) return link;

    // T-Echo (Plus) hardware wiring: DIO2 is the RF/antenna switch and DIO3
    // powers the TCXO at 1.8 V. Without the TCXO control the radio has no clock
    // and will neither TX nor RX. Enable both, then calibrate.
    uint8_t rfsw = 0x01;  // DIO2 controls RF switch
    cmd(sx::kSetDio2AsRfSwitch, &rfsw, 1);
    // SetDIO3AsTcxoCtrl: voltage byte + 3-byte startup delay (~5 ms @ 15.625 us).
    uint8_t tcxo[4] = {sx::kTcxoVolt1v8, 0x00, 0x01, 0x40};
    cmd(sx::kSetDio3AsTcxoCtrl, tcxo, 4);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    uint8_t calib = 0x7F;  // calibrate all blocks after switching to the TCXO
    cmd(sx::kCalibrate, &calib, 1);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    // INFO: wr 02aug26 DS 13.1.12: image rejection is calibrated per band and
    // only once the TCXO clock the calibration runs on is the chip's clock.
    cmd(sx::kCalibrateImage, sx::kImageBand863to870, sizeof(sx::kImageBand863to870));
    if (wait_busy_low() != Status::Ok) return Status::Timeout;

    configured_ = false;
    mode_ = RadioMode::Standby;
    return Status::Ok;
}

void Sx1262::write_register(uint16_t addr, const uint8_t* data, size_t n) {
    spi_.select(true);
    uint8_t head[3] = {sx::kWriteRegister, static_cast<uint8_t>(addr >> 8),
                       static_cast<uint8_t>(addr)};
    spi_.transfer(head, nullptr, sizeof(head));
    spi_.transfer(data, nullptr, n);
    spi_.select(false);
}

// DS 13.2.2 ReadRegister: opcode, address, one NOP the chip answers nothing to,
// then the bytes.
void Sx1262::read_register(uint16_t addr, uint8_t* out, size_t n) {
    spi_.select(true);
    uint8_t head[4] = {sx::kReadRegister, static_cast<uint8_t>(addr >> 8),
                       static_cast<uint8_t>(addr), 0};
    spi_.transfer(head, nullptr, sizeof(head));
    for (size_t i = 0; i < n; i++) {
        uint8_t tx = 0;
        spi_.transfer(&tx, &out[i], 1);
    }
    spi_.select(false);
}

// DS 13.4.6 SetModulationParams, GFSK, in the datasheet's order: bit rate,
// pulse shape, RX bandwidth, frequency deviation.
void Sx1262::configure_modulation(const MbandConfig& cfg) {
    const uint32_t bitrate = cfg.bitrate != 0 ? cfg.bitrate : 100000u;
    const uint32_t br = static_cast<uint32_t>(32ULL * sx::kXtalHz / bitrate);
    const uint32_t fdev =
        static_cast<uint32_t>((static_cast<uint64_t>(cfg.fdev_hz) << 25) / sx::kXtalHz);
    uint8_t params[8];
    params[0] = static_cast<uint8_t>(br >> 16);
    params[1] = static_cast<uint8_t>(br >> 8);
    params[2] = static_cast<uint8_t>(br);
    params[3] = sx::kPulseShapeNone;
    params[4] = sx::kRxBandwidth234kHz;
    params[5] = static_cast<uint8_t>(fdev >> 16);
    params[6] = static_cast<uint8_t>(fdev >> 8);
    params[7] = static_cast<uint8_t>(fdev);
    cmd(sx::kSetModulationParams, params, sizeof(params));
}

void Sx1262::configure_power() {
    cmd(sx::kSetPaConfig, sx::kPaConfigHighPower, sizeof(sx::kPaConfigHighPower));
    uint8_t params[2] = {static_cast<uint8_t>(sx::kConductedDbm), sx::kRampTime200Us};
    cmd(sx::kSetTxParams, params, sizeof(params));
}

// DS 13.3.1 SetDioIrqParams: the global mask, then one mask per DIO. DIO1 is the
// only interrupt line wired on this board.
void Sx1262::configure_irq() {
    const uint8_t hi = static_cast<uint8_t>(sx::kIrqMask >> 8);
    const uint8_t lo = static_cast<uint8_t>(sx::kIrqMask);
    uint8_t params[8] = {hi, lo, hi, lo, 0, 0, 0, 0};
    cmd(sx::kSetDioIrqParams, params, sizeof(params));
}

void Sx1262::configure_frame(const MbandConfig& cfg) {
    if (cfg.sync_bits == 0) return;
    write_register(sx::kSyncWordRegister, cfg.sync, (cfg.sync_bits + 7u) / 8u);
    // DS 13.4.6 SetPacketParams, GFSK, in the datasheet's order.
    uint8_t params[9] = {0};
    params[0] = static_cast<uint8_t>(sx::kPreambleChips >> 8);
    params[1] = static_cast<uint8_t>(sx::kPreambleChips);
    params[2] = sx::kPreambleDetect8Chips;
    params[3] = cfg.sync_bits;
    params[4] = sx::kAddrCompOff;
    params[5] = sx::kFixedLength;
    params[6] = cfg.payload_bytes;
    params[7] = sx::kCrcOff;
    params[8] = sx::kWhiteningOff;
    cmd(sx::kSetPacketParams, params, sizeof(params));
}

// INFO: wr 02aug26 DS 13.1: SetPacketType and SetRfFrequency are standby-only
// commands, and the previous dwell leaves the chip in continuous RX. Bracket the
// whole sequence and hand the caller back the mode it had.
Status Sx1262::configure_mband(const MbandConfig& cfg) {
    const RadioMode was = mode_;
    if (was != RadioMode::Standby && enter_standby() != Status::Ok) return Status::Timeout;
    cfg_ = cfg;
    uint8_t gfsk = 0x00;
    cmd(sx::kSetPacketType, &gfsk, 1);
    uint64_t frf = (static_cast<uint64_t>(cfg.freq_hz) << 25) / 32000000ULL;
    uint8_t f[4] = {static_cast<uint8_t>(frf >> 24), static_cast<uint8_t>(frf >> 16),
                    static_cast<uint8_t>(frf >> 8), static_cast<uint8_t>(frf)};
    cmd(sx::kSetRfFrequency, f, 4);
    configure_power();
    configure_modulation(cfg);
    configure_frame(cfg);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    configured_ = true;
    if (was == RadioMode::Rx) return start_receive();
    return Status::Ok;
}

// Preamble, sync window and payload at the configured bit rate, plus the margin
// SoftRF uses to decide a burst is never going to end (almic.cpp:510).
uint32_t Sx1262::tx_timeout_ticks(uint8_t len) const {
    const uint32_t bitrate = cfg_.bitrate != 0 ? cfg_.bitrate : 100000u;
    const uint32_t bits = sx::kPreambleChips + cfg_.sync_bits + static_cast<uint32_t>(len) * 8u;
    const uint32_t air_us =
        static_cast<uint32_t>(static_cast<uint64_t>(bits) * 1000000ULL / bitrate);
    const uint64_t ticks =
        (static_cast<uint64_t>(air_us) + sx::kTxGuardUs) * 1000ULL / sx::kTimeoutStepNs;
    return ticks > sx::kTimeoutTicksMax ? sx::kTimeoutTicksMax : static_cast<uint32_t>(ticks);
}

Status Sx1262::transmit(const uint8_t* data, uint8_t len) {
    if (!configured_) return Status::Invalid;
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    uint8_t offs = 0;
    spi_.select(true);
    uint8_t op = sx::kWriteBuffer;
    spi_.transfer(&op, nullptr, 1);
    spi_.transfer(&offs, nullptr, 1);
    spi_.transfer(data, nullptr, len);
    spi_.select(false);
    configure_irq();
    const uint32_t ticks = tx_timeout_ticks(len);
    uint8_t timeout[3] = {static_cast<uint8_t>(ticks >> 16), static_cast<uint8_t>(ticks >> 8),
                          static_cast<uint8_t>(ticks)};
    cmd(sx::kSetTx, timeout, 3);
    mode_ = RadioMode::Tx;
    return Status::Ok;
}

Status Sx1262::start_receive() {
    if (!configured_) return Status::Invalid;
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    configure_irq();
    uint8_t cont[3] = {0xFF, 0xFF, 0xFF};
    cmd(sx::kSetRx, cont, 3);
    mode_ = RadioMode::Rx;
    ms_since_rx_ = 0;
    return Status::Ok;
}

// DS 9.3: warm start keeps the configuration in retention across sleep, so what
// comes back is the radio that went to sleep and not a chip out of reset.
void Sx1262::sleep() {
    uint8_t config = sx::kSleepWarmStartNoRtc;
    cmd(sx::kSetSleep, &config, 1);
    mode_ = RadioMode::Sleep;
}

// DS 9.3: a falling edge on NSS is what wakes the part; it comes back in
// STDBY_RC with everything the warm start retained.
Status Sx1262::wake() {
    if (mode_ != RadioMode::Sleep) return Status::Ok;
    spi_.select(true);
    spi_.select(false);
    if (wait_busy_low() != Status::Ok) return Status::Timeout;
    return enter_standby();
}

// DS 13.5.2: the chip reports -2x the signal level in dBm, one byte.
int8_t Sx1262::rssi_inst() {
    uint8_t v = 0;
    cmd_read(sx::kGetRssiInst, &v, 1);
    return static_cast<int8_t>(-(static_cast<int>(v) / 2));
}

RadioEvent Sx1262::poll(uint8_t* rx_buf, uint8_t cap) {
    RadioEvent ev{};
    if (wait_busy_low(10000) != Status::Ok) {
        ev.type = RadioEventType::Fault;
        return ev;
    }
    uint8_t irq[2];
    cmd_read(sx::kGetIrqStatus, irq, 2);
    uint16_t flags = static_cast<uint16_t>((irq[0] << 8) | irq[1]);
    if (flags == 0) return ev;

    uint8_t clr[2] = {irq[0], irq[1]};
    cmd(sx::kClearIrqStatus, clr, 2);

    if (flags & sx::kIrqCrcErr) {
        ev.type = RadioEventType::CrcError;
        return ev;
    }
    if (flags & sx::kIrqTxDone) {
        ev.type = RadioEventType::TxDone;
        return ev;
    }
    if (flags & sx::kIrqTimeout) {
        ev.type = RadioEventType::Timeout;
        if (mode_ == RadioMode::Tx) recover_tx();
        return ev;
    }
    if (flags & sx::kIrqRxDone) {
        uint8_t st[2];
        cmd_read(sx::kGetRxBufferStatus, st, 2);
        uint8_t len = st[0];
        if (len > cap) len = cap;
        spi_.select(true);
        uint8_t op = sx::kReadBuffer, offs = st[1], nop = 0;
        spi_.transfer(&op, nullptr, 1);
        spi_.transfer(&offs, nullptr, 1);
        spi_.transfer(&nop, nullptr, 1);
        for (uint8_t i = 0; i < len; i++) {
            uint8_t tx = 0;
            spi_.transfer(&tx, &rx_buf[i], 1);
        }
        spi_.select(false);
        // DS 13.5.3 GetPacketStatus in GFSK: RxStatus, RssiSync, RssiAvg.
        uint8_t st3[3] = {0, 0, 0};
        cmd_read(sx::kGetPacketStatus, st3, 3);
        ev.rssi_dbm = static_cast<int8_t>(-(static_cast<int>(st3[2]) / 2));
        ev.type = RadioEventType::RxDone;
        ev.len = len;
        ms_since_rx_ = 0;
    }
    return ev;
}

// The PA is keyed until something unkeys it. The chip's own SetTx timeout is
// what notices; this is what puts the radio back where the dwell expects it.
void Sx1262::recover_tx() {
    tx_recovery_count_++;
    enter_standby();
    start_receive();
}

Status Sx1262::reinit() {
    reinit_count_++;
    Status s = begin();
    if (s != Status::Ok) return s;
    s = configure_mband(cfg_);
    if (s != Status::Ok) return s;
    return start_receive();
}

bool Sx1262::service(uint32_t elapsed_ms, uint32_t no_rx_reinit_ms) {
    if (mode_ != RadioMode::Rx) return false;
    ms_since_rx_ += elapsed_ms;
    if (ms_since_rx_ >= no_rx_reinit_ms) {
        reinit();
        ms_since_rx_ = 0;
        return true;
    }
    return false;
}

}
