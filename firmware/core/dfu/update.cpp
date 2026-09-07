#include "core/dfu/update.h"

#include "core/util/format.h"

namespace skyblip::dfu {

const char* to_string(ImageState state) {
    switch (state) {
        case ImageState::Confirmed: return "confirmed";
        case ImageState::Probation: return "probation";
        case ImageState::Reverted: return "reverted";
    }
    return "confirmed";
}

namespace {

void put_u16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v);
    out[1] = static_cast<uint8_t>(v >> 8);
}

void put_u32(uint8_t* out, uint32_t v) {
    put_u16(out, static_cast<uint16_t>(v));
    put_u16(out + 2, static_cast<uint16_t>(v >> 16));
}

uint16_t get_u16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t get_u32(const uint8_t* in) {
    return static_cast<uint32_t>(get_u16(in)) | (static_cast<uint32_t>(get_u16(in + 2)) << 16);
}

void put_version(uint8_t* out, const hal::ImageVersion& v) {
    out[0] = v.major;
    out[1] = v.minor;
    put_u16(out + 2, v.revision);
    put_u32(out + 4, v.build);
}

hal::ImageVersion get_version(const uint8_t* in) {
    hal::ImageVersion v;
    v.major = in[0];
    v.minor = in[1];
    v.revision = get_u16(in + 2);
    v.build = get_u32(in + 4);
    return v;
}

}  // namespace

size_t to_blob(const UpdateRecord& record, uint8_t* out, size_t cap) {
    if (cap < kUpdateRecordBytes) return 0;
    put_version(out, record.from);
    put_version(out + 8, record.to);
    return kUpdateRecordBytes;
}

bool from_blob(const uint8_t* blob, size_t len, UpdateRecord& out) {
    if (len != kUpdateRecordBytes) return false;
    out.from = get_version(blob);
    out.to = get_version(blob + 8);
    return true;
}

Outcome outcome(const UpdateRecord& record, const hal::ImageVersion& running) {
    if (running == record.to) return Outcome::Landed;
    if (running == record.from) return Outcome::Reverted;
    return Outcome::Unrelated;
}

int format_version(const hal::ImageVersion& version, char* out, size_t cap) {
    if (cap < kVersionTextCap) {
        if (cap > 0) out[0] = 0;
        return 0;
    }
    int n = fmt_uint(out, version.major);
    out[n++] = '.';
    n += fmt_uint(out + n, version.minor);
    out[n++] = '.';
    n += fmt_uint(out + n, version.revision);
    out[n++] = '+';
    n += fmt_uint(out + n, version.build);
    out[n] = 0;
    return n;
}

}  // namespace skyblip::dfu
