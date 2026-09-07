#ifndef SKYBLIP_CORE_DFU_UPDATE_H
#define SKYBLIP_CORE_DFU_UPDATE_H

#include <cstddef>
#include <cstdint>

#include "hal/dfu.h"

namespace skyblip::dfu {

enum class ImageState : uint8_t { Confirmed, Probation, Reverted };

const char* to_string(ImageState state);

struct UpdateRecord {
    hal::ImageVersion from{};
    hal::ImageVersion to{};
};

constexpr size_t kUpdateRecordBytes = 16;

size_t to_blob(const UpdateRecord& record, uint8_t* out, size_t cap);
bool from_blob(const uint8_t* blob, size_t len, UpdateRecord& out);

enum class Outcome : uint8_t { Landed, Reverted, Unrelated };

Outcome outcome(const UpdateRecord& record, const hal::ImageVersion& running);

constexpr size_t kVersionTextCap = 25;

int format_version(const hal::ImageVersion& version, char* out, size_t cap);

}  // namespace skyblip::dfu

#endif
