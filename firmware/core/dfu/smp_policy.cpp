#include "core/dfu/smp_policy.h"

namespace skyblip::dfu {

bool smp_permitted(const SmpCommand& command, bool upload_allowed) {
    if (command.op == static_cast<uint8_t>(SmpOp::Read)) return true;
    if (command.op != static_cast<uint8_t>(SmpOp::Write)) return false;
    if (command.group == static_cast<uint16_t>(SmpGroup::Os)) return command.id == kSmpOsEcho;
    if (command.group == static_cast<uint16_t>(SmpGroup::Image))
        return command.id == kSmpImageUpload && upload_allowed;
    return false;
}

}  // namespace skyblip::dfu
