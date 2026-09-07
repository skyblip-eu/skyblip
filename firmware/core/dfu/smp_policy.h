#ifndef SKYBLIP_CORE_DFU_SMP_POLICY_H
#define SKYBLIP_CORE_DFU_SMP_POLICY_H

#include <cstdint>

namespace skyblip::dfu {

enum class SmpOp : uint8_t { Read = 0, ReadResponse = 1, Write = 2, WriteResponse = 3 };
enum class SmpGroup : uint16_t { Os = 0, Image = 1 };

constexpr uint8_t kSmpOsEcho = 0;
constexpr uint8_t kSmpOsReset = 5;
constexpr uint8_t kSmpImageState = 0;
constexpr uint8_t kSmpImageUpload = 1;
constexpr uint8_t kSmpImageErase = 5;

struct SmpCommand {
    uint16_t group{0};
    uint8_t id{0};
    uint8_t op{0};
};

bool smp_permitted(const SmpCommand& command, bool upload_allowed);

}  // namespace skyblip::dfu

#endif
