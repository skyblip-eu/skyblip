#ifndef SKYBLIP_RUNTIME_TASKS_H
#define SKYBLIP_RUNTIME_TASKS_H

#include <cstdint>

namespace skyblip::runtime {

enum class TaskPrio : uint8_t {
    Rf = 5,
    Gnss = 4,
    Services = 3,
    Comms = 2,
    Ui = 1,
};

constexpr uint32_t kServiceStepMs = 10;
constexpr uint32_t kTaskWatchdogMs = 5000;
constexpr uint32_t kRadioNoRxReinitMs = 30000;
constexpr uint32_t kPpsLossListenOnlyMs = 60000;
constexpr uint32_t kBaroPeriodMs = 250;

}  // namespace skyblip::runtime

#endif
