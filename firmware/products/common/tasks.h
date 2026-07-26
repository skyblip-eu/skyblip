// products/common/tasks.h — shared boot scaffolding (3-ARCHITECTURE §8): task
#ifndef SKYBLIP_PRODUCTS_COMMON_TASKS_H
#define SKYBLIP_PRODUCTS_COMMON_TASKS_H

#include <cstdint>

namespace skyblip::product {

enum class TaskPrio : uint8_t {
    Radio = 5,
    Gnss = 4,
    Comms = 3,
    Sensors = 2,
    Display = 1,
};

constexpr int kRxRadioQueue = 16;
constexpr int kTxPlanQueue = 4;
constexpr int kLinkRxQueue = 8;
constexpr int kDisplayQueue = 4;

constexpr uint32_t kTaskWatchdogMs = 5000;
constexpr uint32_t kRadioNoRxReinitMs = 30000;
constexpr uint32_t kPpsLossListenOnlyMs = 60000;

}

#endif
