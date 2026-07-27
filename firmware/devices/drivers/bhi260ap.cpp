#include "devices/drivers/bhi260ap.h"

namespace skyblip::drivers {

bool Bhi260ap::read_reg(uint8_t reg, uint8_t* out, size_t len) {
    return bus_.write_read(addr_, &reg, 1, out, len);
}

bool Bhi260ap::write_reg(uint8_t reg, const uint8_t* data, size_t len) {
    if (len + 1 > sizeof(tx_)) return false;
    tx_[0] = reg;
    for (size_t i = 0; i < len; i++) tx_[1 + i] = data[i];
    return bus_.write(addr_, tx_, len + 1);
}

bool Bhi260ap::write_reg8(uint8_t reg, uint8_t value) { return write_reg(reg, &value, 1); }

ImuPresence Bhi260ap::probe() {
    uint8_t product = 0;
    if (!read_reg(bhi::kRegProductId, &product, 1)) return ImuPresence::Absent;
    uint8_t chip = 0;
    if (!read_reg(bhi::kRegChipId, &chip, 1)) return ImuPresence::Absent;
    if (product != bhi::kProductId || chip != bhi::kChipId) return ImuPresence::Mismatch;
    return ImuPresence::Present;
}

bool Bhi260ap::wait_boot_bit(uint8_t mask, uint8_t& status) {
    for (int i = 0; i < kBootPollTries; i++) {
        if (!read_reg(bhi::kRegBootStatus, &status, 1)) return false;
        if (status & bhi::kBootFwVerifyError) return false;
        if (status & mask) return true;
    }
    return false;
}

Status Bhi260ap::boot(FirmwareSource& fw) {
    booted_ = false;
    const size_t total = fw.size();
    // The upload command carries a length in 32-bit words, so a ragged image
    // cannot be expressed. Refuse rather than silently truncate.
    if (total == 0 || (total % 4) != 0) return Status::Invalid;

    if (probe() != ImuPresence::Present) return Status::NotFound;
    if (!write_reg8(bhi::kRegResetRequest, 0x01)) return Status::Down;

    uint8_t status = 0;
    if (!wait_boot_bit(bhi::kBootHostInterfaceReady, status)) return Status::Timeout;

    // HIRQ is not connected on this board, so the host must never wait on it.
    if (!write_reg8(bhi::kRegHostIrqCtrl, 0x00)) return Status::Down;

    // Command header: opcode then payload length in words, little-endian. Only
    // the FIRST transaction carries it; the body streams straight after.
    const uint32_t words = static_cast<uint32_t>(total / 4);
    const uint8_t header[4] = {
        static_cast<uint8_t>(bhi::kCmdUploadToProgramRam & 0xFF),
        static_cast<uint8_t>(bhi::kCmdUploadToProgramRam >> 8),
        static_cast<uint8_t>(words & 0xFF),
        static_cast<uint8_t>((words >> 8) & 0xFF),
    };
    if (!write_reg(bhi::kRegCommandInput, header, sizeof(header))) return Status::Down;

    uint8_t chunk[kUploadChunk];
    for (size_t off = 0; off < total; off += kUploadChunk) {
        const size_t n = (total - off) < kUploadChunk ? (total - off) : kUploadChunk;
        if (!fw.read(off, chunk, n)) return Status::Invalid;
        if (!write_reg(bhi::kRegCommandInput, chunk, n)) return Status::Down;
    }

    if (!wait_boot_bit(bhi::kBootFwVerifyDone, status)) return Status::Crc;

    const uint8_t boot_cmd[2] = {static_cast<uint8_t>(bhi::kCmdBootProgramRam & 0xFF),
                                 static_cast<uint8_t>(bhi::kCmdBootProgramRam >> 8)};
    if (!write_reg(bhi::kRegCommandInput, boot_cmd, sizeof(boot_cmd))) return Status::Down;
    if (!wait_boot_bit(bhi::kBootHostInterfaceReady, status)) return Status::Timeout;

    booted_ = true;
    return Status::Ok;
}

Status Bhi260ap::configure_sensor(uint8_t sensor_id, float rate_hz, uint32_t latency_ms) {
    // A framework command: the firmware serves it, so pre-boot it is not merely
    // ignored, it is not understood.
    if (!booted_) return Status::Down;

    // Payload: sensor id, sample rate as a 32-bit float, 24-bit latency.
    uint8_t p[10] = {static_cast<uint8_t>(bhi::kCmdConfigureSensor & 0xFF),
                     static_cast<uint8_t>(bhi::kCmdConfigureSensor >> 8), sensor_id};
    uint32_t rate_bits = 0;
    __builtin_memcpy(&rate_bits, &rate_hz, sizeof(rate_bits));
    p[3] = static_cast<uint8_t>(rate_bits & 0xFF);
    p[4] = static_cast<uint8_t>((rate_bits >> 8) & 0xFF);
    p[5] = static_cast<uint8_t>((rate_bits >> 16) & 0xFF);
    p[6] = static_cast<uint8_t>((rate_bits >> 24) & 0xFF);
    p[7] = static_cast<uint8_t>(latency_ms & 0xFF);
    p[8] = static_cast<uint8_t>((latency_ms >> 8) & 0xFF);
    p[9] = static_cast<uint8_t>((latency_ms >> 16) & 0xFF);
    return write_reg(bhi::kRegCommandInput, p, sizeof(p)) ? Status::Ok : Status::Down;
}

// FIFO packets are a 1-byte sensor id then a fixed payload. Only the two
// passthrough sensors are decoded; everything else is skipped by its known
// length, because a packet whose length we cannot work out desynchronises the
// rest of the buffer.
void Bhi260ap::parse_fifo(const uint8_t* data, size_t len, bool& got_accel) {
    size_t i = 0;
    while (i < len) {
        const uint8_t id = data[i];
        if (id == 0) break;  // padding: the rest of the buffer is empty

        size_t payload = 0;
        switch (id) {
            case bhi::kSensorAccelPassthrough:
            case bhi::kSensorGyroPassthrough: payload = 6; break;
            case 0xFA:                      // timestamp small delta
            case 0xFB: payload = 1; break;  // timestamp large delta
            case 0xFC: payload = 3; break;  // full timestamp
            case 0xFD:                      // meta event
            case 0xFE: payload = 3; break;  // meta event, wake
            default: return;                // unknown: stop, do not guess
        }
        if (i + 1 + payload > len) break;  // truncated tail: wait for the next drain

        if (id == bhi::kSensorAccelPassthrough || id == bhi::kSensorGyroPassthrough) {
            const uint8_t* p = &data[i + 1];
            ImuSample s;
            s.x = static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                       (static_cast<uint16_t>(p[1]) << 8));
            s.y = static_cast<int16_t>(static_cast<uint16_t>(p[2]) |
                                       (static_cast<uint16_t>(p[3]) << 8));
            s.z = static_cast<int16_t>(static_cast<uint16_t>(p[4]) |
                                       (static_cast<uint16_t>(p[5]) << 8));
            if (id == bhi::kSensorAccelPassthrough) {
                accel_ = s;
                got_accel = true;
            } else {
                gyro_ = s;
            }
        }
        i += 1 + payload;
    }
}

bool Bhi260ap::poll() {
    if (!booted_) return false;

    uint8_t len_bytes[2] = {0, 0};
    if (!read_reg(bhi::kRegFifoNonWakeLen, len_bytes, sizeof(len_bytes))) return false;
    size_t available = static_cast<size_t>(len_bytes[0]) | (static_cast<size_t>(len_bytes[1]) << 8);
    if (available == 0) return false;

    bool got_accel = false;
    uint8_t buf[kFifoChunk];
    while (available > 0) {
        const size_t n = available < kFifoChunk ? available : kFifoChunk;
        if (!read_reg(bhi::kRegFifoNonWake, buf, n)) return got_accel;
        parse_fifo(buf, n, got_accel);
        available -= n;
    }
    return got_accel;
}

}  // namespace skyblip::drivers
