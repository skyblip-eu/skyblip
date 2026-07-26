#include "core/util/intmath.h"

namespace skyblip {

namespace {
const int16_t kSineQuarter[65] = {
    0,     402,   804,   1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,  4370,  4756,
    5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,  9102,  9434,
    9760,  10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
    13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
}

int16_t isin(int16_t angle) {
    uint16_t a = static_cast<uint16_t>(angle);
    uint16_t quadrant = (a >> 14) & 3;
    uint16_t idx = (a >> 8) & 0x3F;
    uint16_t frac = a & 0xFF;
    int32_t lo, hi;
    switch (quadrant) {
        case 0:
            lo = kSineQuarter[idx];
            hi = kSineQuarter[idx + 1];
            break;
        case 1:
            lo = kSineQuarter[64 - idx];
            hi = kSineQuarter[63 - idx];
            break;
        case 2:
            lo = -kSineQuarter[idx];
            hi = -kSineQuarter[idx + 1];
            break;
        default:
            lo = -kSineQuarter[64 - idx];
            hi = -kSineQuarter[63 - idx];
            break;
    }
    int32_t v = lo + ((hi - lo) * static_cast<int32_t>(frac)) / 256;
    return static_cast<int16_t>(v);
}

int16_t iatan2(int32_t y, int32_t x) {
    if (x == 0 && y == 0) return 0;
    int32_t ax = x < 0 ? -x : x;
    int32_t ay = y < 0 ? -y : y;
    int32_t angle;
    if (ax >= ay) {
        int64_t r = (static_cast<int64_t>(ay) << 14) / (ax ? ax : 1);
        angle = static_cast<int32_t>((r * 0x2000) >> 14);
        angle -= static_cast<int32_t>(((r * (0x4000 - r)) >> 14) * 0x0AAA >> 14);
    } else {
        int64_t r = (static_cast<int64_t>(ax) << 14) / (ay ? ay : 1);
        angle = static_cast<int32_t>((r * 0x2000) >> 14);
        angle -= static_cast<int32_t>(((r * (0x4000 - r)) >> 14) * 0x0AAA >> 14);
        angle = 0x4000 - angle;
    }
    if (x < 0) angle = 0x8000 - angle;
    if (y < 0) angle = -angle;
    return static_cast<int16_t>(angle & 0xFFFF);
}

int32_t ifast_distance(int32_t dx, int32_t dy) {
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int32_t mn, mx;
    if (dx < dy) {
        mn = dx;
        mx = dy;
    } else {
        mn = dy;
        mx = dx;
    }
    int32_t approx = mx * 1007 + mn * 441;
    if (mx < (mn << 4)) approx -= mx * 40;
    return (approx + 512) >> 10;
}

}
